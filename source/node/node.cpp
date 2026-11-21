#include <mosaic/log/checkpoint.h>
#include <mosaic/node/node.h>
#include <mosaic/proto/codec.h>

#include <algorithm>

using std::greater;
using std::make_unique;
using std::max;
using std::min;
using std::move;
using std::nullopt;
using std::optional;
using std::pair;
using std::remove;
using std::size_t;
using std::sort;
using std::string;
using std::uint32_t;
using std::uint64_t;
using std::vector;
using std::visit;

namespace mosaic {
namespace {
template <class... Fs> struct Overloaded : Fs... {
    using Fs::operator()...;
};
template <class... Fs> Overloaded(Fs...) -> Overloaded<Fs...>;

/// Strikes are reduced proportionally to the time that has passed, with no history per peer: a
/// window of counters would need memory that grows with the network, and memory an attacker can
/// grow (D2 in the design). Integer arithmetic only, so it is identical on every platform.
///
/// The result of a step that earns no decay is *not* recorded, so a fast caller whose every step
/// is too short to decay a single strike still decays eventually instead of never: elapsed time
/// accumulates against the mark.
uint32_t decayed(uint32_t strikes, uint64_t mark_ms, uint64_t now_ms, uint32_t window_ms) {
    if (strikes == 0 || window_ms == 0 || now_ms <= mark_ms) {
        return strikes;
    }
    const auto elapsed = now_ms - mark_ms;
    const auto reduce = static_cast<uint64_t>(strikes) * elapsed / window_ms;
    if (reduce >= strikes) {
        return 0;
    }
    return strikes - static_cast<uint32_t>(reduce);
}
}  // namespace

const char* to_string(PeerFault fault) {
    switch (fault) {
        case PeerFault::Undecodable:
            return "undecodable";
        case PeerFault::Unverified:
            return "unverified";
        case PeerFault::Oversized:
            return "oversized";
        case PeerFault::Unrequested:
            return "unrequested";
        case PeerFault::VersionMismatch:
            return "version";
    }
    return "unknown";
}

uint32_t strike_weight(PeerFault fault) {
    switch (fault) {
        case PeerFault::Undecodable:      // usually a bug or a version skew, and cheap to check
        case PeerFault::VersionMismatch:  // usually a deployment mistake, not an attack
            return 1;
        case PeerFault::Unverified:
            return 3;  // a broken writer or an attacker: either way it must stop quickly
        case PeerFault::Oversized:
        case PeerFault::Unrequested:  // volunteering repair records is never legitimate traffic
            return 2;
    }
    return 1;
}

Node::Node(NodeConfig config, Transport& transport, LogStore& store, const Hasher& hasher,
           const Verifier& verifier)
    : config_(std::move(config)), transport_(transport), store_(store), hasher_(hasher),
      replica_(store, hasher, verifier), rotation_(store, verifier) {
    // The chain origin is a checkpoint when retention has dropped the predecessor: see retain().
    replica_.set_origin([this](const LogKey& key, Seq seq) { return checkpoint_hash(key, seq); });
}

optional<Hash> Node::checkpoint_hash(const LogKey& key, Seq seq) const {
    const auto blob = store_.checkpoint(key);
    if (!blob) {
        return nullopt;
    }
    const auto checkpoint = decode_checkpoint(*blob);
    if (!checkpoint || checkpoint->key != key || checkpoint->seq != seq) {
        return nullopt;
    }
    return checkpoint->hash;
}

Node::RetentionOutcome Node::retain(const Signer& signer, uint64_t now_ms) {
    RetentionOutcome outcome;
    if (config_.retention_max_age_ms == 0 && config_.retention_keep_min == 0) {
        return outcome;  // nothing asked for: retention never destroys anything by default
    }
    if (config_.retention_interval_ms != 0 && last_retention_ms_ != 0 &&
        now_ms - last_retention_ms_ < config_.retention_interval_ms) {
        return outcome;
    }
    last_retention_ms_ = now_ms;
    ++counters_.retention_runs;

    for (auto& [key, slot] : writers_) {
        const auto head = store_.head_seq(key);
        if (!head || *head == 0) {
            continue;
        }
        // The first record kept: `keep_min` records survive, everything before this may go. (The
        // +1 is what makes it exactly the floor rather than one more than asked for.)
        Seq candidate = 0;
        if (static_cast<uint64_t>(*head) + 1 > config_.retention_keep_min) {
            candidate = *head + 1 - static_cast<Seq>(config_.retention_keep_min);
        }
        if (config_.retention_max_age_ms != 0) {
            // Walk back while the records are still too young, with a bound so one call cannot turn
            // into a scan of the whole log.
            int budget = 1024;
            while (candidate > 0 && budget-- > 0) {
                const auto at = store_.get(key, candidate - 1);
                if (!at) {
                    break;
                }
                const auto age = now_ms > at->timestamp_ms ? now_ms - at->timestamp_ms : 0;
                if (age <= config_.retention_max_age_ms) {
                    break;
                }
                --candidate;
            }
        }
        if (candidate == 0) {
            continue;  // nothing old enough, or nothing beyond the floor
        }

        // Replication comes first when a floor is configured: dropping on a guess is exactly the
        // guarantee ACK-1 exists to provide.
        if (config_.min_replicas != 0) {
            const auto released = released_through_.find(key);
            if (released == released_through_.end() || released->second < candidate) {
                ++outcome.refused;
                ++counters_.retention_refused;
                continue;
            }
        }

        // The checkpoint names the last record being dropped, signed by the only party allowed to
        // say what that history was.
        const auto anchor = store_.get(key, candidate - 1);
        const auto anchor_hash = anchor ? record_hash_tagged(*anchor) : nullopt;
        if (!anchor || !anchor_hash) {
            ++outcome.refused;
            ++counters_.retention_refused;
            continue;
        }
        Checkpoint checkpoint;
        checkpoint.key = key;
        checkpoint.seq = anchor->seq;
        checkpoint.hash = *anchor_hash;
        checkpoint.hash_algo = anchor->hash_algo;
        checkpoint = sign_checkpoint(std::move(checkpoint), signer);
        if (!store_.put_checkpoint(key, encode_checkpoint(checkpoint))) {
            ++outcome.refused;  // the backend cannot keep a checkpoint, so it cannot truncate
            ++counters_.retention_refused;
            continue;
        }
        const auto dropped = store_.truncate_before(key, candidate);
        if (!dropped) {
            ++outcome.refused;
            ++counters_.retention_refused;
            continue;
        }
        outcome.logs += 1;
        outcome.dropped += *dropped;
        counters_.retention_dropped += *dropped;
    }
    return outcome;
}

bool Node::follow(const string& pattern) {
    if (!following_.add(pattern)) {
        return false;
    }
    patterns_.push_back(pattern);
    transport_.subscribe(pattern);
    return true;
}

size_t Node::publish_batch(const Signer& signer, const Channel& channel, const string& content_type,
                           const vector<Bytes>& payloads, uint64_t now_ms) {
    size_t written = 0;
    for (const auto& payload : payloads) {
        if (!publish(signer, channel, content_type, payload, now_ms)) {
            break;  // refused (oversized or not ours to write): stop rather than silently skip
        }
        ++written;
    }
    return written;
}

optional<Record> Node::publish(const Signer& signer, const Channel& channel, string content_type,
                               Bytes payload, uint64_t now_ms) {
    const LogKey key{.writer = signer.id(), .channel = channel};
    if (payload.size() > config_.max_record_bytes) {
        ++counters_.oversized_records;
        return nullopt;  // a writer must not create history its own peers would refuse
    }
    if (!authorized(key)) {
        return nullopt;
    }
    auto it = writers_.find(key);
    if (it == writers_.end()) {
        WriterSlot slot{.log = make_unique<LogWriter>(store_, hasher_, signer, channel),
                        .last_head_ms = now_ms};
        it = writers_.emplace(key, std::move(slot)).first;
    }
    Record record = it->second.log->append(std::move(content_type), std::move(payload), now_ms);
    transport_.publish(channel, RecordMsg{record});
    return record;
}

void Node::pump(uint64_t now_ms) {
    last_now_ms_ = now_ms;
    while (auto envelope = transport_.poll()) {
        // A repair reply is a run of DirectRecordMsg from one peer for one log. Buffer a run and
        // append it with a single store call; anything else flushes first, so ordering between a
        // record and the message that follows it is unchanged (LOG-2).
        if (const auto* direct = std::get_if<DirectRecordMsg>(&envelope->message)) {
            const auto key = direct->record.key();
            if (direct_batch_key_.has_value() &&
                (direct_batch_key_->first != envelope->from || direct_batch_key_->second != key)) {
                flush_direct_batch(now_ms);
            }
            if (!precheck_record(envelope->from, direct->record, true, now_ms)) {
                continue;  // refused before the store, exactly as the single-record path would
            }
            direct_batch_key_ = std::pair<NodeId, LogKey>{envelope->from, key};
            direct_batch_.push_back(direct->record);
            if (direct_batch_.size() >= config_.fetch_batch) {
                flush_direct_batch(now_ms);
            }
            continue;
        }
        flush_direct_batch(now_ms);
        handle(envelope->from, envelope->message, now_ms);
    }
    flush_direct_batch(now_ms);
    // A frame that did not decode never becomes a Message, so it is the one fault the choke point
    // cannot see. The transport reports it by sender instead of failing silently (SEC-2).
    for (const auto& peer : transport_.take_decode_faults()) {
        penalize(peer, PeerFault::Undecodable, now_ms);
    }
}

void Node::tick(uint64_t now_ms) {
    last_now_ms_ = now_ms;
    // 0. Bans that have lapsed stop being bans on their own: nothing here needs an operator, and
    // the lapse is reported so a silence is never mystifying (SEC-2).
    expire_bans(now_ms);

    // 0. What we are about to advertise, then the greeting that carries it: rebuilt first, or the
    // first hello of a link would go out empty and ranking would have nothing to work with until
    // the next interval.
    refresh_advert(now_ms);

    // 0a. Version handshake: every peer we can address hears our version until it answers with its
    // own.
    greet_peers(now_ms);

    // 0a2. Discovery: offer what we know about the network, and forget what nothing refreshed.
    gossip_tick(now_ms);

    // 0b. Control-plane work: find logs we are missing, and tell writers what we hold.
    request_heads(now_ms);
    acknowledge(now_ms);

    // 1. HEAD heartbeats for logs we write.
    for (auto& [key, slot] : writers_) {
        if (now_ms - slot.last_head_ms < config_.head_interval_ms) {
            continue;
        }
        if (auto head = slot.log->head()) {
            transport_.publish(key.channel,
                               HeadMsg{.key = key, .seq = head->seq, .hash = head->hash});
        }
        slot.last_head_ms = now_ms;
    }

    // 2. FETCH housekeeping: forget finished requests, retry timed-out ones.
    // Credit is returned per request, and only once: a request that is abandoned returns what it
    // owed right here, so responses that arrive afterwards find no live request and cannot create
    // credit (FLOW-2). Resetting the peer's whole window instead would let one slow round inflate
    // the next.
    vector<pair<LogKey, NodeId>> retry;
    for (auto it = fetches_.begin(); it != fetches_.end();) {
        const auto& [key_peer, state] = *it;
        if (next_seq(key_peer.first) > state.end_seq) {
            return_credit(key_peer.second, state.credit_left);  // complete
            it = fetches_.erase(it);
        } else if (now_ms - state.sent_ms >= config_.fetch_timeout_ms) {
            return_credit(key_peer.second, state.credit_left);  // abandoned; may still answer later
            retry.push_back(key_peer);
            ++counters_.fetch_retries;
            it = fetches_.erase(it);
        } else {
            ++it;
        }
    }
    for (const auto& [key, peer] : retry) {
        request_fetch(key, peer, now_ms);
    }
}

void Node::handle(const NodeId& from, const Message& message, uint64_t now_ms) {
    if (!admit(from, message, now_ms)) {
        return;  // banned: dropped here, before any decoding, verification or store access
    }
    visit(Overloaded{
              [&](const RecordMsg& m) { handle_record(from, m.record, false, now_ms); },
              [&](const DirectRecordMsg& m) { handle_record(from, m.record, true, now_ms); },
              [&](const HeadMsg& m) { handle_head(from, m, now_ms); },
              [&](const FetchMsg& m) { handle_fetch(from, m, now_ms); },
              [&](const HelloMsg& m) { handle_hello(from, m, now_ms); },
              [&](const AckMsg& m) { handle_ack(from, m); },
              [&](const GetHeadsMsg& m) { handle_get_heads(from, m, now_ms); },
              [&](const DirectHeadMsg& m) { handle_direct_head(from, m, now_ms); },
              [&](const CreditMsg& m) { handle_credit(from, m); },
              [&](const PeersMsg& m) { handle_peers(from, m, now_ms); },

          },
          message);
}

void Node::flush_direct_batch(uint64_t now_ms) {
    if (direct_batch_.empty() || !direct_batch_key_) {
        direct_batch_key_.reset();
        return;
    }
    const auto from = direct_batch_key_->first;
    const auto records = std::move(direct_batch_);
    direct_batch_.clear();
    direct_batch_key_.reset();
    const auto outcomes = replica_.ingest_batch(records);
    for (size_t i = 0; i < records.size(); ++i) {
        apply_ingest(from, records[i], true, outcomes[i], now_ms);
    }
}

void Node::handle_record(const NodeId& from, const Record& record, bool direct, uint64_t now_ms) {
    if (!precheck_record(from, record, direct, now_ms)) {
        return;
    }
    apply_ingest(from, record, direct, replica_.ingest(record), now_ms);
}

bool Node::precheck_record(const NodeId& from, const Record& record, bool direct, uint64_t now_ms) {
    if (!accept_payload(from)) {
        return false;
    }
    if (!direct && live_credit_enabled_[from]) {
        // Credit was granted on this link, so live delivery is paced; a record we cannot afford is
        // lost and repaired, never queued (FLOW-3).
        if (live_credits_[from].take_up_to(1) == 0) {
            ++counters_.credit_drops;
            return false;
        }
    }
    if (!following_.matches_any(record.channel)) {
        return false;
    }

    if (record.payload.size() > config_.max_record_bytes) {
        ++counters_.oversized_records;
        penalize(from, PeerFault::Oversized, now_ms);
        return false;  // refused before the store, the callback or the chain
    }
    if (!authorized(record.key())) {
        return false;  // a channel rule does not permit this writer: never stored, never delivered
    }

    const LogKey key = record.key();
    bool requested = false;
    if (direct) {
        requested = consume_credit(from, key, record.seq);
        // "Nobody asked for this" has to be judged against everything we ever asked this peer for,
        // not against what is still outstanding: a response that arrives after its request timed
        // out, or twice because the link duplicated it, is late, not abusive (spec:
        // security/peer-scoring).
        const auto through = requested_through_.find({key, from});
        const bool ever_requested =
            through != requested_through_.end() && record.seq <= through->second;
        // Ingest still runs below even when the strike lands: a record is judged on its signature,
        // never on who sent it or how that peer has behaved (D6 in the design).
        if (!requested && !ever_requested) {
            penalize(from, PeerFault::Unrequested, now_ms);
        }
    } else if (record.seq > next_seq(key) + config_.slow_follower_threshold) {
        // Too far behind to keep up with a firehose: stop trusting live delivery for this log and
        // let the repair path do the work, which bounds our backlog by our own request size
        // (FLOW-4). The comparison is against this record's own sequence: the head we know is
        // updated below.
        ++counters_.slow_follower_switches;
        request_fetch(key, from, now_ms);
        return false;
    }

    auto& known = remote_head_[key];
    known = max(known, record.seq);
    if (!source_peer_.contains(key)) {
        source_peer_[key] = from;
    }
    return true;
}

void Node::apply_ingest(const NodeId& from, const Record& record, bool direct, IngestResult result,
                        uint64_t now_ms) {
    const LogKey key = record.key();
    const auto known_it = remote_head_.find(key);
    const auto known = known_it == remote_head_.end() ? record.seq : known_it->second;
    switch (result) {
        case IngestResult::Accepted:
            ++counters_.accepted;
            ingested_logs_.insert(key);
            if (record.content_type == kRotationContentType) {
                // A link changes who may write here: drop the cached answer so the next decision
                // sees it, without a restart.
                rotation_.invalidate(record.writer, record.channel);
            }
            if (on_record_) {
                on_record_(record);
            }
            if (direct && next_seq(key) <= known) {
                request_fetch(key, from, now_ms);  // more to catch up on
            }
            break;
        case IngestResult::Gap:
            ++counters_.gap;
            request_fetch(key, from, now_ms);
            break;
        case IngestResult::Fork:
            ++counters_.fork;
            if (on_fork_) {
                on_fork_(record);
            }
            break;
        case IngestResult::Duplicate:
            ++counters_.duplicate;
            break;
        case IngestResult::NotAttempted:
            break;  // only a batch reports this, and this path handles one record
        case IngestResult::BadChain:
        case IngestResult::BadSignature:
            // The two are one fault kind on purpose: to a node that has to decide how much
            // attention a peer deserves, "cannot verify" is the whole of the information.
            if (result == IngestResult::BadChain) {
                ++counters_.bad_chain;
            } else {
                ++counters_.bad_signature;
            }
            penalize(from, PeerFault::Unverified, now_ms);
            return;
    }
    // Every outcome but the two above proves the peer can sign: that is the "verifiable record"
    // half of an early lift.
    confirmed(from, now_ms);
}

void Node::handle_head(const NodeId& from, const HeadMsg& head, uint64_t now_ms) {
    if (!accept_payload(from)) {
        return;
    }
    if (unauthorized_logs_.contains(head.key)) {
        return;  // a rule refuses this log; its heads are not worth acting on
    }
    if (!following_.matches_any(head.key.channel)) {
        return;
    }
    auto& known = remote_head_[head.key];
    known = max(known, head.seq);
    if (next_seq(head.key) <= head.seq) {
        request_fetch(head.key, from, now_ms);
    }
}

void Node::handle_fetch(const NodeId& from, const FetchMsg& fetch, uint64_t now_ms) {
    if (!accept_payload(from)) {
        return;
    }
    if (!admit_request(from, now_ms)) {
        return;
    }
    const auto count = min(fetch.count, config_.max_serve);
    for (auto& record : store_.read(fetch.key, fetch.from, count)) {
        transport_.send(from, DirectRecordMsg{std::move(record)});
    }
}

void Node::request_fetch(const LogKey& key, const NodeId& peer, uint64_t now_ms) {
    if (unauthorized_logs_.contains(key)) {
        return;  // never fetch a log a rule forbids
    }
    const Seq from = next_seq(key);
    const auto known = remote_head_.find(key);
    if (known == remote_head_.end() || known->second < from) {
        return;  // nothing missing as far as we know
    }

    const auto pending = fetches_.find({key, peer});
    if (pending != fetches_.end() && from <= pending->second.end_seq &&
        now_ms - pending->second.sent_ms < config_.fetch_timeout_ms) {
        return;  // a request for this range is already in flight
    }

    const auto wanted =
        static_cast<uint32_t>(min<Seq>(known->second - from + 1, config_.fetch_batch));
    const auto granted = credits_for(peer).take_up_to(wanted);
    if (granted == 0) {
        ++counters_.credit_drops;
        return;  // no credit: wait for responses (or the timeout) before asking again
    }

    if (pending != fetches_.end()) {
        return_credit(peer,
                      pending->second.credit_left);  // the range moved on; close the old request
        fetches_.erase(pending);
    }

    transport_.send(peer, FetchMsg{.key = key, .from = from, .count = granted});
    counters_.credit_spent += granted;
    fetches_[{key, peer}] = FetchState{
        .sent_ms = now_ms, .from = from, .end_seq = from + granted - 1, .credit_left = granted};
    ++counters_.fetches_sent;
    auto& through = requested_through_[{key, peer}];
    through = max(through, from + granted - 1);
}

bool Node::consume_credit(const NodeId& peer, const LogKey& key, Seq seq) {
    const auto it = fetches_.find({key, peer});
    if (it == fetches_.end() || seq < it->second.from || seq > it->second.end_seq ||
        it->second.credit_left == 0) {
        return false;  // late or unsolicited: credit already returned, so it must not create any
    }
    --it->second.credit_left;
    credits_for(peer).grant(1);
    ++counters_.credit_returned;
    return true;
}

void Node::return_credit(const NodeId& peer, uint32_t amount) {
    if (amount != 0) {
        credits_for(peer).grant(amount);
        counters_.credit_returned += amount;
    }
}

uint32_t Node::credit_available(const NodeId& peer) const {
    const auto it = peer_credits_.find(peer);
    // Nothing has been spent for a peer we never fetched from, so the whole window is available.
    return it == peer_credits_.end() ? config_.fetch_window : it->second.available();
}

void Node::grant_live_credit(const NodeId& peer, uint32_t credits) {
    live_credit_enabled_[peer] = true;
    auto& window = live_credits_.try_emplace(peer, credits, credits).first->second;
    window.grant(credits);
}

uint32_t Node::retained_records() const {
    uint32_t total = 0;
    for (const auto& [key, slot] : writers_) {
        const auto head = store_.head_seq(key);
        if (!head) {
            continue;
        }
        const auto released = released_through_.find(key);
        const Seq floor = released == released_through_.end() ? 0 : released->second;
        if (*head > floor) {
            total += static_cast<uint32_t>(*head - floor);
        }
    }
    return total;
}

vector<LogKey> Node::retention_shortfalls() const {
    vector<LogKey> out;
    out.reserve(shortfall_at_.size());
    for (const auto& [key, at] : shortfall_at_) {
        out.push_back(key);
    }
    return out;
}

void Node::handle_ack(const NodeId& from, const AckMsg& ack) {
    ++counters_.acks_received;
    auto& known = acked_[{ack.key, from}];
    // Idempotent and monotonic: a duplicate or stale acknowledgement can never regress or double
    // count.
    known = max(known, ack.seq);
    advance_retention(ack.key);
}

void Node::advance_retention(const LogKey& key) {
    if (config_.min_replicas == 0) {
        return;
    }
    const auto head = store_.head_seq(key);
    if (!head) {
        return;
    }

    // The floor is the highest sequence that at least min_replicas replicas have confirmed - which
    // is just the min_replicas-th largest acknowledgement. Collecting and selecting is O(r log r)
    // in the number of replicas; the previous version walked every sequence in the range and
    // counted acks per sequence.
    Seq candidate = released_through_.contains(key) ? released_through_[key] : 0;
    vector<Seq> acks;
    for (const auto& [entry, acked] : acked_) {
        if (entry.first == key) {
            acks.push_back(acked);
        }
    }
    if (acks.size() >= config_.min_replicas && config_.min_replicas > 0) {
        std::ranges::sort(acks, greater<>());
        candidate = max(candidate, acks[config_.min_replicas - 1]);
    }
    released_through_[key] = candidate;

    // Released means "no longer waiting", not "deleted": the store keeps the record.
    const Seq still_held = *head - candidate;
    if (still_held > config_.retention_limit) {
        released_through_[key] = *head - config_.retention_limit;
        shortfall_at_[key] = *head;
        ++counters_.retention_shortfall;
    }
}

void Node::handle_get_heads(const NodeId& from, const GetHeadsMsg& request, uint64_t now_ms) {
    if (request.patterns.size() > config_.max_head_patterns) {
        ++counters_.head_requests_refused;  // refused whole: a matcher is never built for a hostile
                                            // request
        return;
    }
    if (!admit_request(from, now_ms)) {
        return;
    }
    Matcher patterns;
    for (const auto& pattern : request.patterns) {
        if (!patterns.add(pattern)) {
            return;  // an invalid pattern is a caller error: answer nothing rather than guess
        }
    }

    ++counters_.heads_answered;
    uint32_t answered = 0;
    for (const auto& key : store_.keys()) {
        if (answered >= config_.max_serve) {
            break;
        }
        if (!patterns.matches_any(key.channel)) {
            continue;
        }
        const auto head = store_.head_seq(key);
        if (!head) {
            continue;
        }
        const auto last = store_.get(key, *head);
        if (!last) {
            continue;
        }
        const auto hash = record_hash_tagged(*last);
        if (!hash) {
            continue;
        }
        transport_.send(from, DirectHeadMsg{.key = key, .seq = *head, .hash = *hash});
        ++answered;
    }
}

void Node::handle_direct_head(const NodeId& from, const DirectHeadMsg& head, uint64_t now_ms) {
    if (!following_.matches_any(head.key.channel)) {
        return;
    }
    auto& known = remote_head_[head.key];
    known = max(known, head.seq);
    if (next_seq(head.key) <= head.seq) {
        request_fetch(head.key, from, now_ms);
    }
}

void Node::handle_credit(const NodeId& from, const CreditMsg& credit) {
    live_credit_enabled_[from] = true;
    live_credits_.try_emplace(from, credit.credits * 4, credit.credits)
        .first->second.grant(credit.credits);
}

void Node::handle_peers(const NodeId& from, const PeersMsg& message, uint64_t now_ms) {
    // A list this node must refuse is a message it cannot use, which is the same fault the wire
    // decoder reports for the same bytes: one strike, message refused whole, nothing partly
    // believed (D6).
    if (message.entries.size() > kMaxPeersEntries) {
        ++counters_.gossip_refused;
        penalize(from, PeerFault::Undecodable, now_ms);
        return;
    }
    // Peer lists count against the same per-peer request budget as every other control message, so
    // a gossip burst cannot outrun the throttling that everything else obeys.
    if (!admit_request(from, now_ms)) {
        return;
    }
    for (const auto& entry : message.entries) {
        const auto hops = entry.hops + 1;
        const auto refuse = [&](const char* why) {
            (void)why;  // the counter is the signal; the reason is in the design note
            ++counters_.gossip_refused;
        };
        if (entry.id.empty() || entry.id == config_.id ||
            !valid_peer_endpoint(entry.pub_endpoint) ||
            !valid_peer_endpoint(entry.router_endpoint) ||
            entry.age_ms > config_.gossip_entry_ttl_ms || hops > config_.gossip_max_hops) {
            refuse("entry unwearable");
            continue;
        }

        // A configured peer's address is not gossip's business: the entry confirms that it exists
        // and nothing more (spec: discovery/peer-discovery).
        if (configured_peers_.contains(entry.id)) {
            ++counters_.peers_refreshed;
            continue;
        }

        auto& slot = gossip_[entry.id];
        // An entry older than what we already believe must never refresh it. Without this a peer
        // can replay a stale entry forever and keep a dead node in the table: freshness is judged
        // against what we hold now (its age plus the time since we heard it), never against our own
        // clock - clocks are not comparable across nodes.
        if (slot.refreshed) {
            const auto held_age = now_ms > slot.refreshed_ms ? now_ms - slot.refreshed_ms : 0;
            if (entry.age_ms > held_age) {
                ++counters_.gossip_stale;
                continue;
            }
        }
        // The accepted entry is the freshest we have seen, so its address wins; `hops` keeps the
        // nearest distance anyone has claimed, which is what makes "nearer" meaningful (D4).
        slot.peer = GossipPeer{.id = entry.id,
                               .pub_endpoint = entry.pub_endpoint,
                               .router_endpoint = entry.router_endpoint};
        slot.hops = std::min(slot.hops == 0 ? hops : slot.hops, hops);
        if (slot.refreshed) {
            ++counters_.peers_refreshed;
        } else {
            ++counters_.peers_learned;
        }
        slot.refreshed = true;
        slot.refreshed_ms = now_ms;
        slot.refreshed_seq = ++gossip_seq_;
        slot.offered = false;  // a fresh sighting is worth handing to the dialler again

        // Bound the table the way a full buffer must be bounded: a peer list is a hint, and memory
        // is not a place to be generous with hints (D5).
        while (gossip_.size() > config_.gossip_table_limit) {
            // Evict the oldest entry whose peer we are not actually talking to. A flood of legal-
            // looking entries must not be able to push a connected peer out of the table, which is
            // the difference between churn and an eclipse.
            const auto connected = transport_.peers();
            auto oldest = gossip_.end();
            for (auto it = gossip_.begin(); it != gossip_.end(); ++it) {
                if (it->first == entry.id) {
                    continue;  // the newest entry never evicts itself
                }
                if (std::ranges::find(connected, it->first) != connected.end()) {
                    continue;  // a live link is not evictable
                }
                if (oldest == gossip_.end() ||
                    it->second.refreshed_seq < oldest->second.refreshed_seq) {
                    oldest = it;
                }
            }
            if (oldest == gossip_.end()) {
                break;  // nothing evictable: keep the bound rather than the flood
            }
            gossip_.erase(oldest);
            ++counters_.gossip_overflow;
        }
    }
}

void Node::gossip_tick(uint64_t now_ms) {
    // Expiry runs every tick, not only when we gossip, so a table with nothing to send still
    // forgets what went quiet.
    for (auto it = gossip_.begin(); it != gossip_.end();) {
        if (now_ms - it->second.refreshed_ms > config_.gossip_entry_ttl_ms) {
            it = gossip_.erase(it);
            ++counters_.peers_expired;
        } else {
            ++it;
        }
    }

    if (config_.gossip_interval_ms == 0 || config_.gossip_fanout == 0) {
        return;
    }

    const auto peers = transport_.peers();
    if (peers.empty()) {
        return;
    }

    // Jitter without an RNG: the node cannot read a clock or draw randomness (conventions), and
    // every node already has a unique identity, so its identity is the phase offset. Two nodes with
    // different ids do not gossip on the same tick.
    std::uint64_t phase = 0;
    for (const char c : config_.id) {
        phase = (phase * 131) + static_cast<unsigned char>(c);
    }
    const auto spread = config_.gossip_interval_ms / 5;  // 20%
    const auto wait = config_.gossip_interval_ms - spread + (phase % (2 * spread + 1));
    if (last_gossip_ms_ != 0 && now_ms - last_gossip_ms_ < wait) {
        return;
    }
    last_gossip_ms_ = now_ms;

    // Freshest first, so a bounded message carries what is most likely still true.
    std::vector<std::pair<std::uint64_t, NodeId>> fresh;
    for (const auto& [id, entry] : gossip_) {
        if (!entry.peer.pub_endpoint.empty() && !entry.peer.router_endpoint.empty()) {
            fresh.emplace_back(entry.refreshed_seq, id);
        }
    }
    std::ranges::sort(fresh, greater<>());

    std::vector<PeerEntry> entries;
    // Ourselves first: a node is the origin of its own entry (hop 0, age 0). Without this, gossip
    // could only ever repeat what a third party said, and every entry would expire the moment its
    // introducer left (DISC-3).
    if (!config_.pub_endpoint.empty() && !config_.router_endpoint.empty()) {
        entries.push_back(PeerEntry{.id = config_.id,
                                    .pub_endpoint = config_.pub_endpoint,
                                    .router_endpoint = config_.router_endpoint,
                                    .hops = 0,
                                    .age_ms = 0});
    }
    for (const auto& [refreshed, id] : fresh) {
        if (entries.size() >= kMaxPeersEntries) {
            break;
        }
        const auto& entry = gossip_.at(id);
        PeerEntry outgoing;
        outgoing.id = id;
        outgoing.pub_endpoint = entry.peer.pub_endpoint;
        outgoing.router_endpoint = entry.peer.router_endpoint;
        outgoing.hops = entry.hops;
        // Staleness, not longevity: a fresh entry has age ~0 however long we have known the peer.
        // Dwell time is included: how long we have held it since we last heard anything, so an
        // entry cannot sit at the same age while it circulates.
        outgoing.age_ms = now_ms > entry.refreshed_ms ? now_ms - entry.refreshed_ms : 0;
        entries.push_back(std::move(outgoing));
    }
    gossip_rotate_ += static_cast<std::size_t>(!fresh.empty() ? 1 : 0);
    if (entries.empty()) {
        return;
    }

    // Rotate rather than sample at random: without an RNG the only fair choice is a cursor, and it
    // has the property that no neighbour is starved - every one is offered a round every
    // ceil(neighbours / fanout) intervals.
    const auto rounds = std::min<std::size_t>(config_.gossip_fanout, peers.size());
    for (std::size_t i = 0; i < rounds; ++i) {
        const auto& peer = peers[(gossip_cursor_ + i) % peers.size()];
        if (peer_banned(peer, now_ms)) {
            continue;
        }
        transport_.send(peer, PeersMsg{entries});
        ++counters_.peers_gossiped;
    }
    gossip_cursor_ = (gossip_cursor_ + rounds) % peers.size();
}

void Node::set_configured_peers(vector<NodeId> peers) {
    configured_peers_.clear();
    for (auto& peer : peers) {
        configured_peers_.insert(std::move(peer));
    }
}

vector<GossipPeer> Node::take_new_gossip_peers() {
    const auto known = transport_.peers();
    vector<GossipPeer> out;
    for (auto& [id, entry] : gossip_) {
        if (entry.offered || entry.peer.pub_endpoint.empty()) {
            continue;
        }
        if (configured_peers_.contains(id)) {
            continue;  // configuration placed this peer; a hint does not move it
        }
        if (std::ranges::find(known, id) != known.end()) {
            continue;  // the transport already addresses it
        }
        entry.offered = true;
        out.push_back(entry.peer);
    }
    return out;
}

vector<GossipPeer> Node::gossip_peers() const {
    vector<GossipPeer> out;
    out.reserve(gossip_.size());
    for (const auto& [id, entry] : gossip_) {
        out.push_back(entry.peer);
    }
    return out;
}

void Node::request_heads(uint64_t now_ms) {
    for (const auto& peer : transport_.peers()) {
        if (peer_banned(peer, now_ms)) {
            continue;  // no point asking a peer whose answer we would refuse (SEC-2)
        }
        auto& last = last_heads_request_ms_[peer];
        if (last != 0 && now_ms - last < config_.control_interval_ms) {
            continue;
        }
        last = now_ms;
        ++counters_.heads_requested;
        // Ask for everything we follow: the answer is a set of heads, which is exactly what a
        // newcomer needs when the writer is not around to announce them.
        vector<string> patterns;
        patterns.reserve(patterns_.size());
        for (const auto& pattern : patterns_) {
            patterns.push_back(pattern);
        }
        if (!patterns.empty()) {
            transport_.send(peer, GetHeadsMsg{patterns});
        }
    }
}

void Node::acknowledge(uint64_t now_ms) {
    // Only logs we have actually accepted records for: a writer is not its own replica, and walking
    // the store's inventory here would be a full scan every tick.
    for (const auto& key : ingested_logs_) {
        if (writers_.contains(key)) {
            continue;
        }
        if (!following_.matches_any(key.channel)) {
            continue;
        }
        auto& last = last_ack_ms_[key];
        if (last != 0 && now_ms - last < config_.control_interval_ms) {
            continue;
        }
        const auto head = store_.head_seq(key);
        if (!head) {
            continue;
        }
        const auto source = source_peer_.find(key);
        if (source == source_peer_.end()) {
            continue;  // nothing arrived from a peer yet, so there is nobody to acknowledge to
        }
        last = now_ms;
        // Acknowledge to the peer that supplied the log. The writer *identity* is not an address.
        transport_.send(source->second, AckMsg{.key = key, .seq = *head});
        ++counters_.acks_sent;
    }
}

Seq Node::behind_by(const LogKey& key) const {
    const auto known = remote_head_.find(key);
    if (known == remote_head_.end()) {
        return 0;
    }
    const auto next = next_seq(key);
    return known->second >= next ? known->second - next + 1 : 0;
}

bool Node::authorized(const LogKey& key) {
    if (rules_ == nullptr || rules_->size() == 0) {
        return true;
    }
    if (rules_->permits(key.channel, key.writer, rotation_)) {
        return true;
    }
    ++counters_.unauthorized_records;
    const auto rule = rules_->rule_for(key.channel);
    ++unauthorized_by_pattern_[rule.value_or("<none>")];
    // Stop asking: a forbidden log stays forbidden, so fetching it would only be traffic.
    unauthorized_logs_.insert(key);
    return false;
}

bool Node::admit_request(const NodeId& peer, uint64_t now_ms) {
    // A sliding window per peer: cheap, bounded, and it never disconnects anyone. Requests over
    // budget are dropped and counted, so one bursty peer looks bursty rather than partitioned.
    if (config_.max_requests_per_interval == 0) {
        return true;
    }
    auto& [window_start, used] = request_budget_[peer];
    if (used == 0 || now_ms - window_start >= config_.control_interval_ms) {
        window_start = now_ms;
        used = 0;
    }
    if (used >= config_.max_requests_per_interval) {
        ++counters_.requests_throttled;
        return false;
    }
    ++used;
    return true;
}

void Node::refresh_advert(uint64_t now_ms) {
    // Rebuilt on the hello interval rather than per tick: a capacity figure that moves every tick
    // would reorder ranking constantly for no gain (D3).
    if (advert_refreshed_ && now_ms - last_advert_ms_ < config_.head_interval_ms) {
        return;
    }
    advert_refreshed_ = true;
    last_advert_ms_ = now_ms;
    advert_patterns_.clear();
    for (const auto& pattern : following_.patterns()) {
        if (advert_patterns_.size() >= config_.max_head_patterns) {
            break;  // bounded by the same limit a head request uses
        }
        if (std::ranges::find(advert_patterns_, pattern) == advert_patterns_.end()) {
            advert_patterns_.push_back(pattern);
        }
    }
}

void Node::greet_peers(uint64_t now_ms) {
    for (const auto& peer : transport_.peers()) {
        auto& link = peer_links_[peer];
        // Sent on the head interval for the life of the link, not only until the version is known:
        // the hello also carries what we follow and how much spare work we have, and a figure that
        // is sent once and never refreshed would be worse than no figure at all (HELLO-1). It is
        // also how a lost hello recovers, and how a peer that never negotiated stops looking like a
        // peer with nothing to say.
        if (link.hello_sent && now_ms - link.last_hello_ms < config_.head_interval_ms) {
            continue;
        }
        transport_.send(peer, HelloMsg{.version = kProtocolVersion,
                                       .patterns = advert_patterns_,
                                       .spare_capacity = spare_capacity_});
        link.hello_sent = true;
        link.last_hello_ms = now_ms;
    }
}

void Node::handle_hello(const NodeId& from, const HelloMsg& hello, uint64_t now_ms) {
    auto& link = peer_links_[from];
    link.version_known = true;
    link.version = hello.version;
    // A peer speaking a version we cannot decode cannot be asked to speak ours; its payload is
    // refused from here on and counted, which is what makes the mismatch visible instead of
    // mysterious.
    if (hello.version != kProtocolVersion) {
        ++counters_.version_rejects;
        ++counters_.hello_version_mismatch;
        penalize(from, PeerFault::VersionMismatch, now_ms);
    }

    // Assigned, never merged: a peer that unfollows has to be able to say so. Over the limit is
    // truncated and counted rather than refused, because a hostile or buggy follow set should not
    // hide the rest of a perfectly good hello (D3).
    link.patterns.clear();
    for (const auto& pattern : hello.patterns) {
        if (link.patterns.size() >= config_.max_head_patterns) {
            ++counters_.patterns_truncated;
            break;
        }
        if (std::ranges::find(link.patterns, pattern) == link.patterns.end()) {
            link.patterns.push_back(pattern);
        }
    }
    link.spare_capacity = hello.spare_capacity;
    link.advertised_ms = now_ms;
    link.advertised = true;
}

bool Node::advert_stale(const PeerLink& link, uint64_t now_ms) const {
    if (!link.advertised) {
        return true;
    }
    // Several hello intervals, so one lost hello is not a disappearance.
    const auto limit = static_cast<uint64_t>(config_.head_interval_ms) * kHelloStaleIntervals;
    return now_ms > link.advertised_ms && now_ms - link.advertised_ms > limit;
}

vector<string> Node::peer_patterns(const NodeId& peer) const {
    const auto it = peer_links_.find(peer);
    if (it == peer_links_.end() || advert_stale(it->second, last_now_ms_)) {
        return {};
    }
    return it->second.patterns;
}

vector<NodeId> Node::peers_following(const Channel& channel) const {
    vector<NodeId> out;
    for (const auto& [id, link] : peer_links_) {
        if (advert_stale(link, last_now_ms_)) {
            continue;
        }
        Matcher advertised;
        bool usable = false;
        for (const auto& pattern : link.patterns) {
            usable = advertised.add(pattern) || usable;
        }
        if (usable && advertised.matches_any(channel)) {
            out.push_back(id);
        }
    }
    return out;
}

optional<uint8_t> Node::peer_capacity(const NodeId& peer) {
    const auto it = peer_links_.find(peer);
    if (it == peer_links_.end() || advert_stale(it->second, last_now_ms_)) {
        ++counters_.capacity_unknown;  // an operator can see how often ranking had to guess
        return nullopt;
    }
    return it->second.spare_capacity;
}

bool Node::admit(const NodeId& from, const Message& message, uint64_t now_ms) {
    const auto it = peer_links_.find(from);
    if (it == peer_links_.end() || it->second.score.banned_until_ms == 0) {
        return true;
    }
    auto& link = it->second;
    if (now_ms >= link.score.banned_until_ms) {
        lift_ban(from, link, BanEdge::Expired, now_ms);
        return true;  // the message that reveals the lapse is served: nothing is lost to timing
    }
    // While the ban holds, the only traffic let through is the exchange that can end it early: a
    // hello speaking our version arms one record's worth of probation, and that record decides.
    // Anything else costs a map lookup and a counter, not a hash and a parse (D4 in the design).
    if (std::holds_alternative<HelloMsg>(message)) {
        if (std::get<HelloMsg>(message).version == kProtocolVersion) {
            link.score.probation = true;
        }
        return true;
    }
    if (link.score.probation && (std::holds_alternative<RecordMsg>(message) ||
                                 std::holds_alternative<DirectRecordMsg>(message))) {
        return true;
    }
    ++counters_.banned_drops;
    return false;
}

void Node::penalize(const NodeId& peer, PeerFault fault, uint64_t now_ms) {
    auto& link = peer_links_[peer];
    auto& score = link.score;
    score.strikes = decayed(score.strikes, score.decay_mark_ms, now_ms, config_.strike_decay_ms);
    score.decay_mark_ms = now_ms;
    score.strikes += strike_weight(fault);
    score.probation = false;  // a fault during probation ends the benefit of the doubt
    ++counters_.strikes_by_kind[fault];
    if (config_.strike_threshold != 0 && score.strikes >= config_.strike_threshold) {
        start_ban(peer, link, now_ms);
    }
}

void Node::confirmed(const NodeId& peer, uint64_t now_ms) {
    const auto it = peer_links_.find(peer);
    if (it == peer_links_.end() || !it->second.score.probation) {
        return;
    }
    lift_ban(peer, it->second, BanEdge::Lifted, now_ms);
}

void Node::start_ban(const NodeId& peer, PeerLink& link, uint64_t now_ms) {
    auto& score = link.score;
    const auto strikes = score.strikes;
    // A peer that faults again *while* banned (during probation) re-bans at a longer duration. It
    // must not be counted as a second currently-banned peer: the gauge counts peers, not edges.
    const bool was_banned = score.banned_until_ms != 0;
    ++score.ban_count;
    // Doubling keeps a persistent attacker cheap to hold off and a flapping peer recoverable
    // (D3). The shift is clamped so a long-running node cannot overflow its way to a short ban.
    const auto shift = std::min<std::uint32_t>(score.ban_count - 1, 30);
    auto duration = static_cast<uint64_t>(config_.ban_base_ms) << shift;
    duration = std::min<uint64_t>(duration, config_.ban_max_ms);
    score.banned_until_ms = now_ms + duration;
    // The ban is the escalation from here on: the peer starts clean inside it, so a ban is served
    // once rather than re-triggered by the strikes that caused it.
    score.strikes = 0;
    score.decay_mark_ms = now_ms;
    score.probation = false;
    ++counters_.bans_started;
    if (!was_banned) {
        ++counters_.peer_bans;
    }
    // Tell the transport, so a real link can drop the frames without decoding them. Optional: a
    // transport that ignores this is still correct, only less cheap.
    transport_.set_peer_ignored(peer, true);
    notify_ban(BanEvent{.peer = peer,
                        .edge = BanEdge::Started,
                        .ban_count = score.ban_count,
                        .strikes = strikes,
                        .duration_ms = duration});
}

void Node::lift_ban(const NodeId& peer, PeerLink& link, BanEdge edge, uint64_t now_ms) {
    auto& score = link.score;
    const bool was_banned = score.banned_until_ms != 0;
    score.banned_until_ms = 0;
    score.probation = false;
    score.strikes = 0;
    score.decay_mark_ms = now_ms;
    if (!was_banned) {
        return;
    }
    if (counters_.peer_bans != 0) {
        --counters_.peer_bans;
    }
    transport_.set_peer_ignored(peer, false);
    // `ban_count` is deliberately kept: escalation is a property of the peer, not of this one ban.
    notify_ban(BanEvent{
        .peer = peer, .edge = edge, .ban_count = score.ban_count, .strikes = 0, .duration_ms = 0});
}

void Node::expire_bans(uint64_t now_ms) {
    for (auto& [peer, link] : peer_links_) {
        if (link.score.banned_until_ms != 0 && now_ms >= link.score.banned_until_ms) {
            lift_ban(peer, link, BanEdge::Expired, now_ms);
        }
    }
}

void Node::notify_ban(const BanEvent& event) const {
    if (on_ban_) {
        on_ban_(event);
    }
}

uint32_t Node::peer_strikes(const NodeId& peer, uint64_t now_ms) const {
    const auto it = peer_links_.find(peer);
    if (it == peer_links_.end()) {
        return 0;
    }
    return decayed(it->second.score.strikes, it->second.score.decay_mark_ms, now_ms,
                   config_.strike_decay_ms);
}

bool Node::peer_banned(const NodeId& peer, uint64_t now_ms) const {
    const auto it = peer_links_.find(peer);
    return it != peer_links_.end() && it->second.score.banned_until_ms > now_ms;
}

bool Node::accept_payload(const NodeId& peer) {
    const auto it = peer_links_.find(peer);
    if (it == peer_links_.end() || !it->second.version_known) {
        // The hello may simply be late or lost. Records are self-verifying, so accepting and
        // counting is better than stalling replication to enforce metadata
        // (docs/design/02-protocol.md).
        ++counters_.unnegotiated;
        return true;
    }
    if (it->second.version != kProtocolVersion) {
        ++counters_.version_rejects;
        return false;
    }
    return true;
}

bool Node::unfollow(const string& pattern) {
    // Remove it from our own matcher: everything below (storage, the app callback, repair) is gated
    // on that matcher, so this is the whole behavioural change. The transport subscription remains,
    // because the contract has no unsubscribe; a peer may keep sending and we drop it on arrival.
    if (!following_.remove_pattern(pattern)) {
        return false;
    }
    std::erase(patterns_, pattern);  // C++20: erase-remove in one call
    return true;
}

Seq Node::next_seq(const LogKey& key) const {
    const auto head = store_.head_seq(key);
    return head ? *head + 1 : 0;
}

CreditWindow& Node::credits_for(const NodeId& peer) {
    return peer_credits_.try_emplace(peer, config_.fetch_window, config_.fetch_window)
        .first->second;
}

}  // namespace mosaic

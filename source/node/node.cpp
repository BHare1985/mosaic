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
    if (strikes == 0 || window_ms == 0 || now_ms <= mark_ms)
        return strikes;
    const auto elapsed = now_ms - mark_ms;
    const auto reduce = static_cast<uint64_t>(strikes) * elapsed / window_ms;
    if (reduce >= strikes)
        return 0;
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
        case PeerFault::Undecodable:
            return 1;  // usually a bug or a version skew, and cheap to check
        case PeerFault::Unverified:
            return 3;  // a broken writer or an attacker: either way it must stop quickly
        case PeerFault::Oversized:
            return 2;
        case PeerFault::Unrequested:
            return 2;  // volunteering repair records is never legitimate traffic
        case PeerFault::VersionMismatch:
            return 1;  // usually a deployment mistake, not an attack
    }
    return 1;
}

Node::Node(NodeConfig config, Transport& transport, LogStore& store, const Hasher& hasher,
           const Verifier& verifier)
    : config_(move(config)), transport_(transport), store_(store), hasher_(hasher),
      replica_(store, hasher, verifier) {}

bool Node::follow(const string& pattern) {
    if (!following_.add(pattern))
        return false;
    patterns_.push_back(pattern);
    transport_.subscribe(pattern);
    return true;
}

size_t Node::publish_batch(const Signer& signer, const Channel& channel, const string& content_type,
                           const vector<Bytes>& payloads, uint64_t now_ms) {
    size_t written = 0;
    for (const auto& payload : payloads) {
        if (!publish(signer, channel, content_type, payload, now_ms))
            break;  // refused (oversized or not ours to write): stop rather than silently skip
        ++written;
    }
    return written;
}

optional<Record> Node::publish(const Signer& signer, const Channel& channel, string content_type,
                               Bytes payload, uint64_t now_ms) {
    const LogKey key{signer.id(), channel};
    if (payload.size() > config_.max_record_bytes) {
        ++counters_.oversized_records;
        return nullopt;  // a writer must not create history its own peers would refuse
    }
    if (!authorized(key))
        return nullopt;
    auto it = writers_.find(key);
    if (it == writers_.end()) {
        WriterSlot slot{make_unique<LogWriter>(store_, hasher_, signer, channel), now_ms};
        it = writers_.emplace(key, move(slot)).first;
    }
    Record record = it->second.log->append(move(content_type), move(payload), now_ms);
    transport_.publish(channel, RecordMsg{record});
    return record;
}

void Node::pump(uint64_t now_ms) {
    while (auto envelope = transport_.poll())
        handle(envelope->from, envelope->message, now_ms);
    // A frame that did not decode never becomes a Message, so it is the one fault the choke point
    // cannot see. The transport reports it by sender instead of failing silently (SEC-2).
    for (const auto& peer : transport_.take_decode_faults())
        penalize(peer, PeerFault::Undecodable, now_ms);
}

void Node::tick(uint64_t now_ms) {
    // 0. Bans that have lapsed stop being bans on their own: nothing here needs an operator, and
    // the lapse is reported so a silence is never mystifying (SEC-2).
    expire_bans(now_ms);

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
        if (now_ms - slot.last_head_ms < config_.head_interval_ms)
            continue;
        if (auto head = slot.log->head())
            transport_.publish(key.channel, HeadMsg{key, head->seq, head->hash});
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
    for (const auto& [key, peer] : retry)
        request_fetch(key, peer, now_ms);
}

void Node::handle(const NodeId& from, const Message& message, uint64_t now_ms) {
    if (!admit(from, message, now_ms))
        return;  // banned: dropped here, before any decoding, verification or store access
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

void Node::handle_record(const NodeId& from, const Record& record, bool direct, uint64_t now_ms) {
    if (!accept_payload(from))
        return;
    if (!direct && live_credit_enabled_[from]) {
        // Credit was granted on this link, so live delivery is paced; a record we cannot afford is
        // lost and repaired, never queued (FLOW-3).
        if (live_credits_[from].take_up_to(1) == 0) {
            ++counters_.credit_drops;
            return;
        }
    }
    if (!following_.matches_any(record.channel))
        return;

    if (record.payload.size() > config_.max_record_bytes) {
        ++counters_.oversized_records;
        penalize(from, PeerFault::Oversized, now_ms);
        return;  // refused before the store, the callback or the chain
    }
    if (!authorized(record.key()))
        return;  // a channel rule does not permit this writer: never stored, never delivered

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
        if (!requested && !ever_requested)
            penalize(from, PeerFault::Unrequested, now_ms);
    } else if (record.seq > next_seq(key) + config_.slow_follower_threshold) {
        // Too far behind to keep up with a firehose: stop trusting live delivery for this log and
        // let the repair path do the work, which bounds our backlog by our own request size
        // (FLOW-4). The comparison is against this record's own sequence: the head we know is
        // updated below.
        ++counters_.slow_follower_switches;
        request_fetch(key, from, now_ms);
        return;
    }

    auto& known = remote_head_[key];
    known = max(known, record.seq);
    if (source_peer_.find(key) == source_peer_.end())
        source_peer_[key] = from;

    const auto result = replica_.ingest(record);
    switch (result) {
        case IngestResult::Accepted:
            ++counters_.accepted;
            ingested_logs_.insert(key);
            if (on_record_)
                on_record_(record);
            if (direct && next_seq(key) <= known)
                request_fetch(key, from, now_ms);  // more to catch up on
            break;
        case IngestResult::Gap:
            ++counters_.gap;
            request_fetch(key, from, now_ms);
            break;
        case IngestResult::Fork:
            ++counters_.fork;
            if (on_fork_)
                on_fork_(record);
            break;
        case IngestResult::Duplicate:
            ++counters_.duplicate;
            break;
        case IngestResult::BadChain:
        case IngestResult::BadSignature:
            // The two are one fault kind on purpose: to a node that has to decide how much
            // attention a peer deserves, "cannot verify" is the whole of the information.
            if (result == IngestResult::BadChain)
                ++counters_.bad_chain;
            else
                ++counters_.bad_signature;
            penalize(from, PeerFault::Unverified, now_ms);
            return;
    }
    // Every outcome but the two above proves the peer can sign: that is the "verifiable record"
    // half of an early lift.
    confirmed(from, now_ms);
}

void Node::handle_head(const NodeId& from, const HeadMsg& head, uint64_t now_ms) {
    if (!accept_payload(from))
        return;
    if (unauthorized_logs_.count(head.key) != 0)
        return;  // a rule refuses this log; its heads are not worth acting on
    if (!following_.matches_any(head.key.channel))
        return;
    auto& known = remote_head_[head.key];
    known = max(known, head.seq);
    if (next_seq(head.key) <= head.seq)
        request_fetch(head.key, from, now_ms);
}

void Node::handle_fetch(const NodeId& from, const FetchMsg& fetch, uint64_t now_ms) {
    if (!accept_payload(from))
        return;
    if (!admit_request(from, now_ms))
        return;
    const auto count = min(fetch.count, config_.max_serve);
    for (auto& record : store_.read(fetch.key, fetch.from, count))
        transport_.send(from, DirectRecordMsg{move(record)});
}

void Node::request_fetch(const LogKey& key, const NodeId& peer, uint64_t now_ms) {
    if (unauthorized_logs_.count(key) != 0)
        return;  // never fetch a log a rule forbids
    const Seq from = next_seq(key);
    const auto known = remote_head_.find(key);
    if (known == remote_head_.end() || known->second < from)
        return;  // nothing missing as far as we know

    const auto pending = fetches_.find({key, peer});
    if (pending != fetches_.end() && from <= pending->second.end_seq &&
        now_ms - pending->second.sent_ms < config_.fetch_timeout_ms)
        return;  // a request for this range is already in flight

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

    transport_.send(peer, FetchMsg{key, from, granted});
    counters_.credit_spent += granted;
    fetches_[{key, peer}] = FetchState{now_ms, from, from + granted - 1, granted};
    ++counters_.fetches_sent;
    auto& through = requested_through_[{key, peer}];
    through = max(through, from + granted - 1);
}

bool Node::consume_credit(const NodeId& peer, const LogKey& key, Seq seq) {
    const auto it = fetches_.find({key, peer});
    if (it == fetches_.end() || seq < it->second.from || seq > it->second.end_seq ||
        it->second.credit_left == 0)
        return false;  // late or unsolicited: credit already returned, so it must not create any
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
        if (!head)
            continue;
        const auto released = released_through_.find(key);
        const Seq floor = released == released_through_.end() ? 0 : released->second;
        if (*head > floor)
            total += static_cast<uint32_t>(*head - floor);
    }
    return total;
}

vector<LogKey> Node::retention_shortfalls() const {
    vector<LogKey> out;
    for (const auto& [key, at] : shortfall_at_)
        out.push_back(key);
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
    if (config_.min_replicas == 0)
        return;
    const auto head = store_.head_seq(key);
    if (!head)
        return;

    // The floor is the highest sequence that at least min_replicas replicas have confirmed - which
    // is just the min_replicas-th largest acknowledgement. Collecting and selecting is O(r log r)
    // in the number of replicas; the previous version walked every sequence in the range and
    // counted acks per sequence.
    Seq candidate = released_through_.count(key) ? released_through_[key] : 0;
    vector<Seq> acks;
    for (const auto& [entry, acked] : acked_)
        if (entry.first == key)
            acks.push_back(acked);
    if (acks.size() >= config_.min_replicas && config_.min_replicas > 0) {
        sort(acks.begin(), acks.end(), greater<Seq>());
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
    if (!admit_request(from, now_ms))
        return;
    Matcher patterns;
    for (const auto& pattern : request.patterns)
        if (!patterns.add(pattern))
            return;  // an invalid pattern is a caller error: answer nothing rather than guess

    ++counters_.heads_answered;
    uint32_t answered = 0;
    for (const auto& key : store_.keys()) {
        if (answered >= config_.max_serve)
            break;
        if (!patterns.matches_any(key.channel))
            continue;
        const auto head = store_.head_seq(key);
        if (!head)
            continue;
        const auto last = store_.get(key, *head);
        if (!last)
            continue;
        const auto hash = record_hash_tagged(*last);
        if (!hash)
            continue;
        transport_.send(from, DirectHeadMsg{key, *head, *hash});
        ++answered;
    }
}

void Node::handle_direct_head(const NodeId& from, const DirectHeadMsg& head, uint64_t now_ms) {
    if (!following_.matches_any(head.key.channel))
        return;
    auto& known = remote_head_[head.key];
    known = max(known, head.seq);
    if (next_seq(head.key) <= head.seq)
        request_fetch(head.key, from, now_ms);
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
    if (!admit_request(from, now_ms))
        return;
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
        if (configured_peers_.count(entry.id) != 0) {
            ++counters_.peers_refreshed;
            continue;
        }

        auto& slot = gossip_[entry.id];
        slot.peer = GossipPeer{entry.id, entry.pub_endpoint, entry.router_endpoint};
        // Nearer wins: an entry one hop from its origin describes a peer better than one six hops
        // away that may be quoting a stale address, and hops is a distance for exactly this (D4).
        slot.hops = std::min(slot.hops == 0 ? hops : slot.hops, hops);
        if (slot.refreshed_ms == 0) {
            ++counters_.peers_learned;
        } else {
            ++counters_.peers_refreshed;
        }
        slot.refreshed_ms = now_ms;
        slot.refreshed_seq = ++gossip_seq_;
        slot.offered = false;  // a fresh sighting is worth handing to the dialler again

        // Bound the table the way a full buffer must be bounded: a peer list is a hint, and memory
        // is not a place to be generous with hints (D5).
        while (gossip_.size() > config_.gossip_table_limit) {
            auto oldest = gossip_.begin();
            for (auto it = gossip_.begin(); it != gossip_.end(); ++it)
                if (it->second.refreshed_seq < oldest->second.refreshed_seq)
                    oldest = it;
            if (oldest->first == entry.id)
                break;  // the newest entry never evicts itself
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

    if (config_.gossip_interval_ms == 0 || config_.gossip_fanout == 0)
        return;

    const auto peers = transport_.peers();
    if (peers.empty())
        return;

    // Jitter without an RNG: the node cannot read a clock or draw randomness (conventions), and
    // every node already has a unique identity, so its identity is the phase offset. Two nodes with
    // different ids do not gossip on the same tick.
    std::uint64_t phase = 0;
    for (const char c : config_.id)
        phase = phase * 131 + static_cast<unsigned char>(c);
    const auto spread = config_.gossip_interval_ms / 5;  // 20%
    const auto wait = config_.gossip_interval_ms - spread + (phase % (2 * spread + 1));
    if (last_gossip_ms_ != 0 && now_ms - last_gossip_ms_ < wait)
        return;
    last_gossip_ms_ = now_ms;

    // Freshest first, so a bounded message carries what is most likely still true.
    std::vector<std::pair<std::uint64_t, NodeId>> fresh;
    for (const auto& [id, entry] : gossip_)
        if (!entry.peer.pub_endpoint.empty() && !entry.peer.router_endpoint.empty())
            fresh.emplace_back(entry.refreshed_seq, id);
    sort(fresh.begin(), fresh.end(), greater<>());

    std::vector<PeerEntry> entries;
    // Ourselves first: a node is the origin of its own entry (hop 0, age 0). Without this, gossip
    // could only ever repeat what a third party said, and every entry would expire the moment its
    // introducer left (DISC-3).
    if (!config_.pub_endpoint.empty() && !config_.router_endpoint.empty()) {
        entries.push_back(
            PeerEntry{config_.id, config_.pub_endpoint, config_.router_endpoint, 0, 0});
    }
    for (const auto& [refreshed, id] : fresh) {
        if (entries.size() >= kMaxPeersEntries)
            break;
        const auto& entry = gossip_.at(id);
        PeerEntry outgoing;
        outgoing.id = id;
        outgoing.pub_endpoint = entry.peer.pub_endpoint;
        outgoing.router_endpoint = entry.peer.router_endpoint;
        outgoing.hops = entry.hops;
        // Staleness, not longevity: a fresh entry has age ~0 however long we have known the peer.
        outgoing.age_ms = now_ms > entry.refreshed_ms ? now_ms - entry.refreshed_ms : 0;
        entries.push_back(move(outgoing));
    }
    if (entries.empty())
        return;

    // Rotate rather than sample at random: without an RNG the only fair choice is a cursor, and it
    // has the property that no neighbour is starved - every one is offered a round every
    // ceil(neighbours / fanout) intervals.
    const auto rounds = std::min<std::size_t>(config_.gossip_fanout, peers.size());
    for (std::size_t i = 0; i < rounds; ++i) {
        const auto& peer = peers[(gossip_cursor_ + i) % peers.size()];
        if (peer_banned(peer, now_ms))
            continue;
        transport_.send(peer, PeersMsg{entries});
        ++counters_.peers_gossiped;
    }
    gossip_cursor_ = (gossip_cursor_ + rounds) % peers.size();
}

void Node::set_configured_peers(vector<NodeId> peers) {
    configured_peers_.clear();
    for (auto& peer : peers)
        configured_peers_.insert(move(peer));
}

vector<GossipPeer> Node::take_new_gossip_peers() {
    const auto known = transport_.peers();
    vector<GossipPeer> out;
    for (auto& [id, entry] : gossip_) {
        if (entry.offered || entry.peer.pub_endpoint.empty())
            continue;
        if (configured_peers_.count(id) != 0)
            continue;  // configuration placed this peer; a hint does not move it
        if (find(known.begin(), known.end(), id) != known.end())
            continue;  // the transport already addresses it
        entry.offered = true;
        out.push_back(entry.peer);
    }
    return out;
}

vector<GossipPeer> Node::gossip_peers() const {
    vector<GossipPeer> out;
    out.reserve(gossip_.size());
    for (const auto& [id, entry] : gossip_)
        out.push_back(entry.peer);
    return out;
}

void Node::request_heads(uint64_t now_ms) {
    for (const auto& peer : transport_.peers()) {
        if (peer_banned(peer, now_ms))
            continue;  // no point asking a peer whose answer we would refuse (SEC-2)
        auto& last = last_heads_request_ms_[peer];
        if (last != 0 && now_ms - last < config_.control_interval_ms)
            continue;
        last = now_ms;
        ++counters_.heads_requested;
        // Ask for everything we follow: the answer is a set of heads, which is exactly what a
        // newcomer needs when the writer is not around to announce them.
        vector<string> patterns;
        for (const auto& pattern : patterns_)
            patterns.push_back(pattern);
        if (!patterns.empty())
            transport_.send(peer, GetHeadsMsg{patterns});
    }
}

void Node::acknowledge(uint64_t now_ms) {
    // Only logs we have actually accepted records for: a writer is not its own replica, and walking
    // the store's inventory here would be a full scan every tick.
    for (const auto& key : ingested_logs_) {
        if (writers_.count(key))
            continue;
        if (!following_.matches_any(key.channel))
            continue;
        auto& last = last_ack_ms_[key];
        if (last != 0 && now_ms - last < config_.control_interval_ms)
            continue;
        const auto head = store_.head_seq(key);
        if (!head)
            continue;
        const auto source = source_peer_.find(key);
        if (source == source_peer_.end())
            continue;  // nothing arrived from a peer yet, so there is nobody to acknowledge to
        last = now_ms;
        // Acknowledge to the peer that supplied the log. The writer *identity* is not an address.
        transport_.send(source->second, AckMsg{key, *head});
        ++counters_.acks_sent;
    }
}

Seq Node::behind_by(const LogKey& key) const {
    const auto known = remote_head_.find(key);
    if (known == remote_head_.end())
        return 0;
    const auto next = next_seq(key);
    return known->second >= next ? known->second - next + 1 : 0;
}

bool Node::authorized(const LogKey& key) {
    if (rules_ == nullptr || rules_->size() == 0)
        return true;
    if (rules_->permits(key.channel, key.writer))
        return true;
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
    if (config_.max_requests_per_interval == 0)
        return true;
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

void Node::greet_peers(uint64_t now_ms) {
    for (const auto& peer : transport_.peers()) {
        auto& link = peer_links_[peer];
        if (link.version_known)
            continue;
        // Retry on the head interval: a hello can be lost like anything else, and a link that never
        // negotiates must not look like a peer that simply has nothing to say.
        if (link.hello_sent && now_ms - link.last_hello_ms < config_.head_interval_ms)
            continue;
        transport_.send(peer, HelloMsg{kProtocolVersion, {}});
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
        penalize(from, PeerFault::VersionMismatch, now_ms);
    }
}

bool Node::admit(const NodeId& from, const Message& message, uint64_t now_ms) {
    const auto it = peer_links_.find(from);
    if (it == peer_links_.end() || it->second.score.banned_until_ms == 0)
        return true;
    auto& link = it->second;
    if (now_ms >= link.score.banned_until_ms) {
        lift_ban(from, link, BanEdge::Expired, now_ms);
        return true;  // the message that reveals the lapse is served: nothing is lost to timing
    }
    // While the ban holds, the only traffic let through is the exchange that can end it early: a
    // hello speaking our version arms one record's worth of probation, and that record decides.
    // Anything else costs a map lookup and a counter, not a hash and a parse (D4 in the design).
    if (std::holds_alternative<HelloMsg>(message)) {
        if (std::get<HelloMsg>(message).version == kProtocolVersion)
            link.score.probation = true;
        return true;
    }
    if (link.score.probation && (std::holds_alternative<RecordMsg>(message) ||
                                 std::holds_alternative<DirectRecordMsg>(message)))
        return true;
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
    if (config_.strike_threshold != 0 && score.strikes >= config_.strike_threshold)
        start_ban(peer, link, now_ms);
}

void Node::confirmed(const NodeId& peer, uint64_t now_ms) {
    const auto it = peer_links_.find(peer);
    if (it == peer_links_.end() || !it->second.score.probation)
        return;
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
    if (!was_banned)
        ++counters_.peer_bans;
    // Tell the transport, so a real link can drop the frames without decoding them. Optional: a
    // transport that ignores this is still correct, only less cheap.
    transport_.set_peer_ignored(peer, true);
    notify_ban(BanEvent{peer, BanEdge::Started, score.ban_count, strikes, duration});
}

void Node::lift_ban(const NodeId& peer, PeerLink& link, BanEdge edge, uint64_t now_ms) {
    auto& score = link.score;
    const bool was_banned = score.banned_until_ms != 0;
    score.banned_until_ms = 0;
    score.probation = false;
    score.strikes = 0;
    score.decay_mark_ms = now_ms;
    if (!was_banned)
        return;
    if (counters_.peer_bans != 0)
        --counters_.peer_bans;
    transport_.set_peer_ignored(peer, false);
    // `ban_count` is deliberately kept: escalation is a property of the peer, not of this one ban.
    notify_ban(BanEvent{peer, edge, score.ban_count, 0, 0});
}

void Node::expire_bans(uint64_t now_ms) {
    for (auto& [peer, link] : peer_links_)
        if (link.score.banned_until_ms != 0 && now_ms >= link.score.banned_until_ms)
            lift_ban(peer, link, BanEdge::Expired, now_ms);
}

void Node::notify_ban(const BanEvent& event) const {
    if (on_ban_)
        on_ban_(event);
}

uint32_t Node::peer_strikes(const NodeId& peer, uint64_t now_ms) const {
    const auto it = peer_links_.find(peer);
    if (it == peer_links_.end())
        return 0;
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
    if (!following_.remove_pattern(pattern))
        return false;
    patterns_.erase(remove(patterns_.begin(), patterns_.end(), pattern), patterns_.end());
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

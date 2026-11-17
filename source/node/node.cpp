#include <algorithm>
#include <mosaic/node/node.h>

namespace mosaic {
namespace {
    template <class... Fs>
    struct Overloaded : Fs... {
        using Fs::operator()...;
    };
    template <class... Fs>
    Overloaded(Fs...) -> Overloaded<Fs...>;
} // namespace

Node::Node(NodeConfig config, Transport& transport, LogStore& store, const Hasher& hasher, const Verifier& verifier)
    : config_(std::move(config))
    , transport_(transport)
    , store_(store)
    , hasher_(hasher)
    , replica_(store, hasher, verifier)
{
}

bool Node::follow(const std::string& pattern)
{
    if (!following_.add(pattern))
        return false;
    patterns_.push_back(pattern);
    transport_.subscribe(pattern);
    return true;
}

std::optional<Record> Node::publish(const Signer& signer, const Channel& channel, std::string content_type, Bytes payload,
    std::uint64_t now_ms)
{
    if (payload.size() > config_.max_record_bytes) {
        ++counters_.oversized_records;
        return std::nullopt; // a writer must not create history its own peers would refuse
    }
    const LogKey key { signer.id(), channel };
    auto it = writers_.find(key);
    if (it == writers_.end()) {
        WriterSlot slot { std::make_unique<LogWriter>(store_, hasher_, signer, channel), now_ms };
        it = writers_.emplace(key, std::move(slot)).first;
    }
    Record record = it->second.log->append(std::move(content_type), std::move(payload), now_ms);
    transport_.publish(channel, RecordMsg { record });
    return record;
}

void Node::pump(std::uint64_t now_ms)
{
    while (auto envelope = transport_.poll())
        handle(envelope->from, envelope->message, now_ms);
}

void Node::tick(std::uint64_t now_ms)
{
    // 0. Version handshake: every peer we can address hears our version until it answers with its own.
    greet_peers(now_ms);

    // 0b. Control-plane work: find logs we are missing, and tell writers what we hold.
    request_heads(now_ms);
    acknowledge(now_ms);

    // 1. HEAD heartbeats for logs we write.
    for (auto& [key, slot] : writers_) {
        if (now_ms - slot.last_head_ms < config_.head_interval_ms)
            continue;
        if (auto head = slot.log->head())
            transport_.publish(key.channel, HeadMsg { key, head->seq, head->hash });
        slot.last_head_ms = now_ms;
    }

    // 2. FETCH housekeeping: forget finished requests, retry timed-out ones.
    // Credit is returned per request, and only once: a request that is abandoned returns what it owed
    // right here, so responses that arrive afterwards find no live request and cannot create credit
    // (FLOW-2). Resetting the peer's whole window instead would let one slow round inflate the next.
    std::vector<std::pair<LogKey, NodeId>> retry;
    for (auto it = fetches_.begin(); it != fetches_.end();) {
        const auto& [key_peer, state] = *it;
        if (next_seq(key_peer.first) > state.end_seq) {
            return_credit(key_peer.second, state.credit_left); // complete
            it = fetches_.erase(it);
        } else if (now_ms - state.sent_ms >= config_.fetch_timeout_ms) {
            return_credit(key_peer.second, state.credit_left); // abandoned; may still answer later
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

void Node::handle(const NodeId& from, const Message& message, std::uint64_t now_ms)
{
    std::visit(Overloaded {
                   [&](const RecordMsg& m) { handle_record(from, m.record, false, now_ms); },
                   [&](const DirectRecordMsg& m) { handle_record(from, m.record, true, now_ms); },
                   [&](const HeadMsg& m) { handle_head(from, m, now_ms); },
                   [&](const FetchMsg& m) { handle_fetch(from, m, now_ms); },
                   [&](const HelloMsg& m) { handle_hello(from, m); },
                   [&](const AckMsg& m) { handle_ack(from, m); },
                   [&](const GetHeadsMsg& m) { handle_get_heads(from, m, now_ms); },
                   [&](const DirectHeadMsg& m) { handle_direct_head(from, m, now_ms); },
                   [&](const CreditMsg& m) { handle_credit(from, m); },

               },
        message);
}

void Node::handle_record(const NodeId& from, const Record& record, bool direct, std::uint64_t now_ms)
{
    if (!accept_payload(from))
        return;
    if (!direct && live_credit_enabled_[from]) {
        // Credit was granted on this link, so live delivery is paced; a record we cannot afford is lost
        // and repaired, never queued (FLOW-3).
        if (live_credits_[from].take_up_to(1) == 0) {
            ++counters_.credit_drops;
            return;
        }
    }
    if (!following_.matches_any(record.channel))
        return;

    if (record.payload.size() > config_.max_record_bytes) {
        ++counters_.oversized_records;
        return; // refused before the store, the callback or the chain
    }

    const LogKey key = record.key();
    if (direct)
        consume_credit(from, key, record.seq);
    else if (record.seq > next_seq(key) + config_.slow_follower_threshold) {
        // Too far behind to keep up with a firehose: stop trusting live delivery for this log and let
        // the repair path do the work, which bounds our backlog by our own request size (FLOW-4).
        // The comparison is against this record's own sequence: the head we know is updated below.
        ++counters_.slow_follower_switches;
        request_fetch(key, from, now_ms);
        return;
    }

    auto& known = remote_head_[key];
    known = std::max(known, record.seq);
    if (source_peer_.find(key) == source_peer_.end())
        source_peer_[key] = from;

    switch (replica_.ingest(record)) {
    case IngestResult::Accepted:
        ++counters_.accepted;
        if (on_record_)
            on_record_(record);
        if (direct && next_seq(key) <= known)
            request_fetch(key, from, now_ms); // more to catch up on
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
        ++counters_.bad_chain;
        break; // TODO(SEC-2): rate-limit peers that keep sending bad data
    case IngestResult::BadSignature:
        ++counters_.bad_signature;
        break;
    }
}

void Node::handle_head(const NodeId& from, const HeadMsg& head, std::uint64_t now_ms)
{
    if (!accept_payload(from))
        return;
    if (!following_.matches_any(head.key.channel))
        return;
    auto& known = remote_head_[head.key];
    known = std::max(known, head.seq);
    if (next_seq(head.key) <= head.seq)
        request_fetch(head.key, from, now_ms);
}

void Node::handle_fetch(const NodeId& from, const FetchMsg& fetch, std::uint64_t now_ms)
{
    if (!accept_payload(from))
        return;
    if (!admit_request(from, now_ms))
        return;
    const auto count = std::min(fetch.count, config_.max_serve);
    for (auto& record : store_.read(fetch.key, fetch.from, count))
        transport_.send(from, DirectRecordMsg { std::move(record) });
}

void Node::request_fetch(const LogKey& key, const NodeId& peer, std::uint64_t now_ms)
{
    const Seq from = next_seq(key);
    const auto known = remote_head_.find(key);
    if (known == remote_head_.end() || known->second < from)
        return; // nothing missing as far as we know

    const auto pending = fetches_.find({ key, peer });
    if (pending != fetches_.end() && from <= pending->second.end_seq && now_ms - pending->second.sent_ms < config_.fetch_timeout_ms)
        return; // a request for this range is already in flight

    const auto wanted = static_cast<std::uint32_t>(std::min<Seq>(known->second - from + 1, config_.fetch_batch));
    const auto granted = credits_for(peer).take_up_to(wanted);
    if (granted == 0) {
        ++counters_.credit_drops;
        return; // no credit: wait for responses (or the timeout) before asking again
    }

    if (pending != fetches_.end()) {
        return_credit(peer, pending->second.credit_left); // the range moved on; close the old request
        fetches_.erase(pending);
    }

    transport_.send(peer, FetchMsg { key, from, granted });
    counters_.credit_spent += granted;
    fetches_[{ key, peer }] = FetchState { now_ms, from, from + granted - 1, granted };
    ++counters_.fetches_sent;
}

void Node::consume_credit(const NodeId& peer, const LogKey& key, Seq seq)
{
    const auto it = fetches_.find({ key, peer });
    if (it == fetches_.end() || seq < it->second.from || seq > it->second.end_seq || it->second.credit_left == 0)
        return; // late or unsolicited: its credit was already returned, so it must not create any
    --it->second.credit_left;
    credits_for(peer).grant(1);
    ++counters_.credit_returned;
}

void Node::return_credit(const NodeId& peer, std::uint32_t amount)
{
    if (amount != 0) {
        credits_for(peer).grant(amount);
        counters_.credit_returned += amount;
    }
}

std::uint32_t Node::credit_available(const NodeId& peer) const
{
    const auto it = peer_credits_.find(peer);
    // Nothing has been spent for a peer we never fetched from, so the whole window is available.
    return it == peer_credits_.end() ? config_.fetch_window : it->second.available();
}

void Node::grant_live_credit(const NodeId& peer, std::uint32_t credits)
{
    live_credit_enabled_[peer] = true;
    auto& window = live_credits_.try_emplace(peer, credits, credits).first->second;
    window.grant(credits);
}

std::uint32_t Node::retained_records() const
{
    std::uint32_t total = 0;
    for (const auto& [key, slot] : writers_) {
        const auto head = store_.head_seq(key);
        if (!head)
            continue;
        const auto released = released_through_.find(key);
        const Seq floor = released == released_through_.end() ? 0 : released->second;
        if (*head > floor)
            total += static_cast<std::uint32_t>(*head - floor);
    }
    return total;
}

std::vector<LogKey> Node::retention_shortfalls() const
{
    std::vector<LogKey> out;
    for (const auto& [key, at] : shortfall_at_)
        out.push_back(key);
    return out;
}

void Node::handle_ack(const NodeId& from, const AckMsg& ack)
{
    ++counters_.acks_received;
    auto& known = acked_[{ ack.key, from }];
    // Idempotent and monotonic: a duplicate or stale acknowledgement can never regress or double count.
    known = std::max(known, ack.seq);
    advance_retention(ack.key);
}

void Node::advance_retention(const LogKey& key)
{
    if (config_.min_replicas == 0)
        return;
    const auto head = store_.head_seq(key);
    if (!head)
        return;

    // The floor moves to the highest sequence that at least min_replicas replicas have confirmed.
    Seq candidate = released_through_.count(key) ? released_through_[key] : 0;
    for (Seq seq = candidate; seq <= *head; ++seq) {
        std::uint32_t replicas = 0;
        for (const auto& [entry, acked] : acked_)
            if (entry.first == key && acked >= seq)
                ++replicas;
        if (replicas < config_.min_replicas)
            break;
        candidate = seq;
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

void Node::handle_get_heads(const NodeId& from, const GetHeadsMsg& request, std::uint64_t now_ms)
{
    if (request.patterns.size() > config_.max_head_patterns) {
        ++counters_.head_requests_refused; // refused whole: a matcher is never built for a hostile request
        return;
    }
    if (!admit_request(from, now_ms))
        return;
    Matcher patterns;
    for (const auto& pattern : request.patterns)
        if (!patterns.add(pattern))
            return; // an invalid pattern is a caller error: answer nothing rather than guess

    ++counters_.heads_answered;
    std::uint32_t answered = 0;
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
        transport_.send(from, DirectHeadMsg { key, *head, *hash });
        ++answered;
    }
}

void Node::handle_direct_head(const NodeId& from, const DirectHeadMsg& head, std::uint64_t now_ms)
{
    if (!following_.matches_any(head.key.channel))
        return;
    auto& known = remote_head_[head.key];
    known = std::max(known, head.seq);
    if (next_seq(head.key) <= head.seq)
        request_fetch(head.key, from, now_ms);
}

void Node::handle_credit(const NodeId& from, const CreditMsg& credit)
{
    live_credit_enabled_[from] = true;
    live_credits_.try_emplace(from, credit.credits * 4, credit.credits).first->second.grant(credit.credits);
}

void Node::request_heads(std::uint64_t now_ms)
{
    for (const auto& peer : transport_.peers()) {
        auto& last = last_heads_request_ms_[peer];
        if (last != 0 && now_ms - last < config_.control_interval_ms)
            continue;
        last = now_ms;
        ++counters_.heads_requested;
        // Ask for everything we follow: the answer is a set of heads, which is exactly what a newcomer
        // needs when the writer is not around to announce them.
        std::vector<std::string> patterns;
        for (const auto& pattern : patterns_)
            patterns.push_back(pattern);
        if (!patterns.empty())
            transport_.send(peer, GetHeadsMsg { patterns });
    }
}

void Node::acknowledge(std::uint64_t now_ms)
{
    // Only logs we do not write: a writer is not its own replica.
    for (const auto& key : store_.keys()) {
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
            continue; // nothing arrived from a peer yet, so there is nobody to acknowledge to
        last = now_ms;
        // Acknowledge to the peer that supplied the log. The writer *identity* is not an address.
        transport_.send(source->second, AckMsg { key, *head });
        ++counters_.acks_sent;
    }
}

Seq Node::behind_by(const LogKey& key) const
{
    const auto known = remote_head_.find(key);
    if (known == remote_head_.end())
        return 0;
    const auto next = next_seq(key);
    return known->second >= next ? known->second - next + 1 : 0;
}

bool Node::admit_request(const NodeId& peer, std::uint64_t now_ms)
{
    // A sliding window per peer: cheap, bounded, and it never disconnects anyone. Requests over budget are
    // dropped and counted, so one bursty peer looks bursty rather than partitioned.
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

void Node::greet_peers(std::uint64_t now_ms)
{
    for (const auto& peer : transport_.peers()) {
        auto& link = peer_links_[peer];
        if (link.version_known)
            continue;
        // Retry on the head interval: a hello can be lost like anything else, and a link that never
        // negotiates must not look like a peer that simply has nothing to say.
        if (link.hello_sent && now_ms - link.last_hello_ms < config_.head_interval_ms)
            continue;
        transport_.send(peer, HelloMsg { kProtocolVersion, { } });
        link.hello_sent = true;
        link.last_hello_ms = now_ms;
    }
}

void Node::handle_hello(const NodeId& from, const HelloMsg& hello)
{
    auto& link = peer_links_[from];
    link.version_known = true;
    link.version = hello.version;
    // A peer speaking a version we cannot decode cannot be asked to speak ours; its payload is refused
    // from here on and counted, which is what makes the mismatch visible instead of mysterious.
    if (hello.version != kProtocolVersion)
        ++counters_.version_rejects;
}

bool Node::accept_payload(const NodeId& peer)
{
    const auto it = peer_links_.find(peer);
    if (it == peer_links_.end() || !it->second.version_known) {
        // The hello may simply be late or lost. Records are self-verifying, so accepting and counting is
        // better than stalling replication to enforce metadata (docs/design/02-protocol.md).
        ++counters_.unnegotiated;
        return true;
    }
    if (it->second.version != kProtocolVersion) {
        ++counters_.version_rejects;
        return false;
    }
    return true;
}

Seq Node::next_seq(const LogKey& key) const
{
    const auto head = store_.head_seq(key);
    return head ? *head + 1 : 0;
}

CreditWindow& Node::credits_for(const NodeId& peer)
{
    return peer_credits_.try_emplace(peer, config_.fetch_window, config_.fetch_window).first->second;
}

} // namespace mosaic

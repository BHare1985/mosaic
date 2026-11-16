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
    transport_.subscribe(pattern);
    return true;
}

Record Node::publish(const Signer& signer, const Channel& channel, std::string content_type, Bytes payload, std::uint64_t now_ms)
{
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
                   [&](const FetchMsg& m) { handle_fetch(from, m); },
                   // TODO(ACK-1, HEADS-1, FLOW-3, HELLO-1): the remaining messages are defined but not handled yet.
                   [&](const AckMsg&) { },
                   [&](const GetHeadsMsg&) { },
                   [&](const DirectHeadMsg&) { },
                   [&](const CreditMsg&) { },
                   [&](const HelloMsg&) { },
               },
        message);
}

void Node::handle_record(const NodeId& from, const Record& record, bool direct, std::uint64_t now_ms)
{
    if (!following_.matches_any(record.channel))
        return;

    const LogKey key = record.key();
    if (direct)
        consume_credit(from, key, record.seq);

    auto& known = remote_head_[key];
    known = std::max(known, record.seq);

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
    if (!following_.matches_any(head.key.channel))
        return;
    auto& known = remote_head_[head.key];
    known = std::max(known, head.seq);
    if (next_seq(head.key) <= head.seq)
        request_fetch(head.key, from, now_ms);
}

void Node::handle_fetch(const NodeId& from, const FetchMsg& fetch)
{
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

#include <mosaic/log/log.h>

using std::move;
using std::nullopt;
using std::optional;
using std::string;
using std::uint64_t;
using std::vector;

namespace mosaic {

const char* to_string(IngestResult result) {
    switch (result) {
        case IngestResult::Accepted:
            return "accepted";
        case IngestResult::Duplicate:
            return "duplicate";
        case IngestResult::Gap:
            return "gap";
        case IngestResult::Fork:
            return "fork";
        case IngestResult::BadChain:
            return "bad-chain";
        case IngestResult::BadSignature:
            return "bad-signature";
    }
    return "?";
}

LogWriter::LogWriter(LogStore& store, const Hasher& hasher, const Signer& signer, Channel channel)
    : store_(store), hasher_(hasher), signer_(signer), key_{signer.id(), move(channel)} {}

optional<Head> LogWriter::head() const {
    auto seq = store_.head_seq(key_);
    if (!seq)
        return nullopt;
    auto last = store_.get(key_, *seq);
    return Head{*seq, record_hash(*last, hasher_)};
}

Record LogWriter::append(string content_type, Bytes payload, uint64_t timestamp_ms) {
    Record r;
    r.writer = key_.writer;
    r.channel = key_.channel;
    if (auto h = head()) {
        r.seq = h->seq + 1;
        r.prev = h->hash;
    }
    r.hash_algo = hasher_.algorithm();
    r.timestamp_ms = timestamp_ms;
    r.content_type = move(content_type);
    r.payload = move(payload);
    r.sig = signer_.sign(record_hash(r, hasher_));
    store_.append(r);  // cannot fail: seq was derived from the store's own head
    return r;
}

ReplicaLog::ReplicaLog(LogStore& store, const Hasher& hasher, const Verifier& verifier)
    : store_(store), hasher_(hasher), verifier_(verifier) {}

optional<Head> ReplicaLog::head(const LogKey& key) const {
    auto seq = store_.head_seq(key);
    if (!seq)
        return nullopt;
    return Head{*seq, record_hash(*store_.get(key, *seq), hasher_)};
}

IngestResult ReplicaLog::ingest(const Record& record) {
    // Use the algorithm the record names, so a log written under a previous default still verifies.
    // An algorithm this build cannot compute is indistinguishable from a forgery: reject it.
    const auto tagged = record_hash_tagged(record);
    if (!tagged)
        return IngestResult::BadSignature;
    const Hash hash = *tagged;
    if (!verifier_.verify(record.writer, hash, record.sig))
        return IngestResult::BadSignature;

    const LogKey key = record.key();
    const auto head_seq = store_.head_seq(key);
    const Seq next = head_seq ? *head_seq + 1 : 0;

    if (record.seq < next) {
        const auto existing = store_.get(key, record.seq);
        const auto existing_hash = existing ? record_hash_tagged(*existing) : nullopt;
        if (!existing_hash)
            return IngestResult::Fork;
        if (*existing_hash == hash)
            return IngestResult::Duplicate;
        if (existing)
            keep_evidence(*existing, record);  // same sequence, different history: keep both sides
        return IngestResult::Fork;
    }
    if (record.seq > next)
        return IngestResult::Gap;

    if (record.seq == 0) {
        if (record.prev != Hash{})
            return IngestResult::BadChain;
    } else {
        const auto previous = store_.get(key, record.seq - 1);
        const auto previous_hash = previous ? record_hash_tagged(*previous) : nullopt;
        if (!previous_hash || record.prev != *previous_hash) {
            // The writer signed a chain that disagrees with the one we hold: the evidence is the
            // incoming record plus the predecessor it contradicts.
            if (previous)
                keep_evidence(*previous, record);
            return IngestResult::Fork;
        }
    }

    store_.append(record);
    return IngestResult::Accepted;
}

vector<ForkEvidence> ReplicaLog::forks() const {
    vector<ForkEvidence> out;
    for (const auto& [key, by_seq] : evidence_)
        for (const auto& [seq, evidence] : by_seq)
            out.push_back(evidence);
    return out;
}

void ReplicaLog::keep_evidence(const Record& accepted, const Record& conflicting) {
    // Evidence is keyed by the sequence the two records disagree at. For a same-sequence fork that
    // is both records' sequence; for a chain conflict the incoming record is the one that does not
    // belong.
    ForkEvidence evidence{conflicting.key(), conflicting.seq, accepted, conflicting};
    auto& by_seq = evidence_[evidence.key];
    if (by_seq.find(evidence.seq) == by_seq.end() && by_seq.size() >= max_evidence) {
        ++evidence_overflow;  // a fork storm must not be able to grow a replica's memory without
                              // bound
        return;
    }
    by_seq[evidence.seq] = evidence;
    if (auto* persistent = store_.evidence())
        persistent->save_fork(evidence);
}

}  // namespace mosaic

#include <mosaic/log/log.h>

#include <algorithm>
#include <map>
#include <set>

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
        case IngestResult::NotAttempted:
            return "not-attempted";
    }
    return "?";
}

LogWriter::LogWriter(LogStore& store, const Hasher& hasher, const Signer& signer, Channel channel)
    : store_(store), hasher_(hasher), signer_(signer),
      key_{.writer = signer.id(), .channel = std::move(channel)} {}

optional<Head> LogWriter::head() const {
    auto seq = store_.head_seq(key_);
    if (!seq) {
        return nullopt;
    }
    auto last = store_.get(key_, *seq);
    if (!last) {
        return nullopt;  // the store claims a head it cannot serve: no head rather than a crash
    }
    return Head{.seq = *seq, .hash = record_hash(*last, hasher_)};
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
    r.content_type = std::move(content_type);
    r.payload = std::move(payload);
    r.sig = signer_.sign(record_hash(r, hasher_));
    store_.append(r);  // cannot fail: seq was derived from the store's own head
    return r;
}

ReplicaLog::ReplicaLog(LogStore& store, const Hasher& hasher, const Verifier& verifier)
    : store_(store), hasher_(hasher), verifier_(verifier) {}

optional<Head> ReplicaLog::head(const LogKey& key) const {
    auto seq = store_.head_seq(key);
    if (!seq) {
        return nullopt;
    }
    const auto last = store_.get(key, *seq);
    if (!last) {
        return nullopt;  // a head the store cannot serve is no head at all
    }
    return Head{.seq = *seq, .hash = record_hash(*last, hasher_)};
}

IngestResult ReplicaLog::ingest(const Record& record) {
    // Use the algorithm the record names, so a log written under a previous default still verifies.
    // An algorithm this build cannot compute is indistinguishable from a forgery: reject it.
    const auto tagged = record_hash_tagged(record);
    if (!tagged) {
        return IngestResult::BadSignature;
    }
    const Hash hash = *tagged;
    if (!verifier_.verify(record.writer, hash, record.sig)) {
        return IngestResult::BadSignature;
    }

    const LogKey key = record.key();
    const auto head_seq = store_.head_seq(key);
    const Seq next = head_seq ? *head_seq + 1 : 0;

    if (record.seq < next) {
        const auto existing = store_.get(key, record.seq);
        const auto existing_hash = existing ? record_hash_tagged(*existing) : nullopt;
        if (!existing_hash) {
            return IngestResult::Fork;
        }
        if (*existing_hash == hash) {
            return IngestResult::Duplicate;
        }
        if (existing) {
            keep_evidence(*existing, record);  // same sequence, different history: keep both sides
        }
        return IngestResult::Fork;
    }
    if (record.seq > next) {
        return IngestResult::Gap;
    }

    if (record.seq == 0) {
        if (record.prev != Hash{}) {
            return IngestResult::BadChain;
        }
    } else {
        const auto previous = store_.get(key, record.seq - 1);
        auto previous_hash = previous ? record_hash_tagged(*previous) : nullopt;
        // The predecessor may have been dropped by retention. If a checkpoint says what it hashed
        // to, the record can still be shown to descend from it; if nobody can say, the record is
        // refused exactly as before (STORE-3, D6).
        if (!previous_hash && origin_) {
            previous_hash = origin_(key, record.seq - 1);
        }
        if (!previous_hash || record.prev != *previous_hash) {
            // The writer signed a chain that disagrees with the one we hold: the evidence is the
            // incoming record plus the predecessor it contradicts.
            if (previous) {
                keep_evidence(*previous, record);
            }
            return IngestResult::Fork;
        }
    }

    store_.append(record);
    return IngestResult::Accepted;
}

vector<IngestResult> ReplicaLog::ingest_batch(const vector<Record>& records) {
    vector<IngestResult> outcomes(records.size(), IngestResult::NotAttempted);

    // Records for a log are taken in *sequence* order, not arrival order. A repair reply can be
    // reordered by the link - the simulation tests it - and a run that stopped at the first
    // out-of-order arrival would throw away the very records that fill the hole. Sequence order is
    // also what the chain requires, so grouping by log and sorting by sequence keeps the batch
    // equivalent to offering the same records one at a time in the order the chain defines.
    std::map<LogKey, std::vector<size_t>> by_log;
    for (size_t i = 0; i < records.size(); ++i) {
        by_log[records[i].key()].push_back(i);
    }

    for (auto& [key, indexes] : by_log) {
        std::ranges::stable_sort(
            indexes, [&](size_t a, size_t b) { return records[a].seq < records[b].seq; });

        Seq next = store_.head_seq(key) ? *store_.head_seq(key) + 1 : 0;
        std::optional<Hash> last_hash;
        if (const auto head = store_.head_seq(key)) {
            if (const auto previous = store_.get(key, *head); previous) {
                last_hash = record_hash_tagged(*previous);
            }
        }

        vector<Record> run;
        std::map<Seq, Hash> pending;  // what the run is holding, for duplicate and fork checks
        bool halted = false;          // a hole: everything above it is not attempted

        const auto flush = [&] {
            if (run.empty()) {
                return;
            }
            store_.append_batch(run);
            run.clear();
        };

        for (const auto index : indexes) {
            const Record& record = records[index];
            const auto tagged = record_hash_tagged(record);
            if (!tagged || !verifier_.verify(record.writer, *tagged, record.sig)) {
                flush();
                halted = true;
                outcomes[index] = IngestResult::BadSignature;
                continue;  // the rest of this log is not attempted
            }
            if (record.seq < next) {
                // A duplicate of something already stored, or of something this run is still
                // holding - which the store does not know about yet.
                std::optional<Hash> existing_hash;
                if (const auto at = pending.find(record.seq); at != pending.end()) {
                    existing_hash = at->second;
                } else if (const auto existing = store_.get(key, record.seq); existing) {
                    existing_hash = record_hash_tagged(*existing);
                }
                const auto existing = store_.get(key, record.seq);
                if (existing_hash && *existing_hash == *tagged) {
                    outcomes[index] = IngestResult::Duplicate;
                    continue;  // a duplicate does not stop the run
                }
                if (existing) {
                    keep_evidence(*existing, record);
                }
                flush();
                halted = true;
                outcomes[index] = IngestResult::Fork;
                continue;
            }
            if (halted) {
                continue;  // NotAttempted stays
            }
            if (record.seq > next) {
                flush();
                halted = true;
                outcomes[index] = IngestResult::Gap;
                continue;
            }
            if (record.seq == 0) {
                if (record.prev != Hash{}) {
                    flush();
                    halted = true;
                    outcomes[index] = IngestResult::BadChain;
                    continue;
                }
            } else if (!last_hash || record.prev != *last_hash) {
                if (const auto previous = store_.get(key, record.seq - 1); previous) {
                    keep_evidence(*previous, record);
                }
                flush();
                halted = true;
                outcomes[index] = IngestResult::Fork;
                continue;
            }
            outcomes[index] = IngestResult::Accepted;
            pending.emplace(record.seq, *tagged);
            run.push_back(record);
            last_hash = *tagged;
            ++next;
        }
        flush();
    }
    return outcomes;
}

vector<ForkEvidence> ReplicaLog::forks() const {
    vector<ForkEvidence> out;
    for (const auto& [key, by_seq] : evidence_) {
        for (const auto& [seq, evidence] : by_seq) {
            out.push_back(evidence);
        }
    }
    return out;
}

void ReplicaLog::keep_evidence(const Record& stored, const Record& conflicting) {
    // Evidence is keyed by the sequence the two records disagree at. For a same-sequence fork that
    // is both records' sequence; for a chain conflict the incoming record is the one that does not
    // belong.
    ForkEvidence evidence{.key = conflicting.key(),
                          .seq = conflicting.seq,
                          .accepted = stored,
                          .conflicting = conflicting};
    auto& by_seq = evidence_[evidence.key];
    if (!by_seq.contains(evidence.seq) && by_seq.size() >= max_evidence) {
        ++evidence_overflow;  // a fork storm must not be able to grow a replica's memory without
                              // bound
        return;
    }
    by_seq[evidence.seq] = evidence;
    if (auto* persistent = store_.evidence()) {
        persistent->save_fork(evidence);
    }
}

}  // namespace mosaic

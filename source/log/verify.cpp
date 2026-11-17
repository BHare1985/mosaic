#include <mosaic/log/verify.h>

#include <mosaic/core/record.h>

namespace mosaic {

VerifyReport verify_store(const LogStore& store, const Verifier& verifier)
{
    VerifyReport report;
    for (const auto& key : store.keys()) {
        ++report.logs;
        const auto head = store.head_seq(key);
        if (!head) {
            report.failure = VerifyFailure { key, 0, "log is listed but has no head" };
            return report;
        }

        Seq expected = 0;
        Hash previous { };
        for (const auto& record : store.read(key, 0, *head + 1)) {
            if (record.seq != expected) {
                report.failure = VerifyFailure { key, expected, "gap: expected sequence " + std::to_string(expected) };
                return report;
            }
            if (record.writer != key.writer || record.channel != key.channel) {
                report.failure = VerifyFailure { key, record.seq, "record belongs to another log" };
                return report;
            }

            const auto hash = record_hash_tagged(record);
            if (!hash) {
                report.failure = VerifyFailure { key, record.seq, "unknown hash algorithm" };
                return report;
            }
            if (!verifier.verify(record.writer, *hash, record.sig)) {
                report.failure = VerifyFailure { key, record.seq, "signature does not verify" };
                return report;
            }
            if (record.prev != previous) {
                report.failure = VerifyFailure { key, record.seq, "link does not match the previous record" };
                return report;
            }

            previous = *hash;
            ++expected;
            ++report.records;
        }

        if (expected <= *head) {
            // read() stopped early: the tail is unreadable, which is a failure rather than a short log.
            report.failure = VerifyFailure { key, expected, "tail is unreadable; head claims " + std::to_string(*head) };
            return report;
        }
    }
    return report;
}

} // namespace mosaic

#pragma once

// Static verification of a store (CLI-2): re-derive every hash, check every signature and link, and
// report the first thing that does not hold. This is the punchline of a tamper-evident log - the
// ability to hand someone a store and a command that says whether it is intact.
//
// Distinct from ReplicaLog::ingest: that decides whether to *accept* one arriving record; this
// walks what is already stored and asks whether it is still true.

#include <mosaic/core/crypto.h>
#include <mosaic/store/log_store.h>

#include <cstddef>
#include <optional>
#include <string>

namespace mosaic {

struct VerifyFailure {
    LogKey key;
    Seq seq = 0;
    std::string reason;
};

struct VerifyReport {
    std::size_t logs = 0;
    std::size_t records = 0;
    std::optional<VerifyFailure> failure;

    bool ok() const {
        return !failure.has_value();
    }
};

/// Walk every log in the store and verify it end to end. A gap, an unreadable record, a broken
/// link, a bad signature or a truncated tail is a failure - never a skipped record.
VerifyReport verify_store(const LogStore& store, const Verifier& verifier);

}  // namespace mosaic

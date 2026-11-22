#pragma once

// A checkpoint is a writer's signed statement about its own history: "the record at sequence S in
// this log hashes to H". It is what lets a log keep only a suffix - the records after S - without
// becoming unverifiable, because a record whose predecessor is gone can still be shown to descend
// from the checkpoint (STORE-3).
//
// Deliberately not a record in the log: putting it there would move the head whenever retention
// ran, and every follower would have to replicate a retention decision it did not make. Two forms,
// one signature: a small signed object kept beside the log, and (when it needs to travel) a record
// on the same channel carrying its bytes.
//
// Format: docs/design/01-data-model.md. Contract: openspec/specs/store/log-store.

#include <mosaic/core/crypto.h>
#include <mosaic/core/record.h>

#include <cstdint>
#include <optional>
#include <string>

namespace mosaic {

struct Checkpoint {
    LogKey key;
    Seq seq = 0;
    Hash hash{};  // the hash of the record at `seq`
    HashAlgo hash_algo = kDefaultHashAlgo;
    Bytes sig;  // over the canonical bytes, by the writer
};

/// Canonical bytes: `u8 format | u8 algorithm | string writer | string channel | u64 seq |
/// hash[32]`. The signature is not part of it, exactly as for a record.
Bytes checkpoint_canonical(const Checkpoint& checkpoint);

/// What the writer signs: the hash of the canonical bytes, under the algorithm the checkpoint
/// names.
std::optional<Hash> checkpoint_digest(const Checkpoint& checkpoint);

/// The stored and travelling form: canonical bytes plus a length-prefixed signature.
Bytes encode_checkpoint(const Checkpoint& checkpoint);

/// Total decoder: a truncated, oversized or malformed checkpoint yields nothing rather than a
/// partially believed one.
std::optional<Checkpoint> decode_checkpoint(const Bytes& bytes);

/// Sign a checkpoint in place and return it. The signer must be the log's writer.
Checkpoint sign_checkpoint(Checkpoint checkpoint, const Signer& signer);

/// Does this checkpoint's signature verify, and does it name the log and sequence it claims?
bool verify_checkpoint(const Checkpoint& checkpoint, const Verifier& verifier);

}  // namespace mosaic

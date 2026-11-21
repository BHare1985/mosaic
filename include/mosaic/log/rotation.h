#pragma once

// Following a writer's key rotation (SEC-3): given an identity a deployment already trusts, walk
// the rotation links in that identity's own log and answer "which identities are its legitimate
// successors on this channel".
//
// The mechanism is deliberately narrow. Trust is a local chain walk from a root the caller names -
// never a global registry - because a link found in someone else's log grants nothing. The walk is
// bounded, refuses repeats, and stops short of trusting anything on its own.
//
// Contract: openspec/specs/core/key-rotation.

#include <mosaic/core/crypto.h>
#include <mosaic/core/key_rotation.h>
#include <mosaic/store/log_store.h>

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace mosaic {

class RotationChain {
public:
    /// How a resolution ended. `identities` is the trusted set: the root first, then each successor
    /// a valid link named, in order.
    struct Resolution {
        std::vector<WriterId> identities;
        bool refused = false;       // a cycle, a repeated identity, or a link that does not verify
        const char* reason = "ok";  // for an operator, and for a test that wants to read it
    };

    /// `max_depth` bounds how many links are followed: an unbounded walk is a denial-of-service,
    /// and exceeding the limit fails closed.
    RotationChain(const LogStore& store, const Verifier& verifier, std::uint32_t max_depth = 4);

    /// The identities `root` may act as on `channel`, root included. Cached per (root, channel)
    /// until a rotation link is ingested; `invalidate` says when.
    const Resolution& resolve(const WriterId& root, const Channel& channel);

    /// Forget a cached resolution, because a link arrived (or the store was replayed).
    void invalidate(const WriterId& root, const Channel& channel);
    void invalidate_all();

    std::uint32_t max_depth() const {
        return max_depth_;
    }

private:
    Resolution walk(const WriterId& root, const Channel& channel) const;

    const LogStore& store_;
    const Verifier& verifier_;
    std::uint32_t max_depth_;
    std::map<std::pair<WriterId, Channel>, Resolution> cache_;
};

}  // namespace mosaic

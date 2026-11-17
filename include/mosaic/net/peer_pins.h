#pragma once

// Which key each peer is allowed to present (SEC-1). The default is open with pin-on-first-use: a
// brokerless log authenticates its *data*, so a link key never decides what is true, but a peer must
// not be able to change identity silently mid-deployment. Closed mode pins everything up front, for
// deployments whose participants are known.
//
// This deliberately knows nothing about ZeroMQ: it is a policy over (peer, key) pairs, and the
// transport is what observes keys on the wire.

#include <cstddef>
#include <map>
#include <optional>
#include <string>

namespace mosaic {

enum class PinVerdict {
    Allowed, // matches the pinned key
    AllowedAndPinned, // first sight, now pinned (open mode only)
    Mismatch, // a different key than the one pinned: impersonation or a rotated identity
    NotListed, // closed mode and this peer has no entry
};

const char* to_string(PinVerdict verdict);

class PeerPins {
public:
    enum class Mode {
        Open, // pin on first use
        Closed, // only pre-pinned peers
    };

    explicit PeerPins(Mode mode = Mode::Open)
        : mode_(mode)
    {
    }

    /// Load a pin file. A missing file is an empty store, not an error (first run).
    static std::optional<PeerPins> load(const std::string& path, Mode mode);

    /// Write the pins. False on I/O failure.
    bool save(const std::string& path) const;

    /// Pin a peer up front (closed mode configuration, or a test).
    void pin(const std::string& peer_id, const std::string& key_z85);

    /// Record what a peer presented and decide whether to accept it.
    PinVerdict observe(const std::string& peer_id, const std::string& key_z85);

    /// The decision the transport asks for at handshake time. `who` is the peer identity when the
    /// handshake exposes one (ZeroMQ's ZAP does not) and otherwise the peer address:
    ///   - open: pin on first sight of that address, refuse a later change
    ///   - closed: the presented key must be one of the pinned keys, whatever the address
    PinVerdict decide(const std::string& who, const std::string& key_z85);

    /// Closed mode: is this key allowed under any pinned identity?
    bool allows_key(const std::string& key_z85) const;

    std::optional<std::string> pinned(const std::string& peer_id) const;
    std::size_t size() const { return pins_.size(); }
    Mode mode() const { return mode_; }

private:
    Mode mode_;
    std::map<std::string, std::string> pins_;
};

} // namespace mosaic

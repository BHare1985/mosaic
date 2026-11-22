#pragma once

#include <mosaic/core/types.h>
#include <mosaic/proto/messages.h>

#include <optional>
#include <vector>

namespace mosaic {

struct Envelope {
    NodeId from;
    Message message;
};

/// Everything Node needs from a network, and nothing more. Implementations: SimNetwork (tests, in
/// this repo, deterministic) and ZmqTransport (real links, CURVE, one SUB and DEALER per peer).
/// Contract and the ZeroMQ socket mapping: docs/design/05-node-and-transport.md.
///
/// Semantics all implementations must honour:
///  - Non-blocking and single-threaded: the owner calls poll() from its own loop.
///  - Best effort. Messages may be lost, duplicated or reordered; Node copes (that is the point).
///  - publish() reaches only peers whose subscribe() pattern matches `channel` (filtering by
///    subscription is how nodes "only listen to what they follow"). Never delivers to self.
///  - send() is unicast to a peer by id. Silently dropped if the peer is unknown/unreachable.
///  - peers() is a best-effort snapshot of the peers this node can address right now. It may be
///  empty
///    before discovery and may gain entries at any time; it is not a membership agreement.
class Transport {
public:
    virtual ~Transport() = default;

    virtual NodeId self() const = 0;
    virtual void subscribe(const std::string& pattern) = 0;
    virtual void publish(const Channel& channel, Message message) = 0;
    virtual void send(const NodeId& to, Message message) = 0;
    virtual std::optional<Envelope> poll() = 0;
    /// Peers currently addressable (never includes self). Used for the version handshake and,
    /// later, for credit accounting per link.
    virtual std::vector<NodeId> peers() const = 0;

    /// Frames this transport could not decode since the previous call, each with the sender's
    /// identity when it was known. A decode failure never becomes a Message, so a transport that
    /// swallowed it silently would leave the node unable to tell a broken peer from a quiet one
    /// (SEC-2).
    ///
    /// Optional: a transport that cannot attribute a bad frame to a sender reports nothing, and
    /// the node simply scores the faults it can see.
    virtual std::vector<NodeId> take_decode_faults() {
        return {};
    }

    /// Ask this transport to stop (or resume) delivering a peer's traffic, so a ban on a real link
    /// costs a lookup rather than a decode. "Ignored" means dropped, not disconnected: the peer
    /// keeps its addresses and resumes the moment the node says so again.
    ///
    /// Optional: a transport that ignores this call is still correct, because Node drops a banned
    /// peer's messages itself - only after the work has been done rather than before it.
    virtual void set_peer_ignored(const NodeId& /*peer*/, bool /*ignored*/) {}
};

}  // namespace mosaic

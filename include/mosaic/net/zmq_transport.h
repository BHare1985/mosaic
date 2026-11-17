#pragma once

// ZeroMQ transport (ZMQ-1), an implementation of the Transport contract in net/transport.h.
//
// Socket layout B, per docs/design/05-node-and-transport.md:
//   - PUB/SUB carries live publication, first frame = channel, second = sender identity, third = message.
//   - ROUTER/DEALER carries unicast (repair), addressed by node identity; DEALER identities are set to
//     the node id, so the ROUTER side sees the peer's identity directly.
//
// The socket layer filters on the *byte prefix* of the first frame, which cannot express "exactly one
// segment". The literal prefix up to the first wildcard is therefore used only as a coarse pre-filter,
// and every arriving channel is re-checked against the real matcher here, before the node sees it.
//
// ZeroMQ deliberately does not appear in this header: it is a PIMPL, so no module can acquire a ZeroMQ
// dependency by including a Mosaic header (the confinement rule in the transport spec).

#include <cstdint>
#include <memory>
#include <mosaic/net/transport.h>
#include <optional>
#include <string>
#include <vector>

namespace mosaic {

/// One peer this node may talk to. Endpoints are ZeroMQ endpoints such as "tcp://10.0.0.2:5555".
struct ZmqPeerEndpoint {
    NodeId id;
    std::string pub_endpoint; // where that peer binds its PUB
    std::string router_endpoint; // where that peer binds its ROUTER
    std::string curve_public_z85; // that peer's CURVE public key, when CURVE is enabled
};

/// How the transport treats a peer key it has not seen before. Open is the default: a brokerless log
/// authenticates its *data*, so a link key never decides what is true (see docs/design/07-security.md).
enum class ZmqLinkPolicy {
    Open, // accept and pin any key (TOFU) - default
    Closed, // accept only keys on the allow-list
};

/// CURVE settings (SEC-1). Keys are derived from the node's Ed25519 identity
/// (`curve_identity_from_keystore`), so the key that authenticates a link is the key that signs records.
struct ZmqCurveConfig {
    bool enabled = false;
    std::string public_z85; // this node's curve25519 public key
    std::string secret_z85; // this node's curve25519 secret key
    /// Where peer keys are remembered. Open mode pins on first use; closed mode enforces what is there.
    std::string pins_path;
};

struct ZmqConfig {
    NodeId id;
    std::string pub_bind = "tcp://127.0.0.1:*"; // "*" lets tests avoid port collisions
    std::string router_bind = "tcp://127.0.0.1:*";
    std::vector<ZmqPeerEndpoint> peers;
    int send_hwm = 1000; // explicit, never inherited: overflow must be a counted drop
    int receive_hwm = 1000;
    ZmqLinkPolicy link_policy = ZmqLinkPolicy::Open;
    ZmqCurveConfig curve;
};

class ZmqTransport final : public Transport {
public:
    explicit ZmqTransport(ZmqConfig config);
    ~ZmqTransport() override;

    NodeId self() const override;
    void subscribe(const std::string& pattern) override;
    void publish(const Channel& channel, Message message) override;
    void send(const NodeId& to, Message message) override;
    std::optional<Envelope> poll() override;
    std::vector<NodeId> peers() const override;

    /// Serve pending CURVE handshake decisions (ZAP). poll() calls this first; it is exposed because a
    /// caller that is not polling yet (a test, a CLI waiting for its first message) must not stall a
    /// peer that is mid-handshake.
    void service_handshakes();

    /// Wire a peer discovered after construction (DISC-1): opens its SUB and DEALER and applies the
    /// subscriptions already registered. Idempotent for an id that is already wired.
    void add_peer(const ZmqPeerEndpoint& peer);

    /// The endpoint actually bound (resolved when the config asked for port 0).
    std::string pub_endpoint() const;
    std::string router_endpoint() const;

    /// Messages dropped because their bytes did not decode, plus live-path overflow.
    std::size_t dropped() const;

    /// Messages refused because the peer's CURVE key did not match what was pinned for it (SEC-1).
    std::size_t curve_rejects() const;

    /// Messages accepted over CURVE without an observable peer key. Non-zero means the build cannot
    /// pin peer keys at this layer (see SEC-1b: a ZAP handler is the enforcement point).
    std::size_t curve_unauthenticated() const;

    /// Whether the linked libzmq was built with CURVE. A build without it cannot encrypt, and a test
    /// that needs encryption must say so rather than quietly run plaintext.
    static bool curve_available();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace mosaic

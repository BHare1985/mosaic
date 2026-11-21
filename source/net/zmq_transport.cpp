#include <mosaic/core/z85.h>
#include <mosaic/match/matcher.h>
#include <mosaic/net/peer_pins.h>
#include <mosaic/net/zmq_transport.h>
#include <mosaic/proto/codec.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <zmq.h>

using std::fprintf;
using std::getenv;
using std::make_unique;
using std::map;
using std::min;
using std::move;
using std::nullopt;
using std::optional;
using std::set;
using std::size_t;
using std::string;
using std::strlen;
using std::uint8_t;
using std::unique_ptr;
using std::vector;

namespace mosaic {
namespace {

/// Small RAII handles: libzmq is a C API and every socket/context needs closing exactly once.
struct Context {
    void* handle = nullptr;
    Context() : handle(zmq_ctx_new()) {}
    ~Context() {
        if (handle)
            zmq_ctx_term(handle);
    }
    Context(const Context&) = delete;
    Context& operator=(const Context&) = delete;
};

struct Socket {
    void* handle = nullptr;
    explicit Socket(void* ctx, int type) : handle(zmq_socket(ctx, type)) {}
    ~Socket() {
        if (handle)
            zmq_close(handle);
    }
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;

    /// False when this libzmq does not know the option: a build without it must degrade visibly
    /// rather than silently run without the behaviour the option was there for.
    bool set_int(int option, int value) {
        return zmq_setsockopt(handle, option, &value, sizeof(value)) == 0;
    }
    /// Options whose value is a 64-bit integer (ZMQ_MAXMSGSIZE among them). Setting those with an
    /// `int` fails with EINVAL and leaves the socket without the limit, which is how a "bounded"
    /// transport ends up unbounded.
    bool set_int64(int option, int64_t value) {
        return zmq_setsockopt(handle, option, &value, sizeof(value)) == 0;
    }
    void set_string(const string& value) {
        zmq_setsockopt(handle, ZMQ_IDENTITY, value.data(), value.size());
    }

    bool bind(const string& endpoint) {
        return zmq_bind(handle, endpoint.c_str()) == 0;
    }
    bool connect(const string& endpoint) {
        return zmq_connect(handle, endpoint.c_str()) == 0;
    }

    bool send_frame(const void* data, size_t size, bool more) {
        const int flags = ZMQ_DONTWAIT | (more ? ZMQ_SNDMORE : 0);
        return zmq_send(handle, data, size, flags) == static_cast<int>(size);
    }

    /// Receive one frame into `out`. False when nothing is available.
    /// `user_id` receives the authenticated CURVE identity when the message arrived encrypted.
    bool receive_frame(string& out, string* user_id = nullptr) {
        zmq_msg_t message;
        zmq_msg_init(&message);
        const int size = zmq_msg_recv(&message, handle, ZMQ_DONTWAIT);
        if (size < 0) {
            zmq_msg_close(&message);
            return false;
        }
        if (user_id) {
            const char* id = zmq_msg_gets(&message, "User-Id");
            if (id)
                *user_id = id;
        }
        out.assign(static_cast<const char*>(zmq_msg_data(&message)), static_cast<size_t>(size));
        zmq_msg_close(&message);
        return true;
    }

    /// Is another frame waiting in the message currently being received?
    bool more() const {
        int more = 0;
        size_t size = sizeof(more);
        if (zmq_getsockopt(handle, ZMQ_RCVMORE, &more, &size) != 0)
            return false;
        return more != 0;
    }
    /// The value this socket reports for an option, for the diagnostic accessor. libzmq is strict
    /// about the width, and options are not uniform: try `int` first, then `int64`.
    int option_value(int option) const {
        int value = -1;
        size_t size = sizeof(value);
        if (zmq_getsockopt(handle, option, &value, &size) == 0)
            return value;
        int64_t wide = -1;
        size = sizeof(wide);
        if (zmq_getsockopt(handle, option, &wide, &size) != 0)
            return -1;
        return static_cast<int>(wide);
    }

    string last_endpoint() const {
        char buffer[256] = {};
        size_t size = sizeof(buffer) - 1;
        if (zmq_getsockopt(handle, ZMQ_LAST_ENDPOINT, buffer, &size) != 0)
            return {};
        // libzmq counts the terminating NUL in the size it reports, so a raw copy carries it into
        // every endpoint string this transport hands out - which then fails any comparison, any
        // length check and any address validation that expects a bare "tcp://host:port".
        while (size > 0 && buffer[size - 1] == '\0')
            --size;
        return string(buffer, size);
    }
};

/// The coarse filter ZeroMQ can apply: everything up to the first wildcard. "exactly one segment"
/// cannot be expressed as a byte prefix, which is why the real matcher re-checks every delivery.
string literal_prefix(const string& pattern) {
    const auto star = pattern.find('*');
    const auto tail = pattern.find('>');
    auto cut = min(star == string::npos ? pattern.size() : star,
                   tail == string::npos ? pattern.size() : tail);
    if (cut == 0)
        return {};  // a pattern starting with a wildcard accepts anything
    // Cut at the last separator before the wildcard so "storage.*" subscribes "storage.".
    const auto dot = pattern.rfind('.', cut == pattern.size() ? cut : cut - 1);
    return dot == string::npos ? string{} : pattern.substr(0, dot + 1);
}

}  // namespace

struct ZmqTransport::Impl {
    ZmqConfig config;
    Context context;
    unique_ptr<Socket> pub;
    unique_ptr<Socket> router;
    map<NodeId, unique_ptr<Socket>> dealers;  // unicast to each peer
    map<NodeId, unique_ptr<Socket>> subs;     // live traffic from each peer
    Matcher matcher;                          // our own subscriptions: the exact decision
    set<NodeId> ignored;  // peers the node has banned: their frames are dropped before decoding
    vector<NodeId> decode_faults;  // senders of frames that did not decode, since the last call
    size_t dropped = 0;
    size_t malformed = 0;       // messages whose frame count did not match their kind
    size_t option_rejects = 0;  // socket options this libzmq build does not support
    size_t curve_rejects = 0;
    size_t curve_unauthenticated = 0;  // accepted without an observable peer key (needs SEC-1b)
    unique_ptr<PeerPins> pins;         // address (open) or key list (closed), when CURVE is enabled
    unique_ptr<Socket> zap;  // inproc handler libzmq consults before a handshake completes
    bool bound = false;      // both bind calls succeeded

    explicit Impl(ZmqConfig cfg) : config(move(cfg)) {}

    /// Everything that must be true of a socket before it binds or connects. The frame limit is set
    /// here, not after: ZMQ_MAXMSGSIZE takes effect from the first frame, so setting it late
    /// protects nothing.
    void apply_limits(Socket& socket) {
        // One place to apply an option, whatever its width: libzmq is strict about that, and a
        // rejected option must be counted rather than assumed.
        const auto set = [this, &socket](int option, auto value) {
            const auto ok = sizeof(value) == sizeof(int64_t)
                                ? socket.set_int64(option, static_cast<int64_t>(value))
                                : socket.set_int(option, static_cast<int>(value));
            if (ok)
                return;
            if (getenv("MOSAIC_ZMQ_TRACE"))
                fprintf(stderr, "[opt] rejected %d\n", option);
            ++option_rejects;
        };
        set(ZMQ_SNDHWM, config.send_hwm);
        set(ZMQ_RCVHWM, config.receive_hwm);
        set(ZMQ_LINGER, 0);  // never block shutdown on undelivered traffic
        set(ZMQ_MAXMSGSIZE, static_cast<int64_t>(kMaxFrameBytes));
        set(ZMQ_TCP_KEEPALIVE, 1);  // notice a half-open link instead of queueing into it
        if (config.heartbeat_ivl_ms != 0) {
            set(ZMQ_HEARTBEAT_IVL, static_cast<int>(config.heartbeat_ivl_ms));
            set(ZMQ_HEARTBEAT_TIMEOUT, static_cast<int>(config.heartbeat_timeout_ms));
        }
    }

    void subscribe_socket(Socket& socket, const string& pattern) const {
        const auto prefix = literal_prefix(pattern);
        zmq_setsockopt(socket.handle, ZMQ_SUBSCRIBE, prefix.data(), prefix.size());
    }

    /// True when the linked libzmq was built with CURVE. Checked rather than assumed: a libzmq
    /// without it accepts the connection plaintext, which would be a security hole reported as a
    /// success. Probed once per process - creating a context starts an I/O thread, and this used to
    /// run for every socket wired.
    static bool curve_supported() {
        static const bool supported = [] {
            void* context = zmq_ctx_new();
            void* socket = zmq_socket(context, ZMQ_DEALER);
            const char* key = "rq:rM>}U?@Lns47E1%kR.o@n%FcmmsL/@{H8]yf7";
            const bool ok = zmq_setsockopt(socket, ZMQ_CURVE_PUBLICKEY, key, strlen(key)) == 0;
            zmq_close(socket);
            zmq_ctx_term(context);
            return ok;
        }();
        return supported;
    }

    bool curve_active() const {
        return config.curve.enabled && curve_supported();
    }

    PeerPins& pin_store() {
        if (!pins) {
            pins = make_unique<PeerPins>(config.link_policy == ZmqLinkPolicy::Closed
                                             ? PeerPins::Mode::Closed
                                             : PeerPins::Mode::Open);
            if (!config.curve.pins_path.empty())
                if (auto loaded = PeerPins::load(config.curve.pins_path, pins->mode()))
                    pins = make_unique<PeerPins>(*loaded);
        }
        return *pins;
    }

    /// Count a message whose frame count did not match its kind, and attribute it to its sender so
    /// peer scoring sees a protocol violation the way it sees a decode failure.
    void refuse_malformed(const NodeId& from) {
        ++malformed;
        decode_faults.push_back(from);
    }

    /// Drain frames past the ones a message kind declares, so a surplus cannot be decoded as the
    /// next message. True when there was one.
    bool drain_surplus(Socket& socket, const NodeId& from) {
        if (!socket.more())
            return false;
        while (socket.more()) {
            string surplus;
            if (!socket.receive_frame(surplus))
                break;
        }
        ++dropped;
        refuse_malformed(from);
        return true;
    }

    /// Decide one CURVE handshake from a ZAP request:
    ///   version, request_id, domain, address, identity, mechanism, credentials...
    /// The reply is SIX frames - version, request id, status code, status text, user id, metadata -
    /// which is what czmq's zauth sends. REP already holds the request's envelope and prepends it,
    /// so adding an empty delimiter frame here shifts libzmq's indices and it reports
    /// ZAP_MALFORMED_REPLY.
    bool handle_zap_request() {
        vector<string> frames;
        for (;;) {
            string frame;
            if (!zap->receive_frame(frame))
                break;
            frames.push_back(move(frame));
            int more = 0;
            size_t size = sizeof(more);
            zmq_getsockopt(zap->handle, ZMQ_RCVMORE, &more, &size);
            if (!more)
                break;
        }
        if (frames.size() < 6)
            return false;  // nothing pending, or not a request we understand

        const string& request_id = frames[1];
        const string& address = frames[3];
        const string& identity = frames[4];
        const string& mechanism = frames[5];

        string status = "400";
        string text = "denied";
        string user_id;
        PinVerdict verdict = PinVerdict::NotListed;
        string who;

        if (mechanism != "CURVE" || frames.size() < 7 || frames[6].size() != 32) {
            text = "unsupported mechanism";  // NULL is refused too: this transport is encrypted or
                                             // nothing
        } else {
            const auto key_z85 =
                z85_encode(reinterpret_cast<const uint8_t*>(frames[6].data()), frames[6].size());
            // The handshake does not expose the peer's configured identity, so open mode pins by
            // address and closed mode checks the key against what was provisioned
            // (PeerPins::decide).
            who = identity.empty() ? address : identity;
            verdict = pin_store().decide(who, key_z85);
            if (verdict == PinVerdict::Allowed || verdict == PinVerdict::AllowedAndPinned) {
                status = "200";
                text = "ok";
                user_id = key_z85;
                if (verdict == PinVerdict::AllowedAndPinned && !config.curve.pins_path.empty())
                    pin_store().save(config.curve.pins_path);
            } else {
                ++curve_rejects;
                text = verdict == PinVerdict::Mismatch ? "key changed" : "key not allowed";
            }
        }

        if (getenv("MOSAIC_ZAP_TRACE"))
            fprintf(stderr, "[zap] identity=[%s] addr=%s verdict(%s)=%s -> %s\n", identity.c_str(),
                    address.c_str(), who.c_str(), to_string(verdict), status.c_str());
        const auto send = [&](const string& data, bool more) {
            return zap->send_frame(data.data(), data.size(), more);
        };
        const bool sent = send("1.0", true) && send(request_id, true) && send(status, true) &&
                          send(text, true) && send(user_id, true) && send("", false);
        if (!sent)
            return false;
        return status == "200";
    }

    void apply_curve_server(Socket& socket) const {
        if (!curve_active())
            return;
        socket.set_int(ZMQ_CURVE_SERVER, 1);
        zmq_setsockopt(socket.handle, ZMQ_CURVE_SECRETKEY, config.curve.secret_z85.data(),
                       config.curve.secret_z85.size());
    }

    void apply_curve_client(Socket& socket, const string& peer_public_z85) const {
        if (!curve_active())
            return;
        zmq_setsockopt(socket.handle, ZMQ_CURVE_SERVERKEY, peer_public_z85.data(),
                       peer_public_z85.size());
        zmq_setsockopt(socket.handle, ZMQ_CURVE_PUBLICKEY, config.curve.public_z85.data(),
                       config.curve.public_z85.size());
        zmq_setsockopt(socket.handle, ZMQ_CURVE_SECRETKEY, config.curve.secret_z85.data(),
                       config.curve.secret_z85.size());
    }

    /// Enforce the pinning policy for a message that arrived over CURVE from a peer we serve.
    /// Returns false when the peer's authenticated key is not the one we expect - an impersonating
    /// peer cannot forge its User-Id, so the check is on the key the handshake actually proved.
    /// Only called on the ROUTER path, where that metadata exists.
    bool accept_key(const NodeId& peer, const string& user_id) {
        if (!curve_active())
            return true;
        if (!pins) {
            pins = make_unique<PeerPins>(config.link_policy == ZmqLinkPolicy::Closed
                                             ? PeerPins::Mode::Closed
                                             : PeerPins::Mode::Open);
            if (!config.curve.pins_path.empty())
                if (auto loaded = PeerPins::load(config.curve.pins_path, pins->mode()))
                    pins = make_unique<PeerPins>(*loaded);
        }
        if (user_id.empty()) {
            // This libzmq build does not expose the peer's CURVE key as message metadata, so
            // server-side pinning cannot be enforced here. The link is still encrypted and the
            // *client* still proves the server's key at connect (ZMQ_CURVE_SERVERKEY). Key-based
            // pinning needs a ZAP handler, tracked as SEC-1b; until then this is counted so the gap
            // is visible rather than silent.
            ++curve_unauthenticated;
            return true;
        }
        const auto verdict = pins->observe(peer, user_id);
        if (verdict == PinVerdict::Mismatch || verdict == PinVerdict::NotListed) {
            ++curve_rejects;
            return false;
        }
        if (verdict == PinVerdict::AllowedAndPinned && !config.curve.pins_path.empty())
            pins->save(config.curve.pins_path);
        return true;
    }

    void wire_peer(const ZmqPeerEndpoint& peer) {
        if (peer.id == config.id)
            return;  // never talk to ourselves
        if (dealers.count(peer.id))
            return;  // already wired

        auto sub = make_unique<Socket>(context.handle, ZMQ_SUB);
        apply_limits(*sub);
        // No ZMQ_IMMEDIATE here, deliberately. A SUB socket's first "send" is its subscription, and
        // IMMEDIATE would discard that instead of queueing it until the connection completes - the
        // publisher then never learns the pattern and live delivery silently stops. The requirement
        // is about application sends, which is the DEALER below.
        apply_curve_client(*sub, peer.curve_public_z85);
        sub->connect(peer.pub_endpoint);
        for (const auto& pattern : patterns)  // apply what was subscribed before this peer appeared
            subscribe_socket(*sub, pattern);
        subs.emplace(peer.id, move(sub));

        auto dealer = make_unique<Socket>(context.handle, ZMQ_DEALER);
        apply_limits(*dealer);
        dealer->set_int(ZMQ_IMMEDIATE, 1);
        apply_curve_client(*dealer, peer.curve_public_z85);
        dealer->set_string(config.id);
        dealer->connect(peer.router_endpoint);
        dealers.emplace(peer.id, move(dealer));
    }

    vector<string> patterns;  // subscription history, replayed onto late peers
};

ZmqTransport::ZmqTransport(ZmqConfig config) : impl_(make_unique<Impl>(move(config))) {
    impl_->pub = make_unique<Socket>(impl_->context.handle, ZMQ_PUB);
    impl_->apply_limits(*impl_->pub);
    impl_->apply_curve_server(*impl_->pub);
    const bool pub_bound = impl_->pub->bind(impl_->config.pub_bind);

    impl_->router = make_unique<Socket>(impl_->context.handle, ZMQ_ROUTER);
    impl_->apply_limits(*impl_->router);
    // A node that restarts with its own identity must be able to take over from its dying
    // connection instead of being refused until that one expires.
    impl_->router->set_int(ZMQ_ROUTER_HANDOVER, 1);
    impl_->apply_curve_server(*impl_->router);
    const bool router_bound = impl_->router->bind(impl_->config.router_bind);
    impl_->bound = pub_bound && router_bound;

    if (impl_->curve_active()) {
        // Must exist before any peer connects: libzmq blocks the handshake until the handler
        // answers.
        impl_->zap = make_unique<Socket>(impl_->context.handle, ZMQ_REP);
        impl_->zap->bind("inproc://zeromq.zap.01");
    }

    for (const auto& peer : impl_->config.peers)
        impl_->wire_peer(peer);
}

ZmqTransport::~ZmqTransport() = default;

bool ZmqTransport::is_bound() const {
    return impl_->bound;
}

NodeId ZmqTransport::self() const {
    return impl_->config.id;
}

void ZmqTransport::subscribe(const string& pattern) {
    if (!impl_->matcher.add(pattern))
        return;
    impl_->patterns.push_back(pattern);
    for (auto& [peer, sub] : impl_->subs)
        impl_->subscribe_socket(*sub, pattern);
}

void ZmqTransport::add_peer(const ZmqPeerEndpoint& peer) {
    impl_->wire_peer(peer);
}

void ZmqTransport::publish(const Channel& channel, Message message) {
    const Bytes frame = encode_message(message);
    // Three frames: the channel (so PUB-side filtering works), who sent it, and the message.
    const bool trace = getenv("MOSAIC_SEND_TRACE") != nullptr;
    if (!impl_->pub->send_frame(channel.data(), channel.size(), true) ||
        !impl_->pub->send_frame(impl_->config.id.data(), impl_->config.id.size(), true) ||
        !impl_->pub->send_frame(frame.data(), frame.size(), false)) {
        ++impl_->dropped;  // queue limit reached: a drop is counted, never a block
    }
    if (trace)
        fprintf(stderr, "[send] channel=%s dropped=%zu\n", channel.c_str(), impl_->dropped);
}

void ZmqTransport::send(const NodeId& to, Message message) {
    const auto it = impl_->dealers.find(to);
    if (it == impl_->dealers.end())
        return;  // unknown peer: silently dropped, per the contract
    const Bytes frame = encode_message(message);
    if (!it->second->send_frame(frame.data(), frame.size(), false))
        ++impl_->dropped;
}

void ZmqTransport::service_handshakes() {
    if (impl_->zap)
        impl_->handle_zap_request();  // one per call: libzmq queues them and the call stays bounded
}

optional<Envelope> ZmqTransport::poll() {
    // Handshakes first: a peer connecting right now must not wait for us to finish with traffic.
    service_handshakes();

    // At most one message per call, so the caller's loop keeps control of time and ordering. The
    // set is sized from the sockets it watches: peers arrive at runtime through configuration and
    // gossip, so no fixed capacity can be assumed (this used to be a three-entry stack array).
    vector<zmq_pollitem_t> items(1 + impl_->subs.size());
    items[0] = zmq_pollitem_t{impl_->router->handle, 0, ZMQ_POLLIN, 0};
    size_t slot = 1;
    for (auto& [peer, sub] : impl_->subs)
        items[slot++] = zmq_pollitem_t{sub->handle, 0, ZMQ_POLLIN, 0};

    const int ready = zmq_poll(items.data(), static_cast<int>(items.size()), 0);
    if (ready <= 0)
        return nullopt;

    // Unicast first: repair traffic is what makes a follower converge.
    if (items[0].revents & ZMQ_POLLIN) {
        string identity;
        string payload;
        // The authenticated peer identity arrives as metadata on the FIRST frame of a ROUTER
        // message (the routing id), not on the payload. The authenticated identity is CURVE
        // metadata; which frame carries it depends on the socket and on how the message was sent,
        // so both parts are inspected and the first non-empty wins.
        string user_id;
        string payload_user_id;
        if (!impl_->router->receive_frame(identity, &user_id))
            return nullopt;
        if (!impl_->router->receive_frame(payload, &payload_user_id)) {
            impl_->refuse_malformed(identity);  // a message that ends after its identity frame
            return nullopt;
        }
        // A unicast message is exactly identity + payload.
        if (impl_->drain_surplus(*impl_->router, identity))
            return nullopt;
        if (user_id.empty())
            user_id = payload_user_id;
        if (!impl_->accept_key(identity, user_id))
            return nullopt;
        // A banned peer's frame is dropped here, before the hash and the parse a decode costs. The
        // node would drop it too; doing it at the wire is the whole point of SEC-2's "cheap to
        // ignore".
        if (impl_->ignored.count(identity) != 0) {
            ++impl_->dropped;
            return nullopt;
        }
        const auto decoded = decode_message(Bytes(payload.begin(), payload.end()));
        if (!decoded.ok()) {
            ++impl_->dropped;
            impl_->decode_faults.push_back(identity);
            return nullopt;
        }
        return Envelope{identity, *decoded.message};
    }

    int index = 1;
    for (auto& [peer, sub] : impl_->subs) {
        if (!(items[index].revents & ZMQ_POLLIN)) {
            ++index;
            continue;
        }
        ++index;
        string channel;
        string sender;
        string payload;
        if (!sub->receive_frame(channel) || !sub->receive_frame(sender)) {
            ++impl_->malformed;  // no sender to attribute it to yet
            return nullopt;
        }
        if (!sub->receive_frame(payload)) {
            impl_->refuse_malformed(sender);
            return nullopt;
        }
        // A live message is exactly channel + sender + payload, and the socket layer's prefix
        // filter is the only thing that ran on the first frame.
        if (impl_->drain_surplus(*sub, sender))
            continue;
        // No pin check on the live path: this end is the CURVE *client* here, so the server's key
        // was already chosen at connect (ZMQ_CURVE_SERVERKEY). Pinning is a server-side policy over
        // client identities, which is exactly where a peer could otherwise claim any name it likes.
        if (getenv("MOSAIC_SEND_TRACE"))
            fprintf(stderr, "[recv] channel=%s sender=%s bytes=%zu\n", channel.c_str(),
                    sender.c_str(), payload.size());
        // The byte-prefix subscription let this through; the real pattern decides.
        if (!impl_->matcher.matches_any(channel))
            continue;
        if (impl_->ignored.count(sender) != 0) {
            ++impl_->dropped;
            continue;
        }
        const auto decoded = decode_message(Bytes(payload.begin(), payload.end()));
        if (!decoded.ok()) {
            ++impl_->dropped;
            impl_->decode_faults.push_back(sender);
            continue;
        }
        return Envelope{sender, *decoded.message};
    }
    return nullopt;
}

vector<NodeId> ZmqTransport::take_decode_faults() {
    vector<NodeId> out;
    out.swap(impl_->decode_faults);
    return out;
}

void ZmqTransport::set_peer_ignored(const NodeId& peer, bool ignored) {
    if (ignored)
        impl_->ignored.insert(peer);
    else
        impl_->ignored.erase(peer);
}

vector<NodeId> ZmqTransport::peers() const {
    vector<NodeId> out;
    out.reserve(impl_->dealers.size());
    for (const auto& [id, dealer] : impl_->dealers)
        out.push_back(id);
    return out;
}

string ZmqTransport::pub_endpoint() const {
    return impl_->pub->last_endpoint();
}

string ZmqTransport::router_endpoint() const {
    return impl_->router->last_endpoint();
}

size_t ZmqTransport::dropped() const {
    return impl_->dropped;
}

size_t ZmqTransport::malformed() const {
    return impl_->malformed;
}

size_t ZmqTransport::option_rejects() const {
    return impl_->option_rejects;
}

int ZmqTransport::socket_option(int option) const {
    return impl_->pub ? impl_->pub->option_value(option) : -1;
}

size_t ZmqTransport::curve_rejects() const {
    return impl_->curve_rejects;
}

size_t ZmqTransport::curve_unauthenticated() const {
    return impl_->curve_unauthenticated;
}

bool ZmqTransport::curve_available() {
    return ZmqTransport::Impl::curve_supported();
}

}  // namespace mosaic

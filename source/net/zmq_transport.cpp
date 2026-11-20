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

    void set_int(int option, int value) {
        zmq_setsockopt(handle, option, &value, sizeof(value));
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
    size_t curve_rejects = 0;
    size_t curve_unauthenticated = 0;  // accepted without an observable peer key (needs SEC-1b)
    unique_ptr<PeerPins> pins;         // address (open) or key list (closed), when CURVE is enabled
    unique_ptr<Socket> zap;  // inproc handler libzmq consults before a handshake completes
    bool bound = false;      // both bind calls succeeded

    explicit Impl(ZmqConfig cfg) : config(move(cfg)) {}

    void apply_limits(Socket& socket) const {
        socket.set_int(ZMQ_SNDHWM, config.send_hwm);
        socket.set_int(ZMQ_RCVHWM, config.receive_hwm);
        socket.set_int(ZMQ_LINGER, 0);  // never block shutdown on undelivered traffic
    }

    void subscribe_socket(Socket& socket, const string& pattern) const {
        const auto prefix = literal_prefix(pattern);
        zmq_setsockopt(socket.handle, ZMQ_SUBSCRIBE, prefix.data(), prefix.size());
    }

    /// True when the linked libzmq was built with CURVE. Checked rather than assumed: a libzmq
    /// without it accepts the connection plaintext, which would be a security hole reported as a
    /// success.
    static bool curve_supported() {
        void* context = zmq_ctx_new();
        void* socket = zmq_socket(context, ZMQ_DEALER);
        const char* key = "rq:rM>}U?@Lns47E1%kR.o@n%FcmmsL/@{H8]yf7";
        const bool ok = zmq_setsockopt(socket, ZMQ_CURVE_PUBLICKEY, key, strlen(key)) == 0;
        zmq_close(socket);
        zmq_ctx_term(context);
        return ok;
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
        apply_curve_client(*sub, peer.curve_public_z85);
        sub->connect(peer.pub_endpoint);
        for (const auto& pattern : patterns)  // apply what was subscribed before this peer appeared
            subscribe_socket(*sub, pattern);
        subs.emplace(peer.id, move(sub));

        auto dealer = make_unique<Socket>(context.handle, ZMQ_DEALER);
        apply_limits(*dealer);
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

    // At most one message per call, so the caller's loop keeps control of time and ordering.
    zmq_pollitem_t items[2 + 1] = {};
    int count = 0;
    items[count++] = zmq_pollitem_t{impl_->router->handle, 0, ZMQ_POLLIN, 0};
    for (auto& [peer, sub] : impl_->subs)
        items[count++] = zmq_pollitem_t{sub->handle, 0, ZMQ_POLLIN, 0};

    const int ready = zmq_poll(items, count, 0);
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
        if (!impl_->router->receive_frame(payload, &payload_user_id))
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
        if (!sub->receive_frame(channel) || !sub->receive_frame(sender))
            return nullopt;
        if (!sub->receive_frame(payload))
            return nullopt;
        // No pin check on the live path: this end is the CURVE *client* here, so the server's key
        // was already chosen at connect (ZMQ_CURVE_SERVERKEY). Pinning is a server-side policy over
        // client identities, which is exactly where a peer could otherwise claim any name it likes.
        if (getenv("MOSAIC_SEND_TRACE"))
            fprintf(stderr, "[recv] channel=%s bytes=%zu\n", channel.c_str(), payload.size());
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

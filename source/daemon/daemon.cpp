#include <mosaic/daemon/daemon.h>

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <mosaic/core/keystore.h>
#include <mosaic/net/zmq_transport.h>
#include <mosaic/node/channel_rules.h>
#include <mosaic/node/node.h>
#include <mosaic/store/sqlite_log_store.h>
#include <sstream>
#include <thread>

namespace mosaic::daemon {

struct Daemon::Impl {
    Config config;
    std::unique_ptr<SqliteLogStore> store;
    std::unique_ptr<ZmqTransport> transport;
    std::unique_ptr<Sha256Hasher> hasher;
    std::unique_ptr<Ed25519Verifier> verifier;
    std::unique_ptr<ChannelRules> rules;
    std::unique_ptr<Node> node;
    std::uint64_t now_ms = 0;
    std::uint64_t last_status_ms = 0;
    std::string pub;
    std::string router;
    bool curve_on = false;
    std::string curve_public;
};

Daemon::Daemon()
    : impl_(std::make_unique<Impl>())
{
}

Daemon::~Daemon() = default;

std::unique_ptr<Daemon> Daemon::create(const Config& config, std::string& error)
{
    const auto keystore = load_keystore(config.keys);
    if (!keystore) {
        error = "cannot load keys: " + config.keys;
        return nullptr;
    }
    auto store = SqliteLogStore::open(config.store);
    if (!store) {
        error = "cannot open store: " + config.store;
        return nullptr;
    }

    auto daemon = std::unique_ptr<Daemon>(new Daemon());
    auto& self = *daemon->impl_;
    self.config = config;
    self.store = std::move(*store);

    // listen is host:port for the PUB; the ROUTER takes the next port.
    ZmqConfig transport_config;
    transport_config.id = config.id;
    const auto colon = config.listen.rfind(':');
    if (colon == std::string::npos) {
        error = "node.listen must be host:port";
        return nullptr;
    }
    const auto host = config.listen.substr(0, colon);
    const auto port = std::atoi(config.listen.c_str() + colon + 1);
    transport_config.pub_bind = "tcp://" + host + ":" + std::to_string(port);
    transport_config.router_bind = "tcp://" + host + ":" + std::to_string(port + 1);

    for (const auto& peer : config.peers) {
        ZmqPeerEndpoint endpoint;
        endpoint.id = peer.id;
        endpoint.pub_endpoint = "tcp://" + peer.endpoint;
        const auto peer_colon = peer.endpoint.rfind(':');
        endpoint.router_endpoint = "tcp://" + peer.endpoint.substr(0, peer_colon + 1)
            + std::to_string(std::atoi(peer.endpoint.c_str() + peer_colon + 1) + 1);
        endpoint.curve_public_z85 = peer.curve_public;
        transport_config.peers.push_back(endpoint);
    }
    if (config.curve)
        if (const auto curve = curve_identity_from_keystore(*keystore))
            transport_config.curve = ZmqCurveConfig { true, curve->public_z85, curve->secret_z85, "" };

    self.curve_on = transport_config.curve.enabled;
    self.curve_public = transport_config.curve.public_z85;
    self.transport = std::make_unique<ZmqTransport>(transport_config);
    self.pub = self.transport->pub_endpoint();
    self.router = self.transport->router_endpoint();

    if (!config.allow.empty()) {
        self.rules = std::make_unique<ChannelRules>();
        for (const auto& rule : config.allow)
            if (!self.rules->allow(rule.pattern, rule.writers)) {
                error = "invalid allow pattern: " + rule.pattern;
                return nullptr;
            }
    }

    self.hasher = std::make_unique<Sha256Hasher>();
    self.verifier = std::make_unique<Ed25519Verifier>();

    NodeConfig node_config;
    node_config.id = config.id;
    node_config.head_interval_ms = config.head_interval_ms;
    node_config.fetch_batch = config.fetch_batch;
    node_config.fetch_window = config.fetch_window;
    node_config.fetch_timeout_ms = config.fetch_timeout_ms;
    node_config.max_serve = config.max_serve;
    node_config.min_replicas = config.min_replicas;
    node_config.retention_limit = config.retention_limit;
    node_config.slow_follower_threshold = config.slow_follower_threshold;
    node_config.control_interval_ms = config.control_interval_ms;
    node_config.max_record_bytes = config.max_record_bytes;
    node_config.max_head_patterns = config.max_head_patterns;
    node_config.max_requests_per_interval = config.max_requests_per_interval;

    self.node = std::make_unique<Node>(
        node_config, *self.transport, *self.store, *self.hasher, *self.verifier);
    if (self.rules)
        self.node->set_rules(self.rules.get());
    for (const auto& pattern : config.follows)
        if (!self.node->follow(pattern)) {
            error = "invalid follow pattern: " + pattern;
            return nullptr;
        }
    return daemon;
}

RunResult Daemon::run(const std::function<bool()>& stop, const std::function<void()>& on_iteration)
{
    auto& self = *impl_;
    const auto started = std::chrono::steady_clock::now();
    auto last_tick = started;

    while (!stop()) {
        // Time is read once per iteration and handed to the node, which never reads a clock itself.
        const auto elapsed = std::chrono::steady_clock::now() - started;
        self.now_ms = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count());

        self.node->pump(self.now_ms);
        self.node->tick(self.now_ms);

        if (self.config.status_interval_ms != 0
            && self.now_ms - self.last_status_ms >= self.config.status_interval_ms) {
            self.last_status_ms = self.now_ms;
            if (self.config.level != "error")
                std::cout << status_line() << std::endl;
        }

        // A bounded wait: the transport's poll is non-blocking and has no socket-wait, so the loop paces
        // itself. A quiet daemon therefore does not spin, and a busy one adds at most this much latency.
        if (on_iteration)
            on_iteration();

        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        (void)last_tick;
    }

    // Finish the iteration, then let the store close through its destructor: a signal must not leave a
    // half-written record behind.
    self.transport.reset();
    return RunResult::Stopped;
}

std::string Daemon::status_line() const
{
    const auto& counters = impl_->node->counters();
    std::ostringstream out;
    out << "status accepted=" << counters.accepted << " duplicates=" << counters.duplicate
        << " gaps=" << counters.gap << " forks=" << counters.fork << " repair_sent=" << counters.fetches_sent
        << " retained=" << impl_->node->retained_records() << " shortfall=" << counters.retention_shortfall
        << " throttled=" << counters.requests_throttled << " handshake_rejects=" << counters.version_rejects
        << " unauthorized=" << counters.unauthorized_records;
    return out.str();
}

bool Daemon::curve_enabled() const { return impl_->curve_on && impl_->transport->curve_available(); }

std::string Daemon::curve_public() const { return impl_->curve_on ? impl_->curve_public : std::string { }; }

void Daemon::dump_status() const { std::cout << status_line() << std::endl; }

std::string Daemon::pub_endpoint() const { return impl_->pub; }

std::string Daemon::router_endpoint() const { return impl_->router; }

} // namespace mosaic::daemon

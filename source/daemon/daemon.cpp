#include <mosaic/core/keystore.h>
#include <mosaic/daemon/daemon.h>
#include <mosaic/net/czmq_discovery.h>
#include <mosaic/net/peer_discovery.h>
#include <mosaic/net/zmq_transport.h>
#include <mosaic/node/channel_rules.h>
#include <mosaic/node/node.h>
#include <mosaic/store/sqlite_log_store.h>

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <thread>

namespace chrono = std::chrono;
namespace this_thread = std::this_thread;
using std::atoi;
using std::cout;
using std::endl;
using std::function;
using std::make_unique;
using std::move;
using std::ostringstream;
using std::size_t;
using std::string;
using std::uint32_t;
using std::uint64_t;
using std::unique_ptr;
using std::vector;

namespace mosaic::daemon {

struct Daemon::Impl {
    Config config;
    unique_ptr<SqliteLogStore> store;
    unique_ptr<ZmqTransport> transport;
    unique_ptr<Sha256Hasher> hasher;
    unique_ptr<Ed25519Verifier> verifier;
    /// Retention signs a checkpoint with the node's own key: nobody else may say what the history
    /// at a sequence was, which is why the signer lives here rather than in the store.
    unique_ptr<Ed25519Signer> signer;
    unique_ptr<ChannelRules> rules;
    unique_ptr<CzmqDiscovery> discovery;
    PeerBook discovered;  // dial each peer once, bound the set, forget silent ones
    bool discovery_tick = false;
    unique_ptr<Node> node;
    uint64_t now_ms = 0;
    uint64_t last_status_ms = 0;
    string pub;
    string router;
    bool curve_on = false;
    string curve_public;
};

Daemon::Daemon() : impl_(make_unique<Impl>()) {}

Daemon::~Daemon() = default;

unique_ptr<Daemon> Daemon::create(const Config& config, string& error) {
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

    auto daemon = unique_ptr<Daemon>(new Daemon());
    auto& self = *daemon->impl_;
    self.config = config;
    self.store = move(*store);

    // listen is host:port for the PUB; the ROUTER takes the next port.
    ZmqConfig transport_config;
    transport_config.id = config.id;
    const auto colon = config.listen.rfind(':');
    if (colon == string::npos) {
        error = "node.listen must be host:port";
        return nullptr;
    }
    const auto host = config.listen.substr(0, colon);
    const auto port = atoi(config.listen.c_str() + colon + 1);
    transport_config.pub_bind = "tcp://" + host + ":" + std::to_string(port);
    transport_config.router_bind = "tcp://" + host + ":" + std::to_string(port + 1);

    for (const auto& peer : config.peers) {
        ZmqPeerEndpoint endpoint;
        endpoint.id = peer.id;
        endpoint.pub_endpoint = "tcp://" + peer.endpoint;
        const auto peer_colon = peer.endpoint.rfind(':');
        endpoint.router_endpoint = "tcp://" + peer.endpoint.substr(0, peer_colon + 1) +
                                   std::to_string(atoi(peer.endpoint.c_str() + peer_colon + 1) + 1);
        endpoint.curve_public_z85 = peer.curve_public;
        transport_config.peers.push_back(endpoint);
    }
    if (config.curve)
        if (const auto curve = curve_identity_from_keystore(*keystore))
            transport_config.curve = ZmqCurveConfig{true, curve->public_z85, curve->secret_z85, ""};

    self.curve_on = transport_config.curve.enabled;
    self.curve_public = transport_config.curve.public_z85;
    self.transport = make_unique<ZmqTransport>(transport_config);
    self.pub = self.transport->pub_endpoint();
    self.router = self.transport->router_endpoint();

    if (!config.allow.empty()) {
        self.rules = make_unique<ChannelRules>();
        for (const auto& rule : config.allow) {
            if (!self.rules->allow(rule.pattern, rule.writers)) {
                error = "invalid allow pattern: " + rule.pattern;
                return nullptr;
            }
            if (!rule.rotation_roots.empty() &&
                !self.rules->allow_rotation(rule.pattern, rule.rotation_roots)) {
                error = "invalid allow pattern: " + rule.pattern;
                return nullptr;
            }
        }
    }

    // Discovery, when configured: candidates are pulled once per loop iteration and dialled through
    // the same add_peer path a configured peer uses, so the key policy still decides whether they
    // are trusted.
    if (config.discover.enabled()) {
        CzmqDiscoveryConfig discovery_config;
        discovery_config.cluster = config.discover.cluster;
        discovery_config.beacon_port = config.discover.beacon_port;
        discovery_config.self = DiscoveredPeer{
            config.id, config.listen,
            transport_config.curve.enabled ? transport_config.curve.public_z85 : string{}};
        self.discovery = make_unique<CzmqDiscovery>(discovery_config);
    }

    self.hasher = make_unique<Sha256Hasher>();
    self.verifier = make_unique<Ed25519Verifier>();
    self.signer = make_unique<Ed25519Signer>(keystore->public_key, keystore->secret_key);

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
    node_config.strike_threshold = config.strike_threshold;
    node_config.strike_decay_ms = config.strike_decay_ms;
    node_config.ban_base_ms = config.ban_base_ms;
    node_config.ban_max_ms = config.ban_max_ms;
    node_config.gossip_interval_ms = config.gossip_interval_ms;
    node_config.gossip_fanout = config.gossip_fanout;
    node_config.gossip_max_hops = config.gossip_max_hops;
    node_config.gossip_entry_ttl_ms = config.gossip_entry_ttl_ms;
    node_config.gossip_table_limit = config.gossip_table_limit;
    node_config.retention_max_age_ms = config.retention_max_age_ms;
    node_config.retention_keep_min = config.retention_keep_min;
    node_config.retention_interval_ms = config.retention_interval_ms;
    // A node introduces itself by gossip, so it needs to know where it is reachable. The transport
    // has already resolved any ephemeral port by now.
    node_config.pub_endpoint = self.transport->pub_endpoint();
    node_config.router_endpoint = self.transport->router_endpoint();

    self.node =
        make_unique<Node>(node_config, *self.transport, *self.store, *self.hasher, *self.verifier);
    if (self.rules)
        self.node->set_rules(self.rules.get());
    // Peers from the config file are placed by an operator: gossip may confirm them, never move
    // them.
    {
        vector<NodeId> configured;
        for (const auto& peer : config.peers)
            configured.push_back(peer.id);
        self.node->set_configured_peers(move(configured));
    }
    // Both edges of every ban go to the operator. Printed at every level: a ban is the one thing
    // that makes a peer's traffic disappear, so silence must never be the only evidence (SEC-2).
    self.node->on_ban([](const BanEvent& event) { cout << ban_line(event) << endl; });
    for (const auto& pattern : config.follows)
        if (!self.node->follow(pattern)) {
            error = "invalid follow pattern: " + pattern;
            return nullptr;
        }
    return daemon;
}

RunResult Daemon::run(const function<bool()>& stop, const function<void()>& on_iteration) {
    auto& self = *impl_;
    const auto started = chrono::steady_clock::now();
    auto last_tick = started;

    while (!stop()) {
        // Time is read once per iteration and handed to the node, which never reads a clock itself.
        const auto elapsed = chrono::steady_clock::now() - started;
        self.now_ms =
            static_cast<uint64_t>(chrono::duration_cast<chrono::milliseconds>(elapsed).count());

        self.node->pump(self.now_ms);
        self.node->tick(self.now_ms);
        if (self.config.retention_interval_ms != 0)
            self.node->retain(*self.signer, self.now_ms);  // a policy evaluation, not a timer

        // Peers learned by gossip are dialled through the same path a beacon candidate uses, so the
        // key policy decides - and a peer the config file placed is never re-dialled elsewhere.
        for (const auto& candidate : self.node->take_new_gossip_peers()) {
            DiscoveredPeer found{candidate.id, candidate.pub_endpoint, {}};
            if (!self.discovered.observe(found, self.now_ms))
                continue;  // already known, or the book is full
            ZmqPeerEndpoint endpoint;
            endpoint.id = candidate.id;
            endpoint.pub_endpoint = candidate.pub_endpoint;
            endpoint.router_endpoint = candidate.router_endpoint;
            self.transport->add_peer(endpoint);
        }

        if (self.discovery) {
            for (const auto& candidate : self.discovery->poll_discovered()) {
                if (!self.discovered.observe(candidate, self.now_ms))
                    continue;  // already known, or the book is full
                ZmqPeerEndpoint endpoint;
                endpoint.id = candidate.id;
                endpoint.pub_endpoint = "tcp://" + candidate.endpoint;
                const auto colon = candidate.endpoint.rfind(':');
                endpoint.router_endpoint =
                    "tcp://" + candidate.endpoint.substr(0, colon + 1) +
                    std::to_string(atoi(candidate.endpoint.c_str() + colon + 1) + 1);
                endpoint.curve_public_z85 = candidate.curve_public;
                self.transport->add_peer(endpoint);
            }
            // Expiry runs at the status cadence: often enough to notice a disappeared peer, rarely
            // enough to be free.
            if (self.config.status_interval_ms != 0 &&
                self.now_ms - self.last_status_ms >= self.config.status_interval_ms)
                self.discovered.expire(self.now_ms);
        }

        if (self.config.status_interval_ms != 0 &&
            self.now_ms - self.last_status_ms >= self.config.status_interval_ms) {
            self.last_status_ms = self.now_ms;
            if (self.config.level != "error")
                cout << status_line() << endl;
        }

        // A bounded wait: the transport's poll is non-blocking and has no socket-wait, so the loop
        // paces itself. A quiet daemon therefore does not spin, and a busy one adds at most this
        // much latency.
        if (on_iteration)
            on_iteration();

        this_thread::sleep_for(chrono::milliseconds(5));
        (void)last_tick;
    }

    // Finish the iteration, then let the store close through its destructor: a signal must not
    // leave a half-written record behind.
    self.transport.reset();
    return RunResult::Stopped;
}

string counters_line(const NodeCounters& counters, uint32_t retained, size_t discovered) {
    ostringstream out;
    out << "status accepted=" << counters.accepted << " duplicates=" << counters.duplicate
        << " gaps=" << counters.gap << " forks=" << counters.fork
        << " repair_sent=" << counters.fetches_sent << " retained=" << retained
        << " shortfall=" << counters.retention_shortfall
        << " throttled=" << counters.requests_throttled
        << " handshake_rejects=" << counters.version_rejects
        << " unauthorized=" << counters.unauthorized_records << " bans=" << counters.bans_started
        << " banned=" << counters.peer_bans << " banned_drops=" << counters.banned_drops
        << " discovered=" << discovered;
    return out.str();
}

string ban_line(const BanEvent& event) {
    ostringstream out;
    out << "ban peer=" << event.peer;
    switch (event.edge) {
        case BanEdge::Started:
            out << " edge=start duration_ms=" << event.duration_ms << " strikes=" << event.strikes;
            break;
        case BanEdge::Expired:
            out << " edge=expired";
            break;
        case BanEdge::Lifted:
            out << " edge=lifted";
            break;
    }
    out << " bans=" << event.ban_count;
    return out.str();
}

string Daemon::status_line() const {
    const auto& counters = impl_->node->counters();
    return counters_line(counters, impl_->node->retained_records(), impl_->discovered.size());
}

size_t Daemon::discovered_peers() const {
    return impl_->discovered.size();
}

const char* Daemon::discovery_kind() const {
    return impl_->discovery ? impl_->discovery->kind() : "none";
}

bool Daemon::curve_enabled() const {
    return impl_->curve_on && impl_->transport->curve_available();
}

string Daemon::curve_public() const {
    return impl_->curve_on ? impl_->curve_public : string{};
}

void Daemon::dump_status() const {
    cout << status_line() << endl;
}

string Daemon::pub_endpoint() const {
    return impl_->pub;
}

string Daemon::router_endpoint() const {
    return impl_->router;
}

}  // namespace mosaic::daemon

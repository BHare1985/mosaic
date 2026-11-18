// Mosaic operator CLI. Thin by design: every command builds the same node the library user builds,
// drives pump/tick in a loop, and does nothing the library does not already do (see design D7).
//
//   Mosaic version
//   Mosaic match <pattern> <channel>
//   Mosaic keygen <path>                     generate an identity, print its hex id and CURVE key
//   Mosaic pub <channel> [options]           publish stdin lines as records, print seq + hash
//   Mosaic tail <pattern>[,<pattern>...] [options]   print matching records as they arrive
//
// Shared options: --node <id> --keys <path> --store <path> --listen <host:port>
//                 --peer <id=host:port,curve-z85>   (repeatable)
// Peer endpoints are supplied in configuration: static discovery, DISC-1.

#include <mosaic/core/keystore.h>
#include <mosaic/daemon/config.h>
#include <mosaic/log/log.h>
#include <mosaic/log/verify.h>
#include <mosaic/match/matcher.h>
#include <mosaic/net/sim_network.h>
#include <mosaic/net/zmq_transport.h>
#include <mosaic/node/node.h>
#include <mosaic/store/memory_log_store.h>
#include <mosaic/store/sqlite_log_store.h>
#include <mosaic/version.h>

#include <chrono>
#include <csignal>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace chrono = std::chrono;
namespace this_thread = std::this_thread;
using std::atoi;
using std::cerr;
using std::cin;
using std::cout;
using std::getline;
using std::make_unique;
using std::move;
using std::nullopt;
using std::numeric_limits;
using std::ofstream;
using std::optional;
using std::pair;
using std::sig_atomic_t;
using std::signal;
using std::size_t;
using std::string;
using std::stringstream;
using std::uint64_t;
using std::unique_ptr;
using std::vector;

using namespace mosaic;

namespace {

volatile sig_atomic_t g_stop = 0;
void on_signal(int) {
    g_stop = 1;
}

int usage() {
    cerr
        << "usage:\n"
           "  Mosaic version\n"
           "  Mosaic match <pattern> <channel>\n"
           "  Mosaic keygen <path>\n"
           "  Mosaic pub <channel> [options]\n"
           "  Mosaic tail <pattern[,pattern...]> [options]\n"
           "  Mosaic verify <store>\n"
           "  Mosaic sim [--seeds N] [--records N]\n"
           "\n"
           "options: --node <id>  --keys <path>  --store <path>  --listen <host:port>\n"
           "         --peer <id=host:port,curve-z85>   (repeatable)\n"
           "         --allow-writer <pattern>=<id>[,<id>]   (repeatable; open by default)\n"
           "         --no-curve                             diagnostic: run the link in plaintext\n"
           "         --config <file>                        ZPL config shared with mosaicd (flags "
           "win)\n";
    return 2;
}

struct Options {
    string node = "node";
    string keys = "identity.key";
    string store = "mosaic.db";
    string listen = "127.0.0.1";
    int port = 0;  // 0 = pick a free port and report it
    vector<ZmqPeerEndpoint> peers;
    vector<pair<string, vector<WriterId>>> allow;  // channel rules
    string config_path;                            // ZPL file shared with mosaicd
    bool explicit_node = false, explicit_keys = false, explicit_store = false,
         explicit_listen = false;
    bool explicit_peers = false, explicit_allow = false;
    bool no_curve = false;  // diagnostic: run the link in plaintext to isolate CURVE issues
    string port_out;        // file to write the chosen ports to, so a test can discover them
};

/// "<id>=<host:port>,<curve-z85>" -> a peer endpoint. The curve key is optional so plaintext demos
/// work.
bool parse_peer(const string& text, ZmqPeerEndpoint& out, string& error) {
    const auto equals = text.find('=');
    const auto comma = text.find(',');
    if (equals == string::npos || (comma != string::npos && comma < equals)) {
        error = "peer must be <id>=<host:port>[,<curve-z85>]: " + text;
        return false;
    }
    out.id = text.substr(0, equals);
    const auto endpoint =
        text.substr(equals + 1, comma == string::npos ? string::npos : comma - equals - 1);
    out.pub_endpoint = "tcp://" + endpoint;
    out.router_endpoint =
        out.pub_endpoint;  // both sockets bind the same host; the port pairs are given below
    if (comma != string::npos)
        out.curve_public_z85 = text.substr(comma + 1);
    return true;
}

/// Read the shared ZPL config into the options. Explicit flags then win over it, which is the
/// documented precedence and the reason both carry the same names.
bool apply_config(Options& options, string& error) {
    if (options.config_path.empty())
        return true;
    const auto loaded = mosaic::daemon::load_config(options.config_path);
    if (!loaded.ok()) {
        string joined;
        for (const auto& problem : loaded.errors)
            joined += "\n  " + problem.where + ": " + problem.message;
        error = "invalid config " + options.config_path + ":" + joined;
        return false;
    }
    const auto& config = loaded.config;
    if (!options.explicit_node && !config.id.empty())
        options.node = config.id;
    if (!options.explicit_keys && !config.keys.empty())
        options.keys = config.keys;
    if (!options.explicit_store && !config.store.empty())
        options.store = config.store;
    if (!options.explicit_listen && !config.listen.empty()) {
        const auto colon = config.listen.rfind(':');
        if (colon != string::npos) {
            options.listen = config.listen.substr(0, colon);
            options.port = atoi(config.listen.c_str() + colon + 1);
        }
    }
    if (!options.explicit_peers)
        for (const auto& peer : config.peers)
            options.peers.push_back({peer.id, "tcp://" + peer.endpoint, "", peer.curve_public});
    if (!options.explicit_allow)
        for (const auto& rule : config.allow)
            options.allow.emplace_back(rule.pattern, rule.writers);
    return true;
}

bool parse_options(int argc, char** argv, int first, Options& options, string& error) {
    for (int i = first; i < argc; ++i) {
        const string flag = argv[i];
        const auto next = [&](string& into) {
            if (i + 1 >= argc) {
                error = "missing value for " + flag;
                return false;
            }
            into = argv[++i];
            return true;
        };
        if (flag == "--config") {
            if (!next(options.config_path))
                return false;
        } else if (flag == "--node") {
            options.explicit_node = true;
            if (!next(options.node))
                return false;
        } else if (flag == "--keys") {
            options.explicit_keys = true;
            if (!next(options.keys))
                return false;
        } else if (flag == "--store") {
            options.explicit_store = true;
            if (!next(options.store))
                return false;
        } else if (flag == "--listen") {
            options.explicit_listen = true;
            string value;
            if (!next(value))
                return false;
            // The documented form is host:port; --port still exists for the two-part spelling.
            const auto colon = value.rfind(':');
            if (colon != string::npos && colon + 1 < value.size()) {
                options.listen = value.substr(0, colon);
                options.port = atoi(value.c_str() + colon + 1);
            } else {
                options.listen = value;
            }
        } else if (flag == "--port") {
            string value;
            if (!next(value))
                return false;
            options.port = atoi(value.c_str());
        } else if (flag == "--port-out") {
            if (!next(options.port_out))
                return false;
        } else if (flag == "--no-curve") {
            options.no_curve = true;
        } else if (flag == "--allow-writer") {
            options.explicit_allow = true;
            // <pattern>=<writer-id>[,<writer-id>...]
            string value;
            if (!next(value))
                return false;
            const auto equals = value.find('=');
            if (equals == string::npos) {
                error = "--allow-writer needs <pattern>=<id>[,<id>]: " + value;
                return false;
            }
            vector<WriterId> ids;
            stringstream list(value.substr(equals + 1));
            string id;
            while (getline(list, id, ','))
                if (!id.empty())
                    ids.push_back(id);
            if (ids.empty()) {
                error = "--allow-writer needs at least one writer id: " + value;
                return false;
            }
            options.allow.emplace_back(value.substr(0, equals), move(ids));
        } else if (flag == "--peer") {
            options.explicit_peers = true;
            ZmqPeerEndpoint peer;
            string value;
            if (!next(value))
                return false;
            if (!parse_peer(value, peer, error))
                return false;
            options.peers.push_back(peer);
        } else {
            error = "unknown option: " + flag;
            return false;
        }
    }
    return apply_config(options, error);
}

/// The transport only needs one endpoint pair per peer, so a peer's PUB and ROUTER ports are given
/// as "port,port" on the command line in real use; for the demo both live on the same port range.
struct Runtime {
    // Order matters: Node holds references to everything above it, so it is constructed last and
    // destroyed first. Passing temporaries here is a dangling reference, not a style question.
    unique_ptr<SqliteLogStore> store;
    unique_ptr<ZmqTransport> transport;
    // Held by pointer, not by value: Runtime is moved out of make_runtime, and a moved member would
    // relocate underneath the Node's references.
    unique_ptr<Sha256Hasher> hasher;
    unique_ptr<Ed25519Verifier> verifier;
    unique_ptr<ChannelRules> rules;  // outlives the node, which holds a pointer to it
    unique_ptr<Node> node;
};

optional<Runtime> make_runtime(const Options& options, vector<string> patterns, string& error) {
    const auto keystore = load_keystore(options.keys);
    if (!keystore) {
        error = "cannot load keys: " + options.keys + " (run `Mosaic keygen " + options.keys + "`)";
        return nullopt;
    }
    auto store = SqliteLogStore::open(options.store);
    if (!store) {
        error = "cannot open store: " + options.store;
        return nullopt;
    }

    ZmqConfig config;
    config.id = options.node;
    config.pub_bind = "tcp://" + options.listen + ":" + std::to_string(options.port);
    config.router_bind = "tcp://" + options.listen + ":" + std::to_string(options.port + 1);
    // Peers are configured with one host:port; the second socket lives on port+1. Resolve that here
    // so the transport never has to guess.
    for (const auto& peer : options.peers) {
        ZmqPeerEndpoint adjusted = peer;
        const auto colon = adjusted.pub_endpoint.rfind(':');
        adjusted.router_endpoint = adjusted.pub_endpoint;
        if (colon != string::npos) {
            const auto port = atoi(adjusted.pub_endpoint.c_str() + colon + 1);
            adjusted.router_endpoint =
                adjusted.pub_endpoint.substr(0, colon + 1) + std::to_string(port + 1);
        }
        config.peers.push_back(adjusted);
    }

    const auto curve = curve_identity_from_keystore(*keystore);
    if (curve && !options.no_curve) {
        config.curve = ZmqCurveConfig{true, curve->public_z85, curve->secret_z85, ""};
    }

    Runtime runtime;
    runtime.store = move(*store);
    runtime.transport = make_unique<ZmqTransport>(config);
    if (!runtime.transport->is_bound()) {
        error =
            "cannot bind " + config.pub_bind + " (port in use, or not an address of this machine)";
        return nullopt;
    }

    runtime.hasher = make_unique<Sha256Hasher>();
    runtime.verifier = make_unique<Ed25519Verifier>();
    if (!options.allow.empty()) {
        runtime.rules = make_unique<ChannelRules>();
        for (const auto& peer : options.peers)
            cout << "peer " << peer.id << " " << peer.pub_endpoint << " curve "
                 << (peer.curve_public_z85.empty() ? "(none)" : peer.curve_public_z85) << '\n';
        for (const auto& [pattern, writers] : options.allow) {
            if (!runtime.rules->allow(pattern, writers)) {
                error = "invalid --allow-writer pattern: " + pattern;
                return nullopt;
            }
        }
    }
    NodeConfig node_config;
    node_config.id = options.node;
    // Announce heads quickly. A CLI process is short-lived, and a subscriber that started *before*
    // this writer existed may never receive the live records at all; the head announcement is what
    // lets it repair, so waiting the default second would mean never announcing before exiting.
    node_config.head_interval_ms = 250;
    runtime.node = make_unique<Node>(node_config, *runtime.transport, *runtime.store,
                                     *runtime.hasher, *runtime.verifier);
    if (runtime.rules)
        runtime.node->set_rules(runtime.rules.get());
    for (const auto& pattern : patterns)
        if (!runtime.node->follow(pattern)) {
            error = "invalid pattern: " + pattern;
            return nullopt;
        }
    return runtime;
}

int run_pub(int argc, char** argv) {
    if (argc < 3)
        return usage();
    const string channel = argv[2];
    Options options;
    string error;
    if (!parse_options(argc, argv, 3, options, error)) {
        cerr << error << '\n';
        return 2;
    }

    auto runtime = make_runtime(options, {}, error);
    if (!runtime) {
        cerr << error << '\n';
        return 1;
    }
    const auto keystore = load_keystore(options.keys);
    Ed25519Signer signer(keystore->public_key, keystore->secret_key);

    if (!options.port_out.empty()) {
        ofstream(options.port_out) << runtime->transport->pub_endpoint() << '\n'
                                   << runtime->transport->router_endpoint() << '\n';
    }
    cout << "identity " << to_hex(keystore->public_key) << '\n';
    cout << "curve " << curve_identity_from_keystore(*keystore)->public_z85 << '\n';
    for (const auto& peer : options.peers)
        cout << "peer " << peer.id << " " << peer.pub_endpoint << " curve "
             << (peer.curve_public_z85.empty() ? "(none)" : peer.curve_public_z85) << '\n';
    for (const auto& [pattern, writers] : options.allow) {
        cout << "allow " << pattern << " =";
        for (const auto& writer : writers)
            cout << ' ' << writer;
        cout << '\n';
    }

    uint64_t now = 0;
    string line;
    while (getline(cin, line)) {
        now += 1;
        const auto record = runtime->node->publish(signer, channel, "text/plain",
                                                   Bytes(line.begin(), line.end()), now);
        if (!record) {
            cerr << "refused: payload exceeds the record size limit\n";
            return 1;
        }
        cout << "seq " << record->seq << " hash " << to_hex(record_hash_tagged(*record).value())
             << '\n';
        cout.flush();
        // Give the live path a moment to reach subscribers before the next line is published.
        for (int i = 0; i < 10; ++i) {
            runtime->node->pump(now);
            runtime->node->tick(now);
            this_thread::sleep_for(chrono::milliseconds(10));
        }
        if (g_stop)
            break;
    }

    // Linger before exiting. The live path is best effort, and a SUB that was still connecting when
    // we published has missed the record; only a HEAD announcement lets it repair, so give the
    // writer a moment to announce. `pump` matters as much as `tick` here: with CURVE the incoming
    // handshake is serviced from the poll loop, so a tick-only linger leaves a later subscriber
    // unable to connect at all.
    for (int i = 0; i < 60 && !g_stop; ++i) {
        now += 10;
        runtime->node->pump(now);
        runtime->node->tick(now);
        this_thread::sleep_for(chrono::milliseconds(10));
    }
    return 0;
}

int run_tail(int argc, char** argv) {
    if (argc < 3)
        return usage();
    vector<string> patterns;
    stringstream stream(argv[2]);
    string pattern;
    while (getline(stream, pattern, ','))
        patterns.push_back(pattern);

    Options options;
    string error;
    if (!parse_options(argc, argv, 3, options, error)) {
        cerr << error << '\n';
        return 2;
    }

    auto runtime = make_runtime(options, patterns, error);
    if (!runtime) {
        cerr << error << '\n';
        return 1;
    }
    const auto keystore = load_keystore(options.keys);
    if (!options.port_out.empty()) {
        ofstream(options.port_out) << runtime->transport->pub_endpoint() << '\n'
                                   << runtime->transport->router_endpoint() << '\n';
    }
    cout << "identity " << to_hex(keystore->public_key) << '\n';
    cout << "curve " << curve_identity_from_keystore(*keystore)->public_z85 << '\n';
    for (const auto& peer : options.peers)
        cout << "peer " << peer.id << " " << peer.pub_endpoint << " curve "
             << (peer.curve_public_z85.empty() ? "(none)" : peer.curve_public_z85) << '\n';
    for (const auto& [pattern, writers] : options.allow) {
        cout << "allow " << pattern << " =";
        for (const auto& writer : writers)
            cout << ' ' << writer;
        cout << '\n';
    }
    cout.flush();

    // Replay what the durable store already holds, before live delivery starts. The node only
    // reports records it *newly* accepts, so a restarted tail would otherwise start mid-history
    // (task 6.3).
    Matcher replay;
    for (const auto& pattern : patterns)
        replay.add(pattern);
    for (const auto& key : runtime->store->keys()) {
        if (!replay.matches_any(key.channel))
            continue;
        for (const auto& record : runtime->store->read(key, 0, numeric_limits<size_t>::max())) {
            cout << record.channel << ' ' << record.seq << ' '
                 << string(record.payload.begin(), record.payload.end()) << '\n';
        }
    }
    cout.flush();

    runtime->node->on_record([](const Record& record) {
        cout << record.channel << ' ' << record.seq << ' '
             << string(record.payload.begin(), record.payload.end()) << '\n';
        cout.flush();
    });

    // Give the subscriptions a moment to reach the publishers before announcing readiness: the
    // socket layer forwards subscriptions asynchronously, so "started" and "listening" are not the
    // same moment.
    uint64_t now = 0;
    for (int i = 0; i < 30; ++i) {
        now += 10;
        runtime->node->pump(now);
        runtime->node->tick(now);
        this_thread::sleep_for(chrono::milliseconds(10));
    }
    cout << "ready\n";
    cout.flush();

    while (!g_stop) {
        now += 10;
        runtime->node->pump(now);
        runtime->node->tick(now);
        this_thread::sleep_for(chrono::milliseconds(10));
    }
    return 0;
}

/// `Mosaic sim`: the convergence self-check, runnable without a network or a test binary. It uses
/// the same SimNetwork the suite uses, so the operator is exercising the shipped code path
/// (task 6.5).
int run_sim(int argc, char** argv) {
    int seeds = 8;
    int records = 200;
    for (int i = 2; i < argc; ++i) {
        const string flag = argv[i];
        if (flag == "--seeds" && i + 1 < argc)
            seeds = atoi(argv[++i]);
        else if (flag == "--records" && i + 1 < argc)
            records = atoi(argv[++i]);
        else
            return usage();
    }

    // Real keys, not the test double: the CLI must never link the insecure signer.
    const auto keys = ed25519_generate();
    if (!keys) {
        cerr << "sim needs crypto support (MOSAIC_WITH_SODIUM=OFF)\n";
        return 1;
    }
    const Ed25519Signer signer(keys->first, keys->second);
    const Ed25519Verifier verifier;

    int converged = 0;
    for (int seed = 1; seed <= seeds; ++seed) {
        SimNetwork net(static_cast<uint64_t>(seed));
        net.set_loss(0.3);
        net.set_duplicate(0.1);
        net.set_delay(0, 120);

        Sha256Hasher hasher;
        MemoryLogStore writer_store, follower_store;
        SimTransport writer_net(net, "writer"), follower_net(net, "follower");
        Node writer(NodeConfig{.id = "writer"}, writer_net, writer_store, hasher, verifier);
        Node follower(NodeConfig{.id = "follower"}, follower_net, follower_store, hasher, verifier);
        int delivered = 0;
        follower.on_record([&delivered](const Record&) { ++delivered; });
        follower.follow("events.>");

        uint64_t now = 0;
        int published = 0;
        for (int step = 0; step < 4000 && delivered < records; ++step) {
            now += 50;
            if (published < records) {
                static_cast<void>(writer.publish(
                    signer, "events.a", "text/plain",
                    Bytes(std::to_string(published).begin(), std::to_string(published).end()),
                    now));
                ++published;
            }
            net.advance(now);
            writer.pump(now);
            writer.tick(now);
            follower.pump(now);
            follower.tick(now);
        }
        const auto wh = writer.head({signer.id(), "events.a"});
        const auto fh = follower.head({signer.id(), "events.a"});
        const bool ok =
            wh && fh && wh->seq == fh->seq && wh->hash == fh->hash && delivered == records;
        if (ok)
            ++converged;
        else
            cout << "seed " << seed << " did not converge (delivered " << delivered << " of "
                 << records << ", dropped " << net.dropped() << ")\n";
    }
    cout << converged << "/" << seeds
         << " seeds converged under 30% loss, 10% duplication and jitter\n";
    return converged == seeds ? 0 : 1;
}

/// `Mosaic verify <store>`: re-check every chain and signature in a store, and say where it stops
/// being true.
int run_verify(int argc, char** argv) {
    if (argc != 3)
        return usage();
    const string path = argv[2];
    auto store = SqliteLogStore::open(path);
    if (!store) {
        cerr << "cannot open store: " << path << '\n';
        return 1;
    }
    const Ed25519Verifier verifier;
    const auto report = verify_store(**store, verifier);
    if (!report.ok()) {
        cerr << "FAILED " << report.failure->key.writer << ' ' << report.failure->key.channel
             << " seq " << report.failure->seq << ": " << report.failure->reason << '\n';
        return 1;
    }
    cout << "verified " << report.records << " records in " << report.logs << " logs\n";
    return 0;
}

int run_keygen(int argc, char** argv) {
    if (argc != 3)
        return usage();
    const auto keystore = generate_keystore();
    if (!keystore) {
        cerr << "this build has no crypto support (MOSAIC_WITH_SODIUM=OFF)\n";
        return 1;
    }
    if (!save_keystore(*keystore, argv[2])) {
        cerr << "cannot write " << argv[2] << '\n';
        return 1;
    }
    cout << "identity " << keystore->id() << '\n';
    if (const auto curve = curve_identity_from_keystore(*keystore))
        cout << "curve " << curve->public_z85 << '\n';
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    if (argc < 2)
        return usage();
    const string command = argv[1];

    if (command == "version") {
        cout << "Mosaic, version " << kVersion << '\n';
        return 0;
    }
    if (command == "match" && argc == 4) {
        mosaic::Matcher matcher;
        if (!matcher.add(argv[2])) {
            cerr << "invalid pattern: " << argv[2] << '\n';
            return 2;
        }
        const bool hit = matcher.matches_any(argv[3]);
        cout << (hit ? "match" : "no match") << '\n';
        return hit ? 0 : 1;
    }
    if (command == "keygen")
        return run_keygen(argc, argv);
    if (command == "pub")
        return run_pub(argc, argv);
    if (command == "tail")
        return run_tail(argc, argv);
    if (command == "verify")
        return run_verify(argc, argv);
    if (command == "sim")
        return run_sim(argc, argv);
    return usage();
}

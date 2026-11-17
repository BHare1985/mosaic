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

#include <chrono>
#include <csignal>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <mosaic/core/keystore.h>
#include <mosaic/log/log.h>
#include <mosaic/log/verify.h>
#include <mosaic/match/matcher.h>
#include <mosaic/net/sim_network.h>
#include <mosaic/net/zmq_transport.h>
#include <mosaic/node/node.h>
#include <mosaic/store/memory_log_store.h>
#include <mosaic/store/sqlite_log_store.h>
#include <mosaic/version.h>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

using namespace mosaic;

namespace {

volatile std::sig_atomic_t g_stop = 0;
void on_signal(int) { g_stop = 1; }

int usage()
{
    std::cerr << "usage:\n"
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
                 "         --no-curve                             diagnostic: run the link in plaintext\n";
    return 2;
}

struct Options {
    std::string node = "node";
    std::string keys = "identity.key";
    std::string store = "mosaic.db";
    std::string listen = "127.0.0.1";
    int port = 0; // 0 = pick a free port and report it
    std::vector<ZmqPeerEndpoint> peers;
    std::vector<std::pair<std::string, std::vector<WriterId>>> allow; // channel rules
    bool no_curve = false; // diagnostic: run the link in plaintext to isolate CURVE issues
    std::string port_out; // file to write the chosen ports to, so a test can discover them
};

/// "<id>=<host:port>,<curve-z85>" -> a peer endpoint. The curve key is optional so plaintext demos work.
bool parse_peer(const std::string& text, ZmqPeerEndpoint& out, std::string& error)
{
    const auto equals = text.find('=');
    const auto comma = text.find(',');
    if (equals == std::string::npos || (comma != std::string::npos && comma < equals)) {
        error = "peer must be <id>=<host:port>[,<curve-z85>]: " + text;
        return false;
    }
    out.id = text.substr(0, equals);
    const auto endpoint = text.substr(equals + 1, comma == std::string::npos ? std::string::npos : comma - equals - 1);
    out.pub_endpoint = "tcp://" + endpoint;
    out.router_endpoint = out.pub_endpoint; // both sockets bind the same host; the port pairs are given below
    if (comma != std::string::npos)
        out.curve_public_z85 = text.substr(comma + 1);
    return true;
}

bool parse_options(int argc, char** argv, int first, Options& options, std::string& error)
{
    for (int i = first; i < argc; ++i) {
        const std::string flag = argv[i];
        const auto next = [&](std::string& into) {
            if (i + 1 >= argc) {
                error = "missing value for " + flag;
                return false;
            }
            into = argv[++i];
            return true;
        };
        if (flag == "--node") {
            if (!next(options.node))
                return false;
        } else if (flag == "--keys") {
            if (!next(options.keys))
                return false;
        } else if (flag == "--store") {
            if (!next(options.store))
                return false;
        } else if (flag == "--listen") {
            if (!next(options.listen))
                return false;
        } else if (flag == "--port") {
            std::string value;
            if (!next(value))
                return false;
            options.port = std::atoi(value.c_str());
        } else if (flag == "--port-out") {
            if (!next(options.port_out))
                return false;
        } else if (flag == "--no-curve") {
            options.no_curve = true;
        } else if (flag == "--allow-writer") {
            // <pattern>=<writer-id>[,<writer-id>...]
            std::string value;
            if (!next(value))
                return false;
            const auto equals = value.find('=');
            if (equals == std::string::npos) {
                error = "--allow-writer needs <pattern>=<id>[,<id>]: " + value;
                return false;
            }
            std::vector<WriterId> ids;
            std::stringstream list(value.substr(equals + 1));
            std::string id;
            while (std::getline(list, id, ','))
                if (!id.empty())
                    ids.push_back(id);
            if (ids.empty()) {
                error = "--allow-writer needs at least one writer id: " + value;
                return false;
            }
            options.allow.emplace_back(value.substr(0, equals), std::move(ids));
        } else if (flag == "--peer") {
            ZmqPeerEndpoint peer;
            std::string value;
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
    return true;
}

/// The transport only needs one endpoint pair per peer, so a peer's PUB and ROUTER ports are given as
/// "port,port" on the command line in real use; for the demo both live on the same port range.
struct Runtime {
    // Order matters: Node holds references to everything above it, so it is constructed last and
    // destroyed first. Passing temporaries here is a dangling reference, not a style question.
    std::unique_ptr<SqliteLogStore> store;
    std::unique_ptr<ZmqTransport> transport;
    // Held by pointer, not by value: Runtime is moved out of make_runtime, and a moved member would
    // relocate underneath the Node's references.
    std::unique_ptr<Sha256Hasher> hasher;
    std::unique_ptr<Ed25519Verifier> verifier;
    std::unique_ptr<ChannelRules> rules; // outlives the node, which holds a pointer to it
    std::unique_ptr<Node> node;
};

std::optional<Runtime> make_runtime(const Options& options, std::vector<std::string> patterns, std::string& error)
{
    const auto keystore = load_keystore(options.keys);
    if (!keystore) {
        error = "cannot load keys: " + options.keys + " (run `Mosaic keygen " + options.keys + "`)";
        return std::nullopt;
    }
    auto store = SqliteLogStore::open(options.store);
    if (!store) {
        error = "cannot open store: " + options.store;
        return std::nullopt;
    }

    ZmqConfig config;
    config.id = options.node;
    config.pub_bind = "tcp://" + options.listen + ":" + std::to_string(options.port);
    config.router_bind = "tcp://" + options.listen + ":" + std::to_string(options.port + 1);
    // Peers are configured with one host:port; the second socket lives on port+1. Resolve that here so
    // the transport never has to guess.
    for (const auto& peer : options.peers) {
        ZmqPeerEndpoint adjusted = peer;
        const auto colon = adjusted.pub_endpoint.rfind(':');
        adjusted.router_endpoint = adjusted.pub_endpoint;
        if (colon != std::string::npos) {
            const auto port = std::atoi(adjusted.pub_endpoint.c_str() + colon + 1);
            adjusted.router_endpoint = adjusted.pub_endpoint.substr(0, colon + 1) + std::to_string(port + 1);
        }
        config.peers.push_back(adjusted);
    }

    const auto curve = curve_identity_from_keystore(*keystore);
    if (curve && !options.no_curve) {
        config.curve = ZmqCurveConfig { true, curve->public_z85, curve->secret_z85, "" };
    }

    Runtime runtime;
    runtime.store = std::move(*store);
    runtime.transport = std::make_unique<ZmqTransport>(config);

    runtime.hasher = std::make_unique<Sha256Hasher>();
    runtime.verifier = std::make_unique<Ed25519Verifier>();
    if (!options.allow.empty()) {
        runtime.rules = std::make_unique<ChannelRules>();
        for (const auto& [pattern, writers] : options.allow) {
            if (!runtime.rules->allow(pattern, writers)) {
                error = "invalid --allow-writer pattern: " + pattern;
                return std::nullopt;
            }
        }
    }
    NodeConfig node_config;
    node_config.id = options.node;
    // Announce heads quickly. A CLI process is short-lived, and a subscriber that started *before* this
    // writer existed may never receive the live records at all; the head announcement is what lets it
    // repair, so waiting the default second would mean never announcing before exiting.
    node_config.head_interval_ms = 250;
    runtime.node = std::make_unique<Node>(
        node_config, *runtime.transport, *runtime.store, *runtime.hasher, *runtime.verifier);
    if (runtime.rules)
        runtime.node->set_rules(runtime.rules.get());
    for (const auto& pattern : patterns)
        if (!runtime.node->follow(pattern)) {
            error = "invalid pattern: " + pattern;
            return std::nullopt;
        }
    return runtime;
}

int run_pub(int argc, char** argv)
{
    if (argc < 3)
        return usage();
    const std::string channel = argv[2];
    Options options;
    std::string error;
    if (!parse_options(argc, argv, 3, options, error)) {
        std::cerr << error << '\n';
        return 2;
    }

    auto runtime = make_runtime(options, { }, error);
    if (!runtime) {
        std::cerr << error << '\n';
        return 1;
    }
    const auto keystore = load_keystore(options.keys);
    Ed25519Signer signer(keystore->public_key, keystore->secret_key);

    if (!options.port_out.empty()) {
        std::ofstream(options.port_out) << runtime->transport->pub_endpoint() << '\n'
                                        << runtime->transport->router_endpoint() << '\n';
    }
    std::cout << "identity " << to_hex(keystore->public_key) << '\n';
    std::cout << "curve " << curve_identity_from_keystore(*keystore)->public_z85 << '\n';
    for (const auto& [pattern, writers] : options.allow) {
        std::cout << "allow " << pattern << " =";
        for (const auto& writer : writers)
            std::cout << ' ' << writer;
        std::cout << '\n';
    }

    std::uint64_t now = 0;
    std::string line;
    while (std::getline(std::cin, line)) {
        now += 1;
        const auto record = runtime->node->publish(signer, channel, "text/plain",
            Bytes(line.begin(), line.end()), now);
        if (!record) {
            std::cerr << "refused: payload exceeds the record size limit\n";
            return 1;
        }
        std::cout << "seq " << record->seq << " hash " << to_hex(record_hash_tagged(*record).value()) << '\n';
        std::cout.flush();
        // Give the live path a moment to reach subscribers before the next line is published.
        for (int i = 0; i < 10; ++i) {
            runtime->node->pump(now);
            runtime->node->tick(now);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        if (g_stop)
            break;
    }

    // Linger before exiting. The live path is best effort, and a SUB that was still connecting when we
    // published has missed the record; only a HEAD announcement lets it repair, so give the writer a
    // moment to announce. `pump` matters as much as `tick` here: with CURVE the incoming handshake is
    // serviced from the poll loop, so a tick-only linger leaves a later subscriber unable to connect at all.
    for (int i = 0; i < 60 && !g_stop; ++i) {
        now += 10;
        runtime->node->pump(now);
        runtime->node->tick(now);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return 0;
}

int run_tail(int argc, char** argv)
{
    if (argc < 3)
        return usage();
    std::vector<std::string> patterns;
    std::stringstream stream(argv[2]);
    std::string pattern;
    while (std::getline(stream, pattern, ','))
        patterns.push_back(pattern);

    Options options;
    std::string error;
    if (!parse_options(argc, argv, 3, options, error)) {
        std::cerr << error << '\n';
        return 2;
    }

    auto runtime = make_runtime(options, patterns, error);
    if (!runtime) {
        std::cerr << error << '\n';
        return 1;
    }
    const auto keystore = load_keystore(options.keys);
    if (!options.port_out.empty()) {
        std::ofstream(options.port_out) << runtime->transport->pub_endpoint() << '\n'
                                        << runtime->transport->router_endpoint() << '\n';
    }
    std::cout << "identity " << to_hex(keystore->public_key) << '\n';
    std::cout << "curve " << curve_identity_from_keystore(*keystore)->public_z85 << '\n';
    for (const auto& [pattern, writers] : options.allow) {
        std::cout << "allow " << pattern << " =";
        for (const auto& writer : writers)
            std::cout << ' ' << writer;
        std::cout << '\n';
    }
    std::cout.flush();

    // Replay what the durable store already holds, before live delivery starts. The node only reports
    // records it *newly* accepts, so a restarted tail would otherwise start mid-history (task 6.3).
    Matcher replay;
    for (const auto& pattern : patterns)
        replay.add(pattern);
    for (const auto& key : runtime->store->keys()) {
        if (!replay.matches_any(key.channel))
            continue;
        for (const auto& record : runtime->store->read(key, 0, std::numeric_limits<std::size_t>::max())) {
            std::cout << record.channel << ' ' << record.seq << ' '
                      << std::string(record.payload.begin(), record.payload.end()) << '\n';
        }
    }
    std::cout.flush();

    runtime->node->on_record([](const Record& record) {
        std::cout << record.channel << ' ' << record.seq << ' '
                  << std::string(record.payload.begin(), record.payload.end()) << '\n';
        std::cout.flush();
    });

    // Give the subscriptions a moment to reach the publishers before announcing readiness: the socket
    // layer forwards subscriptions asynchronously, so "started" and "listening" are not the same moment.
    std::uint64_t now = 0;
    for (int i = 0; i < 30; ++i) {
        now += 10;
        runtime->node->pump(now);
        runtime->node->tick(now);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    std::cout << "ready\n";
    std::cout.flush();

    while (!g_stop) {
        now += 10;
        runtime->node->pump(now);
        runtime->node->tick(now);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return 0;
}

/// `Mosaic sim`: the convergence self-check, runnable without a network or a test binary. It uses the
/// same SimNetwork the suite uses, so the operator is exercising the shipped code path (task 6.5).
int run_sim(int argc, char** argv)
{
    int seeds = 8;
    int records = 200;
    for (int i = 2; i < argc; ++i) {
        const std::string flag = argv[i];
        if (flag == "--seeds" && i + 1 < argc)
            seeds = std::atoi(argv[++i]);
        else if (flag == "--records" && i + 1 < argc)
            records = std::atoi(argv[++i]);
        else
            return usage();
    }

    // Real keys, not the test double: the CLI must never link the insecure signer.
    const auto keys = ed25519_generate();
    if (!keys) {
        std::cerr << "sim needs crypto support (MOSAIC_WITH_SODIUM=OFF)\n";
        return 1;
    }
    const Ed25519Signer signer(keys->first, keys->second);
    const Ed25519Verifier verifier;

    int converged = 0;
    for (int seed = 1; seed <= seeds; ++seed) {
        SimNetwork net(static_cast<std::uint64_t>(seed));
        net.set_loss(0.3);
        net.set_duplicate(0.1);
        net.set_delay(0, 120);

        Sha256Hasher hasher;
        MemoryLogStore writer_store, follower_store;
        SimTransport writer_net(net, "writer"), follower_net(net, "follower");
        Node writer(NodeConfig { .id = "writer" }, writer_net, writer_store, hasher, verifier);
        Node follower(NodeConfig { .id = "follower" }, follower_net, follower_store, hasher, verifier);
        int delivered = 0;
        follower.on_record([&delivered](const Record&) { ++delivered; });
        follower.follow("events.>");

        std::uint64_t now = 0;
        int published = 0;
        for (int step = 0; step < 4000 && delivered < records; ++step) {
            now += 50;
            if (published < records) {
                static_cast<void>(writer.publish(signer, "events.a", "text/plain",
                    Bytes(std::to_string(published).begin(), std::to_string(published).end()), now));
                ++published;
            }
            net.advance(now);
            writer.pump(now);
            writer.tick(now);
            follower.pump(now);
            follower.tick(now);
        }
        const auto wh = writer.head({ signer.id(), "events.a" });
        const auto fh = follower.head({ signer.id(), "events.a" });
        const bool ok = wh && fh && wh->seq == fh->seq && wh->hash == fh->hash && delivered == records;
        if (ok)
            ++converged;
        else
            std::cout << "seed " << seed << " did not converge (delivered " << delivered << " of " << records
                      << ", dropped " << net.dropped() << ")\n";
    }
    std::cout << converged << "/" << seeds << " seeds converged under 30% loss, 10% duplication and jitter\n";
    return converged == seeds ? 0 : 1;
}

/// `Mosaic verify <store>`: re-check every chain and signature in a store, and say where it stops being true.
int run_verify(int argc, char** argv)
{
    if (argc != 3)
        return usage();
    const std::string path = argv[2];
    auto store = SqliteLogStore::open(path);
    if (!store) {
        std::cerr << "cannot open store: " << path << '\n';
        return 1;
    }
    const Ed25519Verifier verifier;
    const auto report = verify_store(**store, verifier);
    if (!report.ok()) {
        std::cerr << "FAILED " << report.failure->key.writer << ' ' << report.failure->key.channel << " seq "
                  << report.failure->seq << ": " << report.failure->reason << '\n';
        return 1;
    }
    std::cout << "verified " << report.records << " records in " << report.logs << " logs\n";
    return 0;
}

int run_keygen(int argc, char** argv)
{
    if (argc != 3)
        return usage();
    const auto keystore = generate_keystore();
    if (!keystore) {
        std::cerr << "this build has no crypto support (MOSAIC_WITH_SODIUM=OFF)\n";
        return 1;
    }
    if (!save_keystore(*keystore, argv[2])) {
        std::cerr << "cannot write " << argv[2] << '\n';
        return 1;
    }
    std::cout << "identity " << keystore->id() << '\n';
    if (const auto curve = curve_identity_from_keystore(*keystore))
        std::cout << "curve " << curve->public_z85 << '\n';
    return 0;
}

} // namespace

int main(int argc, char** argv)
{
    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);

    if (argc < 2)
        return usage();
    const std::string command = argv[1];

    if (command == "version") {
        std::cout << "Mosaic, version " << MOSAIC_VERSION << '\n';
        return 0;
    }
    if (command == "match" && argc == 4) {
        mosaic::Matcher matcher;
        if (!matcher.add(argv[2])) {
            std::cerr << "invalid pattern: " << argv[2] << '\n';
            return 2;
        }
        const bool hit = matcher.matches_any(argv[3]);
        std::cout << (hit ? "match" : "no match") << '\n';
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

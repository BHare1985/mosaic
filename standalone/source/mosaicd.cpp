// mosaicd: the daemon. Builds a node from a ZPL config and runs it until interrupted.
//
//   mosaicd <config>        run
//   mosaicd --check <config> validate and exit (for deployment scripts)
//   mosaicd --defaults       print a commented config generated from the daemon's own defaults
//
// The process owns exactly one thread: the library is single-threaded and time is read once per
// iteration, which is what keeps a deployment's behaviour identical to what the tests assert.

#include <csignal>
#include <iostream>
#include <mosaic/daemon/config.h>
#include <mosaic/daemon/daemon.h>

namespace {
volatile std::sig_atomic_t g_stop = 0;
volatile std::sig_atomic_t g_dump = 0;

// A handler may only set a flag; the loop decides what to do. SIGUSR1 asks for the counters now.
void on_signal(int signal)
{
    if (signal == SIGUSR1)
        g_dump = 1;
    else
        g_stop = 1;
}

int usage()
{
    std::cerr << "usage:\n"
                 "  mosaicd <config>\n"
                 "  mosaicd --check <config>\n"
                 "  mosaicd --defaults\n"
                 "  mosaicd --no-curve <config>   diagnostic: plaintext links\n";
    return 2;
}

int report(const mosaic::daemon::LoadResult& result)
{
    for (const auto& error : result.errors)
        std::cerr << error.where << ": " << error.message << '\n';
    return result.ok() ? 0 : 1;
}
} // namespace

int main(int argc, char** argv)
{
    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);
    std::signal(SIGUSR1, on_signal);

    // Diagnostic: run the link in plaintext, to separate a configuration/delivery problem from a
    // key-material problem. Same flag as the CLI's.
    bool no_curve = false;
    int first = 1;
    if (argc > 1 && std::string(argv[1]) == "--no-curve") {
        no_curve = true;
        first = 2;
    }
    if (argc == first + 1 && std::string(argv[first]) == "--defaults") {
        std::cout << mosaic::daemon::render_defaults();
        return 0;
    }
    if (argc == first + 2 && std::string(argv[first]) == "--check")
        return report(mosaic::daemon::load_config(argv[first + 1]));
    if (argc != first + 1)
        return usage();

    auto result = mosaic::daemon::load_config(argv[first]);
    if (no_curve)
        result.config.curve = false;
    if (!result.ok())
        return report(result); // every problem at once, before a socket or a store is opened

    std::string error;
    auto daemon = mosaic::daemon::Daemon::create(result.config, error);
    if (!daemon) {
        std::cerr << "cannot start: " << error << '\n';
        return 1;
    }

    std::cout << "node " << result.config.id << " pub " << daemon->pub_endpoint() << " router "
              << daemon->router_endpoint() << " curve " << (daemon->curve_enabled() ? "on" : "off") << '\n';
    for (const auto& pattern : result.config.follows)
        std::cout << "follow " << pattern << '\n';
    for (const auto& rule : result.config.allow) {
        std::cout << "allow " << rule.pattern << " =";
        for (const auto& writer : rule.writers)
            std::cout << ' ' << writer;
        std::cout << '\n';
    }
    if (std::string(daemon->discovery_kind()) != "none")
        std::cout << "discover " << daemon->discovery_kind() << " cluster " << result.config.discover.cluster << '\n';
    if (daemon->curve_enabled())
        std::cout << "curve " << daemon->curve_public() << '\n';
    for (const auto& peer : result.config.peers)
        std::cout << "peer " << peer.id << " curve " << (peer.curve_public.empty() ? "(none)" : peer.curve_public)
                  << '\n';
    if (result.config.min_replicas != 0)
        std::cout << "min_replicas " << result.config.min_replicas << '\n';
    std::cout.flush();

    const auto outcome = daemon->run([] { return g_stop != 0; }, [&daemon] {
        if (g_dump != 0) {
            g_dump = 0;
            daemon->dump_status();
        } });
    std::cout << "stopped" << '\n';
    return outcome == mosaic::daemon::RunResult::Stopped ? 0 : 1;
}

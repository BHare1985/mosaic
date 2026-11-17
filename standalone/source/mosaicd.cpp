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
void on_signal(int) { g_stop = 1; }

int usage()
{
    std::cerr << "usage:\n"
                 "  mosaicd <config>\n"
                 "  mosaicd --check <config>\n"
                 "  mosaicd --defaults\n";
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

    if (argc == 2 && std::string(argv[1]) == "--defaults") {
        std::cout << mosaic::daemon::render_defaults();
        return 0;
    }
    if (argc == 3 && std::string(argv[1]) == "--check")
        return report(mosaic::daemon::load_config(argv[2]));
    if (argc != 2)
        return usage();

    const auto result = mosaic::daemon::load_config(argv[1]);
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
    if (result.config.min_replicas != 0)
        std::cout << "min_replicas " << result.config.min_replicas << '\n';
    std::cout.flush();

    const auto outcome = daemon->run([] { return g_stop != 0; });
    std::cout << "stopped" << '\n';
    return outcome == mosaic::daemon::RunResult::Stopped ? 0 : 1;
}

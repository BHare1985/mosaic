#pragma once

// The daemon: a long-running node built from a validated Config, driven by one loop that owns time,
// with a clean shutdown and an operator-visible summary (spec: daemon/mosaicd).

#include <cstdint>
#include <functional>
#include <mosaic/daemon/config.h>
#include <memory>
#include <string>

namespace mosaic {

class Node;
class LogStore;
class Transport;

namespace daemon {

/// Why a daemon stopped, so the process can exit with a useful status.
enum class RunResult {
    Stopped, // asked to stop through the signal flag
    Failed, // a component could not be built from the config
};

class Daemon {
public:
    /// Build the runtime from a validated config. Empty on failure, with the reason in `error`.
    static std::unique_ptr<Daemon> create(const Config& config, std::string& error);

    ~Daemon();
    Daemon(const Daemon&) = delete;
    Daemon& operator=(const Daemon&) = delete;

    /// Run until `stop` returns true. Called once per iteration, so a signal handler can only set a flag.
    RunResult run(const std::function<bool()>& stop);

    /// The counters line an operator reads. Public so a test can assert what it says.
    std::string status_line() const;

    /// Whether links are encrypted. An operator must be able to see this: a daemon that silently runs
    /// plaintext because its keystore was unreadable is the worst possible failure mode.
    bool curve_enabled() const;

    /// Ports actually bound, for a test that binds ephemeral ports.
    std::string pub_endpoint() const;
    std::string router_endpoint() const;

private:
    Daemon();
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace daemon
} // namespace mosaic

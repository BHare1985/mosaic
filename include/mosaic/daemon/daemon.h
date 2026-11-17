#pragma once

// The daemon: a long-running node built from a validated Config, driven by one loop that owns time,
// with a clean shutdown and an operator-visible summary (spec: daemon/mosaicd).

#include <mosaic/daemon/config.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace mosaic {

class Node;
class LogStore;
class Transport;

namespace daemon {

/// Why a daemon stopped, so the process can exit with a useful status.
enum class RunResult {
    Stopped,  // asked to stop through the signal flag
    Failed,   // a component could not be built from the config
};

class Daemon {
public:
    /// Build the runtime from a validated config. Empty on failure, with the reason in `error`.
    static std::unique_ptr<Daemon> create(const Config& config, std::string& error);

    ~Daemon();
    Daemon(const Daemon&) = delete;
    Daemon& operator=(const Daemon&) = delete;

    /// Run until `stop` returns true. `on_iteration`, when given, runs at the end of every
    /// iteration - the place for work a signal handler may only flag (an on-demand status dump).
    RunResult run(const std::function<bool()>& stop,
                  const std::function<void()>& on_iteration = {});

    /// The counters line an operator reads. Public so a test can assert what it says.
    std::string status_line() const;

    /// Print the counters now, for an on-demand dump (SIGUSR1). Cheap and non-blocking: it is the
    /// same line the periodic summary prints.
    void dump_status() const;

    /// How many peers were learned from discovery rather than configuration, and which source is
    /// active.
    std::size_t discovered_peers() const;
    const char* discovery_kind() const;

    /// Whether this daemon's links are actually encrypted. It reports what the running transport
    /// does, not what libzmq supports: a daemon that silently runs plaintext is the worst failure
    /// mode here.
    bool curve_enabled() const;

    /// This node's CURVE public key (empty when links are not encrypted), so an operator can
    /// compare it with what peers were told to expect.
    std::string curve_public() const;

    /// Ports actually bound, for a test that binds ephemeral ports.
    std::string pub_endpoint() const;
    std::string router_endpoint() const;

private:
    Daemon();
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace daemon
}  // namespace mosaic

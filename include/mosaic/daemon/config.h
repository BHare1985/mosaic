#pragma once

// The daemon's configuration: ZPL (ZeroMQ RFC 4) read with czmq's zconfig, validated as a whole before
// anything starts, and printable from the same defaults the parser fills (spec: daemon/mosaicd).

#include <cstdint>
#include <mosaic/core/types.h>
#include <string>
#include <utility>
#include <vector>

namespace mosaic::daemon {

struct PeerSpec {
    NodeId id;
    std::string endpoint; // host:port of the peer's PUB; its ROUTER is port+1
    std::string curve_public;
};

struct RuleSpec {
    std::string pattern;
    std::vector<WriterId> writers;
};

struct DiscoverSpec {
    std::string cluster = "mosaic"; // scopes the beacon and the gossip namespace
    std::uint16_t beacon_port = 0; // UDP port; 0 disables discovery

    bool enabled() const { return beacon_port != 0; }
};

struct Config {
    // node
    NodeId id;
    std::string store;
    std::string keys;
    std::string listen = "127.0.0.1:6200"; // PUB here, ROUTER on the next port
    std::uint32_t head_interval_ms = 1000;
    std::uint32_t fetch_batch = 64;
    std::uint32_t fetch_window = 256;
    std::uint32_t fetch_timeout_ms = 500;
    std::uint32_t max_serve = 1024;
    std::uint32_t min_replicas = 0;
    std::uint32_t retention_limit = 10000;
    std::uint32_t slow_follower_threshold = 512;
    std::uint32_t control_interval_ms = 500;
    std::uint32_t max_record_bytes = 262144;
    std::uint32_t max_head_patterns = 64;
    std::uint32_t max_requests_per_interval = 32;
    // log
    /// Encrypt links. On by default; the only reason to turn it off is a diagnostic or a loopback test,
    /// so it is stated in the config rather than implied by whether a keystore happens to have a key.
    bool curve = true;
    std::string level = "info";
    std::uint32_t status_interval_ms = 10000;

    std::vector<PeerSpec> peers;
    std::vector<std::string> follows;
    std::vector<RuleSpec> allow;
    DiscoverSpec discover;
};

struct ConfigError {
    std::string where; // section and property, so the message names the problem
    std::string message;
};

struct LoadResult {
    Config config;
    std::vector<ConfigError> errors;

    bool ok() const { return errors.empty(); }
};

/// Parse and validate a ZPL config. Every problem is reported, not only the first: an operator should not
/// discover them one restart at a time.
LoadResult load_config(const std::string& path);

/// A commented ZPL document listing every setting and its default, generated from the same table the
/// parser uses to fill absent settings.
std::string render_defaults();

} // namespace mosaic::daemon

// The daemon's configuration (spec: daemon/mosaicd): ZPL, validated whole, defaults printable.
#include <mosaic/daemon/config.h>
#include <mosaic/daemon/daemon.h>
#include <mosaic/node/node.h>

#include <cstdio>
#include <cstdlib>
#include <doctest/doctest.h>
#include <fstream>
#include <string>

using std::ofstream;
using std::remove;
using std::string;

#ifdef MOSAIC_HAS_CZMQ

using namespace mosaic;
using mosaic::daemon::load_config;

namespace {
string write_temp(const char* name, const string& body) {
    const string path = name;
    ofstream out(path, std::ios::trunc);
    out << body;
    return path;
}

const char* kMinimal = R"(# a minimal but complete config
node
    id = alpha
    store = alpha.db
    keys = alpha.key
    min_replicas = 2
peer
    id = beta
    endpoint = 127.0.0.1:6100
follow
    pattern = events.>
allow
    pattern = svc.resize
    writer = abc123
    writer = def456
)";
}  // namespace

TEST_CASE("a valid ZPL config is read, with defaults for what it omits") {
    const auto path = write_temp("test-config-ok.zpl", kMinimal);
    const auto result = load_config(path);
    for (const auto& error : result.errors)
        MESSAGE(error.where << ": " << error.message);
    REQUIRE(result.ok());

    CHECK(result.config.id == "alpha");
    CHECK(result.config.store == "alpha.db");
    CHECK(result.config.min_replicas == 2);
    CHECK(result.config.listen == "127.0.0.1:6200");  // default, not written in the file
    CHECK(result.config.max_record_bytes == 262144);
    REQUIRE(result.config.peers.size() == 1);
    CHECK(result.config.peers.front().id == "beta");
    CHECK(result.config.peers.front().endpoint == "127.0.0.1:6100");
    REQUIRE(result.config.follows.size() == 1);
    CHECK(result.config.follows.front() == "events.>");
    REQUIRE(result.config.allow.size() == 1);
    CHECK(result.config.allow.front().writers.size() == 2);
    remove(path.c_str());
}

TEST_CASE("an unknown setting is an error that names it") {
    const auto path = write_temp("test-config-typo.zpl", R"(node
    id = alpha
    store = alpha.db
    keys = alpha.key
    min_replicaz = 2
)");
    const auto result = load_config(path);
    REQUIRE_FALSE(result.ok());
    bool named = false;
    for (const auto& error : result.errors)
        if (error.where == "node.min_replicaz" && error.message.find("unknown") != string::npos)
            named = true;
    CHECK(named);
    remove(path.c_str());
}

TEST_CASE("every problem is reported at once") {
    const auto path = write_temp("test-config-bad.zpl", R"(node
    store = alpha.db
    max_serve = lots
)");
    const auto result = load_config(path);
    REQUIRE_FALSE(result.ok());
    CHECK(result.errors.size() >= 3);  // missing id, missing keys, and a non-number
    remove(path.c_str());
}

TEST_CASE("an unset environment variable is an error, not an empty value") {
    const auto path = write_temp("test-config-env.zpl", R"(node
    id = alpha
    store = ${MOSAIC_TEST_UNSET_VAR}/alpha.db
    keys = alpha.key
)");
    const auto result = load_config(path);
    REQUIRE_FALSE(result.ok());
    bool named = false;
    for (const auto& error : result.errors)
        if (error.message.find("MOSAIC_TEST_UNSET_VAR") != string::npos)
            named = true;
    CHECK(named);
    remove(path.c_str());
}

TEST_CASE("a set environment variable is expanded") {
    setenv("MOSAIC_TEST_SET_VAR", "/tmp/mosaic", 1);
    const auto path = write_temp("test-config-env2.zpl", R"(node
    id = alpha
    store = ${MOSAIC_TEST_SET_VAR}/alpha.db
    keys = ${MOSAIC_TEST_SET_VAR}/alpha.key
)");
    const auto result = load_config(path);
    REQUIRE(result.ok());
    CHECK(result.config.store == "/tmp/mosaic/alpha.db");
    remove(path.c_str());
    unsetenv("MOSAIC_TEST_SET_VAR");
}

TEST_CASE("the printed defaults are themselves a valid config") {
    // The document `--defaults` prints, with the three required values filled in, must load cleanly
    // - which is what makes the printed defaults authoritative rather than decorative.
    auto body = mosaic::daemon::render_defaults();
    body += "\nnode\n    id = alpha\n    store = alpha.db\n    keys = alpha.key\n";
    const auto path = write_temp("test-config-defaults.zpl", body);
    const auto result = load_config(path);
    for (const auto& error : result.errors)
        MESSAGE(error.where << ": " << error.message);
    CHECK(result.ok());
    remove(path.c_str());
}

TEST_CASE("the scoring limits are expressible in a config, with documented defaults") {
    const auto path = write_temp("test-config-scoring.zpl", R"(node
    id = alpha
    store = alpha.db
    keys = alpha.key
    strike_threshold = 3
    strike_decay_ms = 30000
    ban_base_ms = 2000
    ban_max_ms = 60000
)");
    const auto result = load_config(path);
    for (const auto& error : result.errors)
        MESSAGE(error.where << ": " << error.message);
    REQUIRE(result.ok());
    CHECK(result.config.strike_threshold == 3);
    CHECK(result.config.strike_decay_ms == 30000);
    CHECK(result.config.ban_base_ms == 2000);
    CHECK(result.config.ban_max_ms == 60000);
    remove(path.c_str());

    // Absent, they take the defaults - which are the numbers docs/design/07-security.md quotes.
    const auto bare = write_temp("test-config-scoring-bare.zpl", R"(node
    id = alpha
    store = alpha.db
    keys = alpha.key
)");
    const auto defaults = load_config(bare);
    REQUIRE(defaults.ok());
    CHECK(defaults.config.strike_threshold == 8);
    CHECK(defaults.config.strike_decay_ms == 60000);
    CHECK(defaults.config.ban_base_ms == 5000);
    CHECK(defaults.config.ban_max_ms == 600000);
    remove(bare.c_str());
}

#endif  // MOSAIC_HAS_CZMQ

#ifdef MOSAIC_HAS_CZMQ
TEST_CASE("a quoted curve key survives a '#' in the value, and a truncated one is rejected") {
    // Z85 keys legitimately contain '#', which ZPL treats as a comment unless the value is quoted.
    // This is the failure that made a two-process daemon test silently deliver nothing.
    const auto quoted = write_temp("test-config-quoted.zpl", R"(node
    id = alpha
    store = alpha.db
    keys = alpha.key
peer
    id = beta
    endpoint = 127.0.0.1:6100
    curve = "Wu{9<Z=*2?-]ru6:P*ytm=W(7[pZTYM&7Gv)#.=j"
)");
    const auto good = load_config(quoted);
    REQUIRE(good.ok());
    REQUIRE(good.config.peers.size() == 1);
    CHECK(good.config.peers.front().curve_public == "Wu{9<Z=*2?-]ru6:P*ytm=W(7[pZTYM&7Gv)#.=j");
    remove(quoted.c_str());

    const auto unquoted = write_temp("test-config-unquoted.zpl", R"(node
    id = alpha
    store = alpha.db
    keys = alpha.key
peer
    id = beta
    endpoint = 127.0.0.1:6100
    curve = Wu{9<Z=*2?-]ru6:P*ytm=W(7[pZTYM&7Gv)#.=j
)");
    const auto bad = load_config(unquoted);
    REQUIRE_FALSE(bad.ok());  // truncated at '#' -> 33 chars -> named as an error
    bool named = false;
    for (const auto& error : bad.errors)
        if (error.where == "peer.curve")
            named = true;
    CHECK(named);
    remove(unquoted.c_str());
}
#endif

#ifdef MOSAIC_HAS_CZMQ
TEST_CASE("gossip is configurable, and its defaults are the documented ones") {
    const auto path = write_temp("test-config-gossip.zpl", R"(node
    id = alpha
    store = alpha.db
    keys = alpha.key
    gossip_interval_ms = 1000
    gossip_fanout = 3
    gossip_max_hops = 4
    gossip_entry_ttl_ms = 5000
    gossip_table_limit = 32
)");
    const auto result = load_config(path);
    for (const auto& error : result.errors)
        MESSAGE(error.where << ": " << error.message);
    REQUIRE(result.ok());
    CHECK(result.config.gossip_interval_ms == 1000);
    CHECK(result.config.gossip_fanout == 3);
    CHECK(result.config.gossip_max_hops == 4);
    CHECK(result.config.gossip_entry_ttl_ms == 5000);
    CHECK(result.config.gossip_table_limit == 32);
    remove(path.c_str());

    const auto bare = write_temp("test-config-gossip-bare.zpl", R"(node
    id = alpha
    store = alpha.db
    keys = alpha.key
)");
    const auto defaults = load_config(bare);
    REQUIRE(defaults.ok());
    CHECK(defaults.config.gossip_interval_ms == 5000);
    CHECK(defaults.config.gossip_fanout == 2);
    CHECK(defaults.config.gossip_max_hops == 6);
    CHECK(defaults.config.gossip_entry_ttl_ms == 30000);
    CHECK(defaults.config.gossip_table_limit == 256);
    remove(bare.c_str());
}

TEST_CASE("the daemon reports bans, and every ban edge is one readable line") {
    // What an operator sees: the status line must name the ban counters, and each edge must be a
    // single line that says what happened without making them read a log. Both are pure functions
    // of the values the node reports, so this needs no socket, keystore or running process.
    mosaic::NodeCounters counters;
    counters.accepted = 4;
    counters.bans_started = 2;
    counters.peer_bans = 1;
    counters.banned_drops = 7;
    const auto status = mosaic::daemon::counters_line(counters, 12, 3);
    CHECK(status.find("accepted=4") != string::npos);
    CHECK(status.find("bans=2") != string::npos);
    CHECK(status.find("banned=1") != string::npos);
    CHECK(status.find("banned_drops=7") != string::npos);
    CHECK(status.find("discovered=3") != string::npos);

    using mosaic::BanEdge;
    using mosaic::BanEvent;
    CHECK(mosaic::daemon::ban_line(BanEvent{"beta", BanEdge::Started, 2, 8, 5000}) ==
          "ban peer=beta edge=start duration_ms=5000 strikes=8 bans=2");
    CHECK(mosaic::daemon::ban_line(BanEvent{"beta", BanEdge::Expired, 2, 0, 0}) ==
          "ban peer=beta edge=expired bans=2");
    CHECK(mosaic::daemon::ban_line(BanEvent{"beta", BanEdge::Lifted, 2, 0, 0}) ==
          "ban peer=beta edge=lifted bans=2");
}
#endif

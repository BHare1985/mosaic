// Real czmq actors: do two nodes on the same host find each other without configuration?
#include <chrono>
#include <doctest/doctest.h>
#include <mosaic/net/czmq_discovery.h>
#include <thread>

#ifdef MOSAIC_HAS_CZMQ

using namespace mosaic;

namespace {
DiscoveredPeer self_for(const std::string& id, const std::string& endpoint)
{
    return DiscoveredPeer { id, endpoint, "" };
}

/// Drains both sides for up to `attempts` rounds, returning everything found.
std::vector<DiscoveredPeer> wait_for_candidates(CzmqDiscovery& discovery, int attempts = 40)
{
    std::vector<DiscoveredPeer> found;
    for (int i = 0; i < attempts && found.empty(); ++i) {
        for (auto& peer : discovery.poll_discovered())
            found.push_back(peer);
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    return found;
}
} // namespace

TEST_CASE("two nodes on the same host find each other with the beacon, without configuration")
{
    CzmqDiscoveryConfig left_config;
    left_config.cluster = "mosaic-test-cluster";
    left_config.beacon_port = 4567;
    left_config.beacon_interval_ms = 100;
    // No interface forced: zbeacon uses the host default, which is the one that can broadcast.
    left_config.self = self_for("left", "127.0.0.1:6100");

    CzmqDiscovery left(left_config);
    if (!left.beacon_active()) {
        MESSAGE("this host has no broadcast-capable interface: beacon test skipped");
        return; // an honest skip: a UDP broadcast that cannot run proves nothing
    }

    CzmqDiscoveryConfig right_config = left_config;
    right_config.self = self_for("right", "127.0.0.1:6200");
    CzmqDiscovery right(right_config);

    const auto found = wait_for_candidates(left, 20);
    const auto found_by_right = wait_for_candidates(right, 20);

    const bool seen = !found.empty() || !found_by_right.empty();
    CHECK(seen);
    if (!found.empty()) {
        CHECK(found.front().id == "right");
        CHECK(found.front().endpoint == "127.0.0.1:6200");
    }
}

TEST_CASE("an unrelated broadcaster is ignored rather than half-understood")
{
    // A different cluster name means a different beacon filter: the two must not see each other.
    CzmqDiscoveryConfig other;
    other.cluster = "someone-elses-cluster";
    other.beacon_port = 4568;
    other.beacon_interval_ms = 100;
    other.self = self_for("stranger", "127.0.0.1:6300");
    CzmqDiscovery stranger(other);
    if (!stranger.beacon_active()) {
        MESSAGE("no broadcast-capable interface: isolation test skipped");
        return;
    }

    CzmqDiscoveryConfig mine = other;
    mine.cluster = "mosaic-test-cluster-2";
    mine.self = self_for("mine", "127.0.0.1:6400");
    CzmqDiscovery mine_discovery(mine);

    CHECK(wait_for_candidates(mine_discovery, 8).empty());
}

#endif // MOSAIC_HAS_CZMQ

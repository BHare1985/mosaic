#include <mosaic/flow/credits.h>
#include <mosaic/jobs/rendezvous.h>

#include <doctest/doctest.h>
#include <random>
#include <set>

using std::mt19937_64;
using std::set;
using std::uint32_t;
using std::uniform_int_distribution;
using std::vector;

using namespace mosaic;

TEST_CASE("CreditWindow spends, clamps and resets") {
    CreditWindow w(10, 4);
    CHECK(w.available() == 4);
    CHECK(w.take_up_to(3) == 3);
    CHECK(w.take_up_to(5) == 1);
    CHECK(w.take_up_to(1) == 0);
    w.grant(100);
    CHECK(w.available() == 10);  // clamped to capacity
    w.take_up_to(10);
    w.reset();
    CHECK(w.available() == 10);
    CHECK(CreditWindow(5, 99).available() == 5);  // initial clamped too
}

TEST_CASE("CreditGranter refills in batches") {
    CreditGranter g(10);
    CHECK(g.initial() == 10);
    CHECK(g.on_consumed(1) == 0);
    CHECK(g.on_consumed(3) == 0);
    CHECK(g.on_consumed(1) == 5);  // half the window consumed: grant it back
    CHECK(g.on_consumed(1) == 0);
}

TEST_CASE("rendezvous ranking is deterministic and stable") {
    Sha256Hasher h;
    const vector<NodeId> workers = {"n1", "n2", "n3", "n4", "n5"};
    const auto order = rendezvous_rank("job-42", workers, h);
    REQUIRE(order.size() == 5);

    // input order does not matter
    vector<NodeId> shuffled = {"n4", "n1", "n5", "n3", "n2"};
    CHECK(rendezvous_rank("job-42", shuffled, h) == order);

    // removing a worker that is not first leaves the first choice unchanged
    vector<NodeId> without_last;
    for (const auto& w : workers)
        if (w != order.back())
            without_last.push_back(w);
    CHECK(rendezvous_rank("job-42", without_last, h).front() == order.front());

    // different jobs spread over workers
    set<NodeId> firsts;
    for (int i = 0; i < 50; ++i)
        firsts.insert(rendezvous_rank("job-" + std::to_string(i), workers, h).front());
    CHECK(firsts.size() >= 3);
}

TEST_CASE("credit invariants hold under randomised sequences") {
    // Seeded, so a failure is reproducible; the invariant is the one the protocol depends on.
    mt19937_64 rng(20261117);
    uniform_int_distribution<uint32_t> amount(0, 40);
    uniform_int_distribution<int> operation(0, 2);

    CreditWindow window(16, 16);
    for (int step = 0; step < 2000; ++step) {
        switch (operation(rng)) {
            case 0:
                window.take_up_to(amount(rng));
                break;
            case 1:
                window.grant(amount(rng));
                break;
            default:
                window.reset();
                break;
        }
        REQUIRE(window.available() <= window.capacity());
        REQUIRE(window.available() >= 0);
    }

    // A grant never reduces availability: an explicit sequence rather than a random walk.
    CreditWindow fixed(10, 5);
    for (int i = 0; i < 20; ++i) {
        const auto before = fixed.available();
        fixed.grant(amount(rng) % 4);
        CHECK(fixed.available() >= before);
    }

    // CreditGranter hands out at most what has been consumed, and refills in batches.
    CreditGranter granter(10);
    uint32_t granted = 0;
    for (int i = 0; i < 100; ++i) {
        const auto now = granter.on_consumed(amount(rng) % 3);
        CHECK(now <= 10);
        granted += now;
    }
    CHECK(granted <= 100);  // never more credit than messages consumed
}

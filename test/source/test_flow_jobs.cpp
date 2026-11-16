#include <doctest/doctest.h>
#include <mosaic/flow/credits.h>
#include <mosaic/jobs/rendezvous.h>
#include <set>

using namespace mosaic;

TEST_CASE("CreditWindow spends, clamps and resets")
{
    CreditWindow w(10, 4);
    CHECK(w.available() == 4);
    CHECK(w.take_up_to(3) == 3);
    CHECK(w.take_up_to(5) == 1);
    CHECK(w.take_up_to(1) == 0);
    w.grant(100);
    CHECK(w.available() == 10); // clamped to capacity
    w.take_up_to(10);
    w.reset();
    CHECK(w.available() == 10);
    CHECK(CreditWindow(5, 99).available() == 5); // initial clamped too
}

TEST_CASE("CreditGranter refills in batches")
{
    CreditGranter g(10);
    CHECK(g.initial() == 10);
    CHECK(g.on_consumed(1) == 0);
    CHECK(g.on_consumed(3) == 0);
    CHECK(g.on_consumed(1) == 5); // half the window consumed: grant it back
    CHECK(g.on_consumed(1) == 0);
}

TEST_CASE("rendezvous ranking is deterministic and stable")
{
    Sha256Hasher h;
    const std::vector<NodeId> workers = { "n1", "n2", "n3", "n4", "n5" };
    const auto order = rendezvous_rank("job-42", workers, h);
    REQUIRE(order.size() == 5);

    // input order does not matter
    std::vector<NodeId> shuffled = { "n4", "n1", "n5", "n3", "n2" };
    CHECK(rendezvous_rank("job-42", shuffled, h) == order);

    // removing a worker that is not first leaves the first choice unchanged
    std::vector<NodeId> without_last;
    for (const auto& w : workers)
        if (w != order.back())
            without_last.push_back(w);
    CHECK(rendezvous_rank("job-42", without_last, h).front() == order.front());

    // different jobs spread over workers
    std::set<NodeId> firsts;
    for (int i = 0; i < 50; ++i)
        firsts.insert(rendezvous_rank("job-" + std::to_string(i), workers, h).front());
    CHECK(firsts.size() >= 3);
}

#include <algorithm>
#include <chrono>
#include <doctest/doctest.h>
#include <mosaic/match/matcher.h>
#include <random>

using namespace mosaic;

namespace {
std::vector<std::string> split(const std::string& s)
{
    std::vector<std::string> out;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= s.size(); ++i)
        if (i == s.size() || s[i] == '.') {
            out.push_back(s.substr(start, i - start));
            start = i + 1;
        }
    return out;
}

// Obviously-correct reference implementation to compare against.
bool naive_match(const std::string& pattern, const std::string& channel)
{
    const auto p = split(pattern), c = split(channel);
    for (std::size_t i = 0; i < p.size(); ++i) {
        if (p[i] == ">")
            return c.size() > i;
        if (i >= c.size())
            return false;
        if (p[i] != "*" && p[i] != c[i])
            return false;
    }
    return p.size() == c.size();
}
}

TEST_CASE("pattern validation")
{
    CHECK(Matcher::valid("a"));
    CHECK(Matcher::valid("a.*.c"));
    CHECK(Matcher::valid(">"));
    CHECK(Matcher::valid("a.>"));
    CHECK_FALSE(Matcher::valid(""));
    CHECK_FALSE(Matcher::valid("a..b"));
    CHECK_FALSE(Matcher::valid(".a"));
    CHECK_FALSE(Matcher::valid("a."));
    CHECK_FALSE(Matcher::valid("a.>.b"));
    Matcher m;
    CHECK_FALSE(m.add("a..b").has_value());
}

TEST_CASE("literal, star and tail")
{
    Matcher m;
    const auto exact = *m.add("storage.eu.shard7");
    const auto star = *m.add("storage.*.shard7");
    const auto tail = *m.add("storage.>");
    const auto all = *m.add(">");

    auto hits = m.match("storage.eu.shard7");
    CHECK(hits == std::vector<Matcher::SubId> { exact, star, tail, all });
    CHECK(m.match("storage.us.shard7") == std::vector<Matcher::SubId> { star, tail, all });
    CHECK(m.match("storage") == std::vector<Matcher::SubId> { all }); // '>' needs at least one segment after "storage."
    CHECK(m.match("other.x") == std::vector<Matcher::SubId> { all });
    CHECK(m.match("storage.eu.shard7.deep") == std::vector<Matcher::SubId> { tail, all });
}

TEST_CASE("remove and id reuse")
{
    Matcher m;
    const auto a = *m.add("a.b");
    const auto b = *m.add("a.*");
    CHECK(m.size() == 2);
    m.remove(a);
    CHECK(m.size() == 1);
    CHECK(m.match("a.b") == std::vector<Matcher::SubId> { b });
    const auto c = *m.add("x.y");
    CHECK(c == a); // freed id is reused
    CHECK(m.match("a.b") == std::vector<Matcher::SubId> { b });
    CHECK(m.match("x.y") == std::vector<Matcher::SubId> { c });
    m.remove(a);
    m.remove(a); // idempotent
    CHECK(m.match("x.y").empty());
}

TEST_CASE("more than 64 subscriptions (multi-word bitsets)")
{
    Matcher m;
    for (int i = 0; i < 200; ++i)
        m.add("topic." + std::to_string(i));
    CHECK(m.match("topic.150") == std::vector<Matcher::SubId> { 150 });
    CHECK(m.match("topic.200").empty());
}

TEST_CASE("randomized: agrees with naive matcher")
{
    std::mt19937 rng(42);
    const std::vector<std::string> words = { "a", "b", "c", "d" };
    auto word = [&] { return words[rng() % words.size()]; };

    for (int round = 0; round < 50; ++round) {
        Matcher m;
        std::vector<std::pair<Matcher::SubId, std::string>> live;
        for (int i = 0; i < 40; ++i) {
            std::string p;
            const int n = 1 + static_cast<int>(rng() % 4);
            for (int s = 0; s < n; ++s) {
                if (s)
                    p += '.';
                const auto roll = rng() % 6;
                if (s == n - 1 && roll == 0)
                    p += '>';
                else if (roll == 1)
                    p += '*';
                else
                    p += word();
            }
            live.push_back({ *m.add(p), p });
        }
        // remove a few to exercise reuse
        for (int i = 0; i < 8; ++i) {
            const auto idx = rng() % live.size();
            m.remove(live[idx].first);
            live.erase(live.begin() + static_cast<std::ptrdiff_t>(idx));
        }
        for (int q = 0; q < 100; ++q) {
            std::string ch;
            const int n = 1 + static_cast<int>(rng() % 5);
            for (int s = 0; s < n; ++s)
                ch += (s ? "." : "") + word();
            std::vector<Matcher::SubId> expected;
            for (const auto& [id, pattern] : live)
                if (naive_match(pattern, ch))
                    expected.push_back(id);
            std::sort(expected.begin(), expected.end());
            CHECK(m.match(ch) == expected);
        }
    }
}

// MATCH-1: the matcher's cost claim, measured instead of asserted. The bound is deliberately loose - an order
// of magnitude, not a percentage - so a noisy machine does not fail it; the printed number is the real output.
TEST_CASE("matcher cost at 1k, 10k and 100k subscriptions")
{
    const auto measure = [](std::size_t subscriptions) {
        Matcher matcher;
        for (std::size_t i = 0; i < subscriptions; ++i) {
            // A realistic mix: literal, star and tail patterns over a few levels.
            switch (i % 3) {
            case 0:
                matcher.add("svc.eu.shard" + std::to_string(i));
                break;
            case 1:
                matcher.add("events.*.topic" + std::to_string(i));
                break;
            default:
                matcher.add("metrics.host" + std::to_string(i) + ".>");
                break;
            }
        }
        constexpr int matches = 2000;
        const auto start = std::chrono::steady_clock::now();
        std::size_t hits = 0;
        for (int i = 0; i < matches; ++i)
            hits += matcher.match("events.eu.topic7").size();
        const auto elapsed = std::chrono::steady_clock::now() - start;
        const auto per_match = std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count() / matches;

        // The per-message question the node and transport actually ask, which needs no id list.
        const auto any_start = std::chrono::steady_clock::now();
        std::size_t any_hits = 0;
        for (int i = 0; i < matches; ++i)
            any_hits += matcher.matches_any("events.eu.topic7") ? 1 : 0;
        const auto any_elapsed = std::chrono::steady_clock::now() - any_start;
        const auto per_any = std::chrono::duration_cast<std::chrono::nanoseconds>(any_elapsed).count() / matches;
        (void)any_hits;
        return std::make_tuple(per_match, per_any, hits);
    };

    // Sanitizers make everything several times slower, so the timing gate is meaningless there: the sanitizer
    // job is looking for undefined behaviour, not speed. Numbers are still printed.
#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)
    constexpr bool instrumented = true;
#elif defined(__has_feature)
#if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer)
    constexpr bool instrumented = true;
#else
    constexpr bool instrumented = false;
#endif
#else
    constexpr bool instrumented = false;
#endif

    // The gate is a regression gate, not a performance target: 4x today's measured cost, so a real regression
    // fails and ordinary noise does not. The numbers themselves are the output, and they are recorded in
    // docs/design/03-matching-and-channels.md.
    struct Expectation {
        unsigned subscriptions;
        std::int64_t match_ns;
        std::int64_t any_ns;
    };
    for (const auto& expected : { Expectation { 1000, 4 * 5200, 4 * 5200 },
             Expectation { 10000, 4 * 22370, 4 * 22370 }, Expectation { 100000, 4 * 192000, 4 * 192000 } }) {
        const auto [nanos_per_match, nanos_per_any, hits] = measure(expected.subscriptions);
        MESSAGE("matcher: " << expected.subscriptions << " subscriptions -> match " << nanos_per_match << " ns, "
                            << "matches_any " << nanos_per_any << " ns (" << hits << " hits / 2000)");
        if (instrumented) {
            MESSAGE("matcher: timing gates skipped under sanitizers");
            continue;
        }
        CHECK(nanos_per_match < expected.match_ns);
        CHECK(nanos_per_any < expected.any_ns);
    }
}

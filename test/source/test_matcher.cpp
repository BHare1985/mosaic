#include <algorithm>
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

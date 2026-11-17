#include <bit>
#include <mosaic/match/matcher.h>

namespace mosaic {
namespace {
    std::vector<std::string_view> split(std::string_view s)
    {
        std::vector<std::string_view> out;
        std::size_t start = 0;
        while (true) {
            const auto dot = s.find('.', start);
            if (dot == std::string_view::npos) {
                out.push_back(s.substr(start));
                return out;
            }
            out.push_back(s.substr(start, dot - start));
            start = dot + 1;
        }
    }

    template <class T>
    T& at_grow(std::vector<T>& v, std::size_t i)
    {
        if (v.size() <= i)
            v.resize(i + 1);
        return v[i];
    }
} // namespace

void Matcher::Bits::set(std::size_t i)
{
    if (w.size() <= i / 64)
        w.resize(i / 64 + 1);
    w[i / 64] |= std::uint64_t { 1 } << (i % 64);
}

void Matcher::Bits::reset(std::size_t i)
{
    if (i / 64 < w.size())
        w[i / 64] &= ~(std::uint64_t { 1 } << (i % 64));
}

void Matcher::Bits::or_with(const Bits& o)
{
    if (w.size() < o.w.size())
        w.resize(o.w.size());
    for (std::size_t i = 0; i < o.w.size(); ++i)
        w[i] |= o.w[i];
}

void Matcher::Bits::and_with(const Bits& o)
{
    if (w.size() > o.w.size())
        w.resize(o.w.size());
    for (std::size_t i = 0; i < w.size(); ++i)
        w[i] &= o.w[i];
}

bool Matcher::Bits::none() const
{
    for (auto word : w)
        if (word)
            return false;
    return true;
}

bool Matcher::valid(std::string_view pattern)
{
    if (pattern.empty())
        return false;
    const auto segs = split(pattern);
    for (std::size_t i = 0; i < segs.size(); ++i) {
        if (segs[i].empty())
            return false;
        if (segs[i] == ">" && i + 1 != segs.size())
            return false;
    }
    return true;
}

std::optional<Matcher::SubId> Matcher::add(std::string_view pattern)
{
    if (!valid(pattern))
        return std::nullopt;

    SubId id;
    if (!free_.empty()) {
        id = free_.back();
        free_.pop_back();
    } else {
        id = static_cast<SubId>(subs_.size());
        subs_.emplace_back();
    }
    subs_[id] = { std::string(pattern), true };

    const auto segs = split(pattern);
    const bool tail = segs.back() == ">";
    const std::size_t constrained = tail ? segs.size() - 1 : segs.size();
    for (std::size_t i = 0; i < constrained; ++i) {
        if (segs[i] == "*")
            at_grow(star_, i).set(id);
        else
            at_grow(literal_, i)[std::string(segs[i])].set(id);
    }
    if (tail)
        at_grow(tail_, constrained).set(id);
    else
        at_grow(exact_, constrained).set(id);
    return id;
}

void Matcher::remove(SubId id)
{
    if (id >= subs_.size() || !subs_[id].active)
        return;
    const auto segs = split(subs_[id].pattern);
    const bool tail = segs.back() == ">";
    const std::size_t constrained = tail ? segs.size() - 1 : segs.size();
    for (std::size_t i = 0; i < constrained; ++i) {
        if (segs[i] == "*")
            star_[i].reset(id);
        else
            literal_[i][std::string(segs[i])].reset(id); // TODO(MATCH-3): erase empty entries
    }
    if (tail)
        tail_[constrained].reset(id);
    else
        exact_[constrained].reset(id);
    subs_[id].active = false;
    free_.push_back(id);
}

bool Matcher::matches_any(std::string_view channel) const
{
    const auto segs = split(channel);
    const std::size_t m = segs.size();

    Bits acc;
    if (m < exact_.size())
        acc = exact_[m];
    for (std::size_t k = 0; k < m && k < tail_.size(); ++k)
        acc.or_with(tail_[k]);
    if (acc.none())
        return false; // nothing of this length: the cheapest exit, and the common case for unrelated channels

    Bits open;
    for (std::size_t i = 0; i < m && !acc.none(); ++i) {
        if (i < tail_.size())
            open.or_with(tail_[i]);
        Bits allowed = open;
        if (i < star_.size())
            allowed.or_with(star_[i]);
        if (i < literal_.size()) {
            const auto it = literal_[i].find(std::string(segs[i]));
            if (it != literal_[i].end())
                allowed.or_with(it->second);
        }
        acc.and_with(allowed);
    }
    return !acc.none();
}

std::vector<Matcher::SubId> Matcher::match(std::string_view channel) const
{
    const auto segs = split(channel);
    const std::size_t m = segs.size();

    // Length filter: exact patterns of length m, plus '>' patterns with fewer than m leading segments.
    Bits acc;
    if (m < exact_.size())
        acc = exact_[m];
    for (std::size_t k = 0; k < m && k < tail_.size(); ++k)
        acc.or_with(tail_[k]);

    Bits open; // '>' patterns whose tail has started: they accept anything from here on
    for (std::size_t i = 0; i < m && !acc.none(); ++i) {
        if (i < tail_.size())
            open.or_with(tail_[i]);
        Bits allowed = open;
        if (i < star_.size())
            allowed.or_with(star_[i]);
        if (i < literal_.size()) {
            const auto it = literal_[i].find(std::string(segs[i]));
            if (it != literal_[i].end())
                allowed.or_with(it->second);
        }
        acc.and_with(allowed);
    }

    std::vector<SubId> out;
    for (std::size_t word = 0; word < acc.w.size(); ++word)
        for (auto bits = acc.w[word]; bits; bits &= bits - 1)
            out.push_back(static_cast<SubId>(word * 64 + static_cast<std::size_t>(std::countr_zero(bits))));
    return out;
}

} // namespace mosaic

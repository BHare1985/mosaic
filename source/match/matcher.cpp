#include <mosaic/match/matcher.h>

#include <bit>

using std::countr_zero;
using std::nullopt;
using std::optional;
using std::size_t;
using std::string;
using std::string_view;
using std::uint64_t;
using std::vector;

namespace mosaic {
namespace {
vector<string_view> split(string_view s) {
    vector<string_view> out;
    size_t start = 0;
    while (true) {
        const auto dot = s.find('.', start);
        if (dot == string_view::npos) {
            out.push_back(s.substr(start));
            return out;
        }
        out.push_back(s.substr(start, dot - start));
        start = dot + 1;
    }
}

template <class T> T& at_grow(vector<T>& v, size_t i) {
    if (v.size() <= i)
        v.resize(i + 1);
    return v[i];
}
}  // namespace

void Matcher::Bits::set(size_t i) {
    if (w.size() <= i / 64)
        w.resize(i / 64 + 1);
    w[i / 64] |= uint64_t{1} << (i % 64);
}

void Matcher::Bits::reset(size_t i) {
    if (i / 64 < w.size())
        w[i / 64] &= ~(uint64_t{1} << (i % 64));
}

void Matcher::Bits::or_with(const Bits& o) {
    if (w.size() < o.w.size())
        w.resize(o.w.size());
    for (size_t i = 0; i < o.w.size(); ++i)
        w[i] |= o.w[i];
}

void Matcher::Bits::and_with(const Bits& o) {
    if (w.size() > o.w.size())
        w.resize(o.w.size());
    for (size_t i = 0; i < w.size(); ++i)
        w[i] &= o.w[i];
}

bool Matcher::Bits::none() const {
    for (auto word : w)
        if (word)
            return false;
    return true;
}

bool Matcher::valid(string_view pattern) {
    if (pattern.empty())
        return false;
    const auto segs = split(pattern);
    for (size_t i = 0; i < segs.size(); ++i) {
        if (segs[i].empty())
            return false;
        if (segs[i] == ">" && i + 1 != segs.size())
            return false;
    }
    return true;
}

optional<Matcher::SubId> Matcher::add(string_view pattern) {
    if (!valid(pattern))
        return nullopt;

    SubId id;
    if (!free_.empty()) {
        id = free_.back();
        free_.pop_back();
    } else {
        id = static_cast<SubId>(subs_.size());
        subs_.emplace_back();
    }
    subs_[id] = {string(pattern), true};

    const auto segs = split(pattern);
    const bool tail = segs.back() == ">";
    const size_t constrained = tail ? segs.size() - 1 : segs.size();
    for (size_t i = 0; i < constrained; ++i) {
        if (segs[i] == "*")
            at_grow(star_, i).set(id);
        else
            at_grow(literal_, i)[string(segs[i])].set(id);
    }
    if (tail)
        at_grow(tail_, constrained).set(id);
    else
        at_grow(exact_, constrained).set(id);
    return id;
}

void Matcher::remove(SubId id) {
    if (id >= subs_.size() || !subs_[id].active)
        return;
    const auto segs = split(subs_[id].pattern);
    const bool tail = segs.back() == ">";
    const size_t constrained = tail ? segs.size() - 1 : segs.size();
    for (size_t i = 0; i < constrained; ++i) {
        if (segs[i] == "*") {
            star_[i].reset(id);
        } else {
            const string segment(segs[i]);
            auto& by_segment = literal_[i];
            const auto it = by_segment.find(segment);
            if (it != by_segment.end()) {
                it->second.reset(id);
                // Erase the entry when nothing subscribes to that literal any more, so add/remove
                // churn returns the matcher to its baseline footprint instead of accumulating empty
                // bitsets.
                if (it->second.none())
                    by_segment.erase(it);
            }
        }
    }
    if (tail)
        tail_[constrained].reset(id);
    else
        exact_[constrained].reset(id);
    subs_[id].active = false;
    free_.push_back(id);
}

bool Matcher::matches_any(string_view channel) const {
    const auto segs = split(channel);
    const size_t m = segs.size();

    Bits acc;
    if (m < exact_.size())
        acc = exact_[m];
    for (size_t k = 0; k < m && k < tail_.size(); ++k)
        acc.or_with(tail_[k]);
    if (acc.none())
        return false;  // nothing of this length: the cheapest exit, and the common case for
                       // unrelated channels

    Bits open;
    for (size_t i = 0; i < m && !acc.none(); ++i) {
        if (i < tail_.size())
            open.or_with(tail_[i]);
        Bits allowed = open;
        if (i < star_.size())
            allowed.or_with(star_[i]);
        if (i < literal_.size()) {
            const auto it = literal_[i].find(string(segs[i]));
            if (it != literal_[i].end())
                allowed.or_with(it->second);
        }
        acc.and_with(allowed);
    }
    return !acc.none();
}

bool Matcher::remove_pattern(string_view pattern) {
    for (SubId id = 0; id < subs_.size(); ++id)
        if (subs_[id].active && subs_[id].pattern == pattern) {
            remove(id);
            return true;
        }
    return false;
}

vector<string> Matcher::patterns() const {
    vector<string> out;
    for (const auto& subscription : subs_)
        if (subscription.active)
            out.push_back(subscription.pattern);
    return out;
}

vector<Matcher::SubId> Matcher::match(string_view channel) const {
    const auto segs = split(channel);
    const size_t m = segs.size();

    // Length filter: exact patterns of length m, plus '>' patterns with fewer than m leading
    // segments.
    Bits acc;
    if (m < exact_.size())
        acc = exact_[m];
    for (size_t k = 0; k < m && k < tail_.size(); ++k)
        acc.or_with(tail_[k]);

    Bits open;  // '>' patterns whose tail has started: they accept anything from here on
    for (size_t i = 0; i < m && !acc.none(); ++i) {
        if (i < tail_.size())
            open.or_with(tail_[i]);
        Bits allowed = open;
        if (i < star_.size())
            allowed.or_with(star_[i]);
        if (i < literal_.size()) {
            const auto it = literal_[i].find(string(segs[i]));
            if (it != literal_[i].end())
                allowed.or_with(it->second);
        }
        acc.and_with(allowed);
    }

    vector<SubId> out;
    for (size_t word = 0; word < acc.w.size(); ++word)
        for (auto bits = acc.w[word]; bits; bits &= bits - 1)
            out.push_back(static_cast<SubId>(word * 64 + static_cast<size_t>(countr_zero(bits))));
    return out;
}

}  // namespace mosaic

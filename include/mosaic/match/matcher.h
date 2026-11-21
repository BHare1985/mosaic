#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace mosaic {

/// Fast channel -> subscriber matching using an inverted bitmap index (the idea from Malamute's
/// whitepaper, which was never implemented there). Design and complexity:
/// docs/design/03-matching-and-channels.md.
///
/// Pattern language (segments separated by '.'):
///   literal   \c eu        matches exactly that segment
///   star      \c *         matches exactly one segment
///   tail      \c >         last segment only; matches one or more remaining segments
/// Examples: \c storage.*.shard7 matches \c storage.eu.shard7; \c storage.> matches
/// \c storage.eu.shard7. Channels (the thing being matched) must not contain a star or tail
/// segment - the wildcards are a pattern's vocabulary, never a channel's.
///
/// Cost of match() is O(segments x words) where words = subscribers/64, independent of how many
/// subscribers do NOT match. Not thread-safe.
class Matcher {
public:
    using SubId = std::uint32_t;

    /// nullopt if the pattern is invalid (empty segment, '>' not last, empty string).
    std::optional<SubId> add(std::string_view pattern);
    void remove(SubId id);

    /// Remove by pattern text. False when no active subscription has that exact pattern. Safer than
    /// mapping a position in `patterns()` onto an id, which only holds when ids are dense.
    bool remove_pattern(std::string_view pattern);

    /// All subscriptions matching `channel`, in ascending SubId order.
    std::vector<SubId> match(std::string_view channel) const;
    /// The per-message question: does anything match? Answered without materialising the id list,
    /// because the node and the transport ask it for every arriving record (MATCH-1 measured the
    /// difference).
    bool matches_any(std::string_view channel) const;

    /// The patterns currently subscribed, in id order. What a node advertises to peers, and what a
    /// test can assert after removals.
    std::vector<std::string> patterns() const;

    static bool valid(std::string_view pattern);
    std::size_t size() const {
        return subs_.size() - free_.size();
    }

private:
    struct Bits {
        std::vector<std::uint64_t> w;
        void set(std::size_t i);
        void reset(std::size_t i);
        void or_with(const Bits& o);
        void and_with(const Bits& o);
        bool none() const;
        /// Reuse: zero every word but keep the buffer, so a steady-state match allocates nothing.
        void clear() {
            for (auto& word : w)
                word = 0;
        }
        void assign(const Bits& o) {
            w = o.w;
        }
    };
    struct Sub {
        std::string pattern;
        bool active = false;
    };

    std::vector<Sub> subs_;
    std::vector<SubId> free_;
    std::vector<std::unordered_map<std::string, Bits>> literal_;  // [position][segment] -> subs
    std::vector<Bits> star_;   // [position] -> subs with '*' there
    std::vector<Bits> tail_;   // [k] -> subs with k leading segments then '>'
    std::vector<Bits> exact_;  // [m] -> subs without '>' that have exactly m segments

    /// Scratch for the hot path, reused across calls: `matches_any` is asked once per arriving
    /// record, and allocating three bitsets and a segment list per call was the measured cost
    /// (MATCH-4). Mutable because matching is a const query; single-threaded by contract, and `add`
    /// and `remove` were already not shareable across threads.
    mutable Bits acc_;
    mutable Bits open_;
    mutable Bits allowed_;
    mutable std::vector<std::string_view> segs_;
};

}  // namespace mosaic

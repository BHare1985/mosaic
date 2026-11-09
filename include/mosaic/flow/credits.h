#pragma once

#include <cstdint>

namespace mosaic {

/// Credit-based flow control primitives (Malamute's CBFC idea, never implemented there).
/// Semantics and wire usage: docs/design/04-flow-control.md.
///
/// Rule of thumb: the party that RECEIVES data decides how much it can take and hands out credit;
/// the party that SENDS data spends credit and stops at zero. Never send without credit.

/// Sender-side budget.
class CreditWindow {
public:
    /// `capacity` is the most credit that can ever be held; `initial` is what we start with.
    explicit CreditWindow(std::uint32_t capacity = 0, std::uint32_t initial = 0)
        : capacity_(capacity)
        , available_(initial > capacity ? capacity : initial)
    {
    }

    std::uint32_t available() const { return available_; }
    std::uint32_t capacity() const { return capacity_; }

    /// Spend up to `wanted` credits; returns how many were actually granted (possibly 0).
    std::uint32_t take_up_to(std::uint32_t wanted)
    {
        const auto n = wanted < available_ ? wanted : available_;
        available_ -= n;
        return n;
    }

    /// Receive `n` credits (from a CreditMsg, or a spent credit coming back). Clamped to capacity.
    void grant(std::uint32_t n)
    {
        available_ = (capacity_ - available_ < n) ? capacity_ : available_ + n;
    }

    /// Give everything back (use after a timeout when outstanding work is presumed lost).
    void reset() { available_ = capacity_; }

private:
    std::uint32_t capacity_;
    std::uint32_t available_;
};

/// Receiver-side bookkeeping: tells you when to send more credit, in batches rather than one by one.
class CreditGranter {
public:
    /// `window` is the number of messages the receiver is willing to have in flight.
    explicit CreditGranter(std::uint32_t window)
        : window_(window)
    {
    }

    /// Credit to send right after connecting.
    std::uint32_t initial() const { return window_; }

    /// Call after consuming `n` messages. Returns credit to send now (0 = nothing yet).
    /// Refills when at least half the window has been consumed.
    std::uint32_t on_consumed(std::uint32_t n)
    {
        consumed_ += n;
        if (consumed_ * 2 < window_)
            return 0;
        const auto grant = consumed_;
        consumed_ = 0;
        return grant;
    }

private:
    std::uint32_t window_;
    std::uint32_t consumed_ = 0;
};

} // namespace mosaic

#pragma once

#include <cstdint>
#include <cstdlib>
#include <functional>
#include <optional>
#include <string>

namespace mosaic::test {

/// How many seeds a sweep runs. MOSAIC_SEEDS raises it (CI can run a large sweep); the default keeps
/// the fast loop quick while still being a sweep rather than a single lucky run (SIM-2).
inline unsigned seed_count()
{
    if (const char* env = std::getenv("MOSAIC_SEEDS")) {
        const auto parsed = std::strtoul(env, nullptr, 10);
        if (parsed > 0)
            return static_cast<unsigned>(parsed);
    }
    return 8;
}

/// Runs `scenario(seed)` for seeds 1..count and returns the first failing seed, or nullopt.
///
/// A scenario reports failure by returning false; assertions inside it carry the detail. Callers print
/// the returned seed, which is enough to re-run that single case: every decision in a SimNetwork run
/// derives from the seed, so the same seed reproduces the same failure.
inline std::optional<std::uint64_t> first_failing_seed(
    unsigned count, const std::function<bool(std::uint64_t)>& scenario)
{
    for (unsigned i = 1; i <= count; ++i)
        if (!scenario(i))
            return static_cast<std::uint64_t>(i);
    return std::nullopt;
}

} // namespace mosaic::test

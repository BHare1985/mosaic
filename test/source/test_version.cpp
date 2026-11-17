// The version comes from CMake and nowhere else. This test is the anti-drift check: CMake passes
// its own PROJECT_VERSION into the test target, so bumping one place and not the other is a red
// build.
#include <mosaic/version.h>

#include <doctest/doctest.h>
#include <string>

using mosaic::kMilestonesClosed;
using mosaic::kVersion;
using mosaic::kVersionMajor;
using mosaic::kVersionMinor;
using mosaic::kVersionParts;
using mosaic::kVersionPatch;
using std::string;
using std::string_view;

TEST_CASE("the version header agrees with CMake") {
    // MOSAIC_EXPECTED_VERSION is supplied by CMake from project(VERSION ...).
    CHECK(string(kVersion) == string(MOSAIC_EXPECTED_VERSION));

    // The macro alias and the constexpr value describe the same version.
    static_assert(string_view(MOSAIC_VERSION) == kVersion, "the macro alias drifted from kVersion");

    // The components are the parts of the string (also asserted at compile time in the header).
    CHECK(kVersionParts[0] == kVersionMajor);
    CHECK(kVersionParts[1] == kVersionMinor);
    CHECK(kVersionParts[2] == kVersionPatch);
}

TEST_CASE("the minor number counts closed milestones") {
    // The convention, not a coincidence: docs/design/conventions.md states it, and this keeps a
    // bump honest.
    CHECK(kMilestonesClosed == kVersionMinor);
    CHECK(kVersionMajor == 0u);  // 1.0.0 waits for a frozen API
}

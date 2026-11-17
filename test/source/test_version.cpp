#include <mosaic/version.h>

#include <doctest/doctest.h>
#include <string>

using std::string;
using std::string_view;

TEST_CASE("Mosaic version") {
    static_assert(string_view(MOSAIC_VERSION) == string_view("0.1.0"));
    CHECK(string(MOSAIC_VERSION) == string("0.1.0"));
}

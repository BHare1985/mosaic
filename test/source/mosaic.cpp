#include "mosaic/Broker.h"

#include <doctest/doctest.h>
#include <mosaic/mosaic.h>
#include <mosaic/version.h>

#include <string>

TEST_CASE("Mosaic version")
{
    static_assert(std::string_view(MOSAIC_VERSION) == std::string_view("0.1.0"));
    CHECK(std::string(MOSAIC_VERSION) == std::string("0.1.0"));
}


TEST_CASE("Can load config")
{
    auto b = mosaic::Broker("publicKey", true);

}


#include <doctest/doctest.h>
#include <mosaic/mosaic.h>
#include <mosaic/version.h>

#include <string>

TEST_CASE("Mosaic")
{
    using namespace mosaic;

    Mosaic mosaic("Tests");

    CHECK(mosaic.greet(LanguageCode::EN) == "Hello, Tests!");
    CHECK(mosaic.greet(LanguageCode::DE) == "Hallo Tests!");
    CHECK(mosaic.greet(LanguageCode::ES) == "¡Hola Tests!");
    CHECK(mosaic.greet(LanguageCode::FR) == "Bonjour Tests!");
}

TEST_CASE("Mosaic version")
{
    static_assert(std::string_view(MOSAIC_VERSION) == std::string_view("0.1.0"));
    CHECK(std::string(MOSAIC_VERSION) == std::string("0.1.0"));
}

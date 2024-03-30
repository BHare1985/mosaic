#include <doctest/doctest.h>
#include <mosiac/mosiac.h>
#include <mosiac/version.h>

#include <string>

TEST_CASE("Mosiac") {
  using namespace mosiac;

  Mosiac mosiac("Tests");

  CHECK(mosiac.greet(LanguageCode::EN) == "Hello, Tests!");
  CHECK(mosiac.greet(LanguageCode::DE) == "Hallo Tests!");
  CHECK(mosiac.greet(LanguageCode::ES) == "¡Hola Tests!");
  CHECK(mosiac.greet(LanguageCode::FR) == "Bonjour Tests!");
}

TEST_CASE("Mosiac version") {
  static_assert(std::string_view(MOSIAC_VERSION) == std::string_view("0.1.0"));
  CHECK(std::string(MOSIAC_VERSION) == std::string("0.1.0"));
}

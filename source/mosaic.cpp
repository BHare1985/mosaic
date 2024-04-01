#include <fmt/format.h>
#include <mosaic/mosaic.h>

using namespace mosaic;

Mosaic::Mosaic(std::string _name)
    : name(std::move(_name))
{
}

std::string Mosaic::greet(LanguageCode lang) const
{
    switch (lang) {
    case LanguageCode::EN:
	return fmt::format("Hello, {}!", name);
    default:
	throw std::runtime_error("No match found for language");
    }
}

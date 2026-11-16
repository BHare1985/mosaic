#include <iostream>
#include <mosaic/match/matcher.h>
#include <mosaic/version.h>
#include <string>

namespace {
int usage()
{
    std::cerr << "usage:\n"
                 "  Mosaic version\n"
                 "  Mosaic match <pattern> <channel>   # does the channel match the pattern?\n";
    return 2;
}
} // namespace

// Minimal CLI. Planned subcommands (pub, sub, tail, verify) are tasks CLI-* in docs/design/TODO.md.
int main(int argc, char** argv)
{
    if (argc < 2)
        return usage();
    const std::string command = argv[1];

    if (command == "version") {
        std::cout << "Mosaic, version " << MOSAIC_VERSION << '\n';
        return 0;
    }
    if (command == "match" && argc == 4) {
        mosaic::Matcher matcher;
        if (!matcher.add(argv[2])) {
            std::cerr << "invalid pattern: " << argv[2] << '\n';
            return 2;
        }
        const bool hit = matcher.matches_any(argv[3]);
        std::cout << (hit ? "match" : "no match") << '\n';
        return hit ? 0 : 1;
    }
    return usage();
}

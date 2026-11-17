#include <mosaic/core/keystore.h>

#ifdef MOSAIC_HAS_SODIUM
#include <sodium.h>
#endif

#include <mosaic/core/z85.h>

#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

#ifndef _WIN32
#include <sys/stat.h>
#endif

namespace mosaic {
namespace {
    constexpr const char* kHeader = "mosaic-keystore 1";

    bool valid_sizes(const Keystore& keystore)
    {
        // 32-byte public key, 64-byte secret key: anything else is not an Ed25519 pair.
        return keystore.public_key.size() == 32 && keystore.secret_key.size() == 64;
    }
} // namespace

std::optional<Keystore> generate_keystore()
{
#ifdef MOSAIC_HAS_SODIUM
    const auto pair = ed25519_generate();
    if (!pair)
        return std::nullopt;
    Keystore keystore { pair->first, pair->second };
    if (!valid_sizes(keystore))
        return std::nullopt;
    return keystore;
#else
    return std::nullopt; // a dependency-free build has no real keys to offer
#endif
}

bool save_keystore(const Keystore& keystore, const std::string& path)
{
    if (!valid_sizes(keystore))
        return false;

    const std::string temp = path + ".tmp";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out)
            return false;
#ifndef _WIN32
        // Restrict before any key material is written, so there is no window where the file is
        // world-readable.
        ::chmod(temp.c_str(), S_IRUSR | S_IWUSR);
#endif
        out << kHeader << "\npublic " << to_hex(keystore.public_key) << "\nsecret " << to_hex(keystore.secret_key)
            << "\n";
        if (!out.good())
            return false;
    }

    if (std::rename(temp.c_str(), path.c_str()) != 0) {
        std::remove(temp.c_str());
        return false;
    }
    return true;
}

std::optional<Keystore> load_keystore(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return std::nullopt;

    std::string header;
    if (!std::getline(in, header) || header != kHeader)
        return std::nullopt;

    Keystore keystore;
    std::string line;
    while (std::getline(in, line)) {
        const auto space = line.find(' ');
        if (space == std::string::npos)
            return std::nullopt;
        const auto tag = line.substr(0, space);
        const auto parsed = from_hex(line.substr(space + 1));
        if (!parsed)
            return std::nullopt;
        if (tag == "public")
            keystore.public_key = *parsed;
        else if (tag == "secret")
            keystore.secret_key = *parsed;
        else
            return std::nullopt;
    }

    if (!valid_sizes(keystore))
        return std::nullopt;
    return keystore;
}

std::optional<CurveIdentity> curve_identity_from_keystore(const Keystore& keystore)
{
#ifdef MOSAIC_HAS_SODIUM
    if (!valid_sizes(keystore))
        return std::nullopt;
    constexpr std::size_t kCurveKeyBytes = 32; // curve25519 public and secret are both 32 bytes
    unsigned char curve_secret[kCurveKeyBytes] = { };
    // 32 bytes: the scalar CURVE uses, derived from the Ed25519 secret.
    crypto_sign_ed25519_sk_to_curve25519(curve_secret, keystore.secret_key.data());

    unsigned char curve_public[kCurveKeyBytes] = { };
    if (crypto_scalarmult_base(curve_public, curve_secret) != 0)
        return std::nullopt;

    CurveIdentity identity;
    identity.public_z85 = z85_encode(curve_public, kCurveKeyBytes);
    identity.secret_z85 = z85_encode(curve_secret, kCurveKeyBytes);
    if (identity.public_z85.empty() || identity.secret_z85.empty())
        return std::nullopt;
    return identity;
#else
    (void)keystore;
    return std::nullopt; // no real keys in a dependency-free build
#endif
}

} // namespace mosaic

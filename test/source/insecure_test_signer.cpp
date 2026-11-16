// Definition of the non-cryptographic test double. It lives in the test sources on purpose: the
// product library must not carry it, and only tests can reach the header that declares it.
#include <mosaic/core/insecure_test_signer.h>

namespace mosaic {

Bytes InsecureTestSigner::make(const WriterId& id, const Hash& record_hash)
{
    Bytes in;
    for (char c : std::string("INSECURE-TEST|") + id + "|")
        in.push_back(static_cast<std::uint8_t>(c));
    in.insert(in.end(), record_hash.bytes.begin(), record_hash.bytes.end());
    const Hash digest = sha256(in);
    return Bytes(digest.bytes.begin(), digest.bytes.end());
}

} // namespace mosaic

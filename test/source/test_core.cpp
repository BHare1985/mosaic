#include <mosaic/core/crypto.h>
#include <mosaic/core/record.h>

#include <doctest/doctest.h>

using std::string;

using namespace mosaic;

namespace {
Bytes bytes(const string& s) {
    return Bytes(s.begin(), s.end());
}
}  // namespace

TEST_CASE("sha256 known vectors") {
    CHECK(to_hex(sha256(bytes(""))) ==
          "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(to_hex(sha256(bytes("abc"))) ==
          "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(to_hex(sha256(bytes("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"))) ==
          "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
}

TEST_CASE("canonical_bytes ignores sig, covers every other field") {
    Record r;
    r.writer = "w";
    r.channel = "a.b";
    r.seq = 3;
    r.timestamp_ms = 9;
    r.content_type = "text/plain";
    r.payload = bytes("hi");
    const auto base = canonical_bytes(r);

    Record with_sig = r;
    with_sig.sig = bytes("whatever");
    CHECK(canonical_bytes(with_sig) == base);

    Record changed = r;
    changed.payload = bytes("ho");
    CHECK(canonical_bytes(changed) != base);
    changed = r;
    changed.seq = 4;
    CHECK(canonical_bytes(changed) != base);
    changed = r;
    changed.channel = "a.c";
    CHECK(canonical_bytes(changed) != base);
    // field boundaries are unambiguous: moving a byte between adjacent fields changes the encoding
    Record x = r, y = r;
    x.writer = "ab";
    x.channel = "c";
    y.writer = "a";
    y.channel = "bc";
    CHECK(canonical_bytes(x) != canonical_bytes(y));
}

// Opt-in libFuzzer target for the wire decoder (ENC-1). Not part of any default build: the seeded
// mutation test in test/source/test_codec.cpp is the guard that runs in the fast loop, and this is the
// deeper instrument for someone with clang.
//
//   cmake -S fuzz -B build/fuzz -DMOSAIC_BUILD_ZMQ_STACK=OFF -DCMAKE_CXX_COMPILER=clang++
//   cmake --build build/fuzz -j && ./build/fuzz/decode_fuzz -max_total_time=60
//
// The whole contract is "a value or a failure": no crash, no UB, no partially decoded message.
#include <cstddef>
#include <cstdint>
#include <mosaic/proto/codec.h>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size)
{
    const mosaic::Bytes frame(data, data + size);
    const auto result = mosaic::decode_message(frame);
    if (result.ok()) {
        // Anything that decodes must re-encode to the same bytes, otherwise the decoder accepted a
        // non-canonical encoding.
        const auto again = mosaic::encode_message(*result.message);
        if (again != frame)
            __builtin_trap();
    } else if (result.message.has_value()) {
        __builtin_trap(); // a failure must never carry a message
    }
    return 0;
}

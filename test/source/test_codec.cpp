#include <mosaic/core/insecure_test_signer.h>
#include <mosaic/proto/codec.h>

#include <doctest/doctest.h>
#include <random>
#include <string>

using std::mt19937_64;
using std::ptrdiff_t;
using std::size_t;
using std::string;
using std::uint8_t;
using std::uniform_int_distribution;
using std::vector;

using namespace mosaic;

namespace {
Record sample_record() {
    Sha256Hasher hasher;
    InsecureTestSigner signer("alice");
    Record record;
    record.writer = "alice";
    record.channel = "events.a";
    record.hash_algo = HashAlgo::Sha256;
    record.seq = 7;
    record.timestamp_ms = 1234;
    record.content_type = "text/plain";
    record.payload = Bytes{'h', 'i'};
    record.prev.bytes[0] = 0xab;
    record.sig = signer.sign(record_hash(record, hasher));
    return record;
}

vector<Message> every_message() {
    const Record record = sample_record();
    return {
        Message{RecordMsg{record}},
        Message{DirectRecordMsg{record}},
        Message{HeadMsg{record.key(), 7, record.prev}},
        Message{FetchMsg{record.key(), 3, 64}},
        Message{AckMsg{record.key(), 7}},
        Message{GetHeadsMsg{{"a.*", "svc.>"}}},
        Message{DirectHeadMsg{record.key(), 7, record.prev}},
        Message{CreditMsg{42}},
        Message{HelloMsg{kProtocolVersion, {"events.>", "svc.x"}, 42}},
        Message{PeersMsg{{PeerEntry{"b", "tcp://10.0.0.2:5555", "tcp://10.0.0.2:5556", 1, 250},
                          PeerEntry{"c", "tcp://10.0.0.3:5555", "tcp://10.0.0.3:5556", 2, 0}}}},
    };
}
}  // namespace

// Ids are permanent: this test exists so a renumbering fails loudly rather than breaking peers.
TEST_CASE("message ids are pinned") {
    const auto messages = every_message();
    CHECK(message_id(messages[0]) == 1);   // RecordMsg
    CHECK(message_id(messages[1]) == 4);   // DirectRecordMsg
    CHECK(message_id(messages[2]) == 2);   // HeadMsg
    CHECK(message_id(messages[3]) == 3);   // FetchMsg
    CHECK(message_id(messages[4]) == 6);   // AckMsg
    CHECK(message_id(messages[5]) == 8);   // GetHeadsMsg
    CHECK(message_id(messages[6]) == 9);   // DirectHeadMsg
    CHECK(message_id(messages[7]) == 7);   // CreditMsg
    CHECK(message_id(messages[8]) == 5);   // HelloMsg
    CHECK(message_id(messages[9]) == 10);  // PeersMsg
}

TEST_CASE("every message round-trips byte for byte") {
    for (const auto& message : every_message()) {
        const Bytes encoded = encode_message(message);
        const auto decoded = decode_message(encoded);
        REQUIRE(decoded.ok());
        CHECK(to_string(decoded.status) == string("ok"));
        // decode -> encode must reproduce the original bytes exactly.
        CHECK(encode_message(*decoded.message) == encoded);
        CHECK(message_id(*decoded.message) == message_id(message));
    }
}

// Golden vectors pin the format: if the encoder changes, these fail instead of silently moving the
// wire.
TEST_CASE("the wire format matches its golden vectors") {
    const Record record = sample_record();
    CHECK(to_hex(canonical_bytes(record)) ==
          "010100000005616c696365000000086576656e74732e610000000000000007ab000000000000000000000000"
          "0000000000000000000000000000000000000000000000000004d20000000a746578742f706c61696e000000"
          "026869");
    CHECK(to_hex(encode_record(record)) ==
          "010100000005616c696365000000086576656e74732e610000000000000007ab000000000000000000000000"
          "0000000000000000000000000000000000000000000000000004d20000000a746578742f706c61696e000000"
          "026869000000206321bfbc3df98c84e88c6ad5f713df0fab38b5ab8f0cb72f94fe050f2debbd85");
    CHECK(to_hex(encode_message(Message{FetchMsg{record.key(), 3, 64}})) ==
          "000000220300000005616c696365000000086576656e74732e61000000000000000300000040");
    // Version 2 added the capacity byte: the frame is one byte longer, and it ends with the figure.
    CHECK(to_hex(encode_message(Message{HelloMsg{2, {"events.>", "svc.x"}, 7}})) ==
          "0000001c050200000002000000086576656e74732e3e000000057376632e7807");
    CHECK(to_hex(encode_message(Message{CreditMsg{42}})) == "00000005070000002a");
    // A peer list: u32 count, then per entry the id, both endpoints, hops and age, each
    // length-prefixed.
    CHECK(
        to_hex(
            encode_message(Message{PeersMsg{{PeerEntry{"b", "tcp://h:1", "tcp://h:2", 1, 5}}}})) ==
        "000000300a000000010000000162000000097463703a2f2f683a31000000097463703a2f2f683a320000000100"
        "00000000000005");
}

TEST_CASE("the protocol version says which hello layout to expect") {
    CHECK(kProtocolVersion == 2);  // the capacity byte is a wire change, not a field nobody sees

    // A hello carrying capacity must round-trip with it, and dropping the figure must change the
    // bytes - otherwise a version 1 peer would decode as a version 2 peer with no spare capacity.
    const Message hello = Message{HelloMsg{2, {"svc.>"}, 3}};
    const auto decoded = decode_message(encode_message(hello));
    REQUIRE(decoded.ok());
    CHECK(std::get<HelloMsg>(*decoded.message).spare_capacity == 3);
    CHECK(to_hex(encode_message(Message{HelloMsg{2, {"svc.>"}, 0}})) !=
          to_hex(encode_message(Message{HelloMsg{2, {"svc.>"}, 3}})));

    // A hello from an older peer is refused rather than decoded as the new layout.
    const auto old = decode_message(encode_message(Message{HelloMsg{1, {"svc.>"}, 0}}));
    CHECK(old.status == DecodeStatus::UnsupportedVersion);
}

TEST_CASE("a peer list is believed wholly or not at all") {
    const auto one = [] {
        return PeersMsg{{PeerEntry{"b", "tcp://10.0.0.2:5555", "tcp://10.0.0.2:5556", 1, 250}}};
    };

    SUBCASE("a truncated list is refused") {
        Bytes frame = encode_message(Message{one()});
        frame.resize(frame.size() - 1);
        CHECK(decode_message(frame).status == DecodeStatus::Truncated);
        CHECK_FALSE(decode_message(frame).message.has_value());
    }

    SUBCASE("a list above the per-message cap is refused whole") {
        PeersMsg flood;
        for (uint32_t i = 0; i < kMaxPeersEntries + 1; ++i)
            flood.entries.push_back(
                PeerEntry{"p" + std::to_string(i), "tcp://h:1", "tcp://h:2", 1, 0});
        CHECK(decode_message(encode_message(Message{flood})).status ==
              DecodeStatus::TooManyEntries);
        CHECK_FALSE(decode_message(encode_message(Message{flood})).message.has_value());
    }

    SUBCASE("an endpoint that is not a tcp address is malformed") {
        PeersMsg bad{{PeerEntry{"b", "udp://10.0.0.2:5555", "tcp://10.0.0.2:5556", 1, 0}}};
        CHECK(decode_message(encode_message(Message{bad})).status == DecodeStatus::Malformed);
        PeersMsg no_port{{PeerEntry{"b", "tcp://10.0.0.2", "tcp://10.0.0.2:5556", 1, 0}}};
        CHECK(decode_message(encode_message(Message{no_port})).status == DecodeStatus::Malformed);
        PeersMsg port_zero{{PeerEntry{"b", "tcp://10.0.0.2:0", "tcp://10.0.0.2:5556", 1, 0}}};
        CHECK(decode_message(encode_message(Message{port_zero})).status == DecodeStatus::Malformed);
    }

    SUBCASE("an age from the future is refused") {
        PeersMsg stale{{PeerEntry{"b", "tcp://10.0.0.2:5555", "tcp://10.0.0.2:5556", 1,
                                  kMaxPeerEntryAgeMs + 1}}};
        CHECK(decode_message(encode_message(Message{stale})).status == DecodeStatus::Malformed);
    }

    SUBCASE("an empty list is a valid message") {
        const auto decoded = decode_message(encode_message(Message{PeersMsg{}}));
        REQUIRE(decoded.ok());
        CHECK(std::get<PeersMsg>(*decoded.message).entries.empty());
    }
}

TEST_CASE("a truncated frame is rejected") {
    const Bytes frame = encode_message(Message{FetchMsg{LogKey{"w", "c"}, 1, 2}});
    const Bytes short_frame(frame.begin(), frame.end() - 1);
    CHECK(decode_message(short_frame).status == DecodeStatus::Truncated);
    CHECK_FALSE(decode_message(short_frame).message.has_value());
    CHECK(decode_message(Bytes{0, 0}).status == DecodeStatus::Truncated);
}

TEST_CASE("an oversized frame is rejected before allocation") {
    // Declare 1 GiB in the length field: the decoder must refuse from the header alone.
    Bytes frame{0x40, 0x00, 0x00, 0x00, kMsgFetch};
    CHECK(decode_message(frame).status == DecodeStatus::Oversized);

    Bytes big{0x00, 0x10, 0x00, 0x01, kMsgFetch};  // just over kMaxFrameBytes
    CHECK(decode_message(big).status == DecodeStatus::Oversized);
}

TEST_CASE("trailing bytes after a message are rejected") {
    Bytes frame = encode_message(Message{CreditMsg{1}});
    frame.push_back(0);
    const auto head_frame = encode_message(Message{CreditMsg{1}});
    Bytes extended(head_frame.begin(), head_frame.end());
    extended.push_back(0xff);
    CHECK(decode_message(extended).status == DecodeStatus::Malformed);
}

TEST_CASE("an unassigned id is reported and does not decode") {
    Bytes frame{0x00, 0x00, 0x00, 0x02, 0x7e, 0x00};  // id 126: unassigned
    const auto result = decode_message(frame);
    CHECK(result.status == DecodeStatus::UnknownId);
    CHECK_FALSE(result.ok());
}

TEST_CASE("an unsupported protocol version is reported, not decoded") {
    const Bytes frame =
        encode_message(Message{HelloMsg{static_cast<uint8_t>(kProtocolVersion + 1), {}}});
    const auto result = decode_message(frame);
    CHECK(result.status == DecodeStatus::UnsupportedVersion);
    CHECK_FALSE(result.message.has_value());
}

TEST_CASE("a record on the wire reproduces the sender's hash") {
    const Record record = sample_record();
    Sha256Hasher hasher;
    const auto decoded = decode_record(encode_record(record));
    REQUIRE(decoded.has_value());
    CHECK(record_hash(*decoded, hasher) == record_hash(record, hasher));
    CHECK(decoded->sig == record.sig);
}

TEST_CASE("a record with a malformed signature field is rejected") {
    const Record record = sample_record();
    Bytes wire = encode_record(record);
    // Rewrite the signature length prefix (last 32+4 bytes) to claim more than exists.
    wire[wire.size() - 36] = 0x00;
    wire[wire.size() - 35] = 0x00;
    wire[wire.size() - 34] = 0x00;
    wire[wire.size() - 33] = 0xff;
    CHECK_FALSE(decode_record(wire).has_value());
}

// The decoder is the only place hostile bytes enter, so it gets the mutation fuzz (ENC-1).
TEST_CASE("mutation fuzz: decoding is total over arbitrary bytes") {
    mt19937_64 rng(20261116);
    uniform_int_distribution<int> bit(0, 7);
    uniform_int_distribution<size_t> position(0, 63);
    uniform_int_distribution<int> byte_value(0, 255);

    const auto seeds = every_message();
    for (int round = 0; round < 2000; ++round) {
        const auto& source = seeds[static_cast<size_t>(round) % seeds.size()];
        Bytes mutated = encode_message(source);
        const int edits = 1 + (round % 4);
        for (int e = 0; e < edits; ++e) {
            const auto kind = (round + e) % 3;
            if (kind == 0 && !mutated.empty()) {
                const auto index = position(rng) % mutated.size();
                mutated[index] = static_cast<uint8_t>(mutated[index] ^ (1u << bit(rng)));
            } else if (kind == 1 && !mutated.empty()) {
                mutated.resize(position(rng) % (mutated.size() + 1));
            } else {
                const auto index = position(rng) % (mutated.size() + 1);
                mutated.insert(mutated.begin() + static_cast<ptrdiff_t>(index),
                               static_cast<uint8_t>(byte_value(rng)));
            }
            // The point: no crash, no UB, no partial value - only a message or a status.
            const auto result = decode_message(mutated);
            REQUIRE((result.ok() || result.status != DecodeStatus::Ok));
            if (!result.ok())
                REQUIRE_FALSE(result.message.has_value());
        }
    }
}

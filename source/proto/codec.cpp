#include <mosaic/core/encoding.h>
#include <mosaic/proto/codec.h>

using std::decay_t;
using std::is_same_v;
using std::move;
using std::nullopt;
using std::optional;
using std::ptrdiff_t;
using std::string;
using std::uint32_t;
using std::uint8_t;
using std::vector;
using std::visit;

namespace mosaic {
namespace {

void put_log_key(ByteWriter& writer, const LogKey& key) {
    writer.put_string(key.writer);
    writer.put_string(key.channel);
}

LogKey get_log_key(ByteReader& reader) {
    return LogKey{reader.get_string(), reader.get_string()};
}

void put_patterns(ByteWriter& writer, const vector<string>& patterns) {
    writer.put_u32(static_cast<uint32_t>(patterns.size()));
    for (const auto& pattern : patterns)
        writer.put_string(pattern);
}

vector<string> get_patterns(ByteReader& reader) {
    const auto count = reader.get_u32();
    vector<string> patterns;
    // A hostile count is bounded by the bytes actually present: each entry needs at least its u32
    // length prefix, so asking the reader for them fails long before anything is allocated.
    if (count > reader.remaining() / 4)
        return patterns;
    patterns.reserve(count);
    for (uint32_t i = 0; i < count; ++i)
        patterns.push_back(reader.get_string());
    return patterns;
}

/// Decode a record from a reader over `body`, rejecting anything that does not re-encode to the
/// same bytes: a canonical encoding must have exactly one representation on the wire.
optional<Record> read_record(ByteReader& reader, const Bytes& body) {
    const auto start = reader.pos();

    Record record;
    reader.get_u8();  // record format version; only version 1 exists
    record.hash_algo = static_cast<HashAlgo>(reader.get_u8());
    record.writer = reader.get_string();
    record.channel = reader.get_string();
    record.seq = reader.get_u64();
    record.prev = reader.get_hash();
    record.timestamp_ms = reader.get_u64();
    record.content_type = reader.get_string();
    record.payload = reader.get_bytes();
    record.sig = reader.get_bytes();
    if (!reader.ok())
        return nullopt;

    const auto end = reader.pos();
    if (end <= start || end > body.size())
        return nullopt;
    const Bytes consumed(body.begin() + static_cast<ptrdiff_t>(start),
                         body.begin() + static_cast<ptrdiff_t>(end));

    Bytes reencoded = canonical_bytes(record);
    ByteWriter tail(reencoded);
    tail.put_bytes(record.sig);
    if (reencoded != consumed)
        return nullopt;  // not the canonical form of this record
    return record;
}

Bytes payload_bytes(const Message& message) {
    Bytes out;
    ByteWriter writer(out);
    visit(
        [&](const auto& value) {
            using T = decay_t<decltype(value)>;
            if constexpr (is_same_v<T, RecordMsg> || is_same_v<T, DirectRecordMsg>) {
                const Bytes record = encode_record(value.record);
                writer.put_raw(record.data(), record.size());
            } else if constexpr (is_same_v<T, HeadMsg> || is_same_v<T, DirectHeadMsg>) {
                put_log_key(writer, value.key);
                writer.put_u64(value.seq);
                writer.put_hash(value.hash);
            } else if constexpr (is_same_v<T, FetchMsg>) {
                put_log_key(writer, value.key);
                writer.put_u64(value.from);
                writer.put_u32(value.count);
            } else if constexpr (is_same_v<T, AckMsg>) {
                put_log_key(writer, value.key);
                writer.put_u64(value.seq);
            } else if constexpr (is_same_v<T, GetHeadsMsg>) {
                put_patterns(writer, value.patterns);
            } else if constexpr (is_same_v<T, CreditMsg>) {
                writer.put_u32(value.credits);
            } else if constexpr (is_same_v<T, HelloMsg>) {
                writer.put_u8(value.version);
                put_patterns(writer, value.patterns);
            }
        },
        message);
    return out;
}

}  // namespace

const char* to_string(DecodeStatus status) {
    switch (status) {
        case DecodeStatus::Ok:
            return "ok";
        case DecodeStatus::Truncated:
            return "truncated";
        case DecodeStatus::Oversized:
            return "oversized";
        case DecodeStatus::UnknownId:
            return "unknown-id";
        case DecodeStatus::Malformed:
            return "malformed";
        case DecodeStatus::UnsupportedVersion:
            return "unsupported-version";
    }
    return "?";
}

uint8_t message_id(const Message& message) {
    switch (message.index()) {
        case 0:
            return kMsgRecord;
        case 1:
            return kMsgDirectRecord;
        case 2:
            return kMsgHead;
        case 3:
            return kMsgFetch;
        case 4:
            return kMsgAck;
        case 5:
            return kMsgGetHeads;
        case 6:
            return kMsgDirectHead;
        case 7:
            return kMsgCredit;
        case 8:
            return kMsgHello;
        default:
            return 0;
    }
}

Bytes encode_record(const Record& record) {
    Bytes out = canonical_bytes(record);
    ByteWriter tail(out);
    tail.put_bytes(record.sig);
    return out;
}

optional<Record> decode_record(const Bytes& bytes) {
    ByteReader reader(bytes);
    auto record = read_record(reader, bytes);
    if (!record || !reader.at_end())
        return nullopt;
    return record;
}

Bytes encode_message(const Message& message) {
    Bytes frame;
    ByteWriter writer(frame);
    const Bytes payload = payload_bytes(message);
    writer.put_u32(static_cast<uint32_t>(payload.size() + 1));  // id byte + payload
    writer.put_u8(message_id(message));
    writer.put_raw(payload.data(), payload.size());
    return frame;
}

DecodeResult decode_message(const Bytes& frame) {
    if (frame.size() < 4)
        return {DecodeStatus::Truncated, nullopt};

    ByteReader header(frame);
    const auto declared = header.get_u32();
    if (declared > kMaxFrameBytes)
        return {DecodeStatus::Oversized, nullopt};  // rejected before any allocation
    if (declared < 1)
        return {DecodeStatus::Malformed, nullopt};
    if (frame.size() - 4 < declared)
        return {DecodeStatus::Truncated, nullopt};
    if (frame.size() - 4 > declared)
        return {DecodeStatus::Malformed, nullopt};

    const Bytes body(frame.begin() + 4, frame.end());
    ByteReader reader(body);
    const auto id = reader.get_u8();

    const auto finish = [&](Message message) {
        return reader.at_end() ? DecodeResult{DecodeStatus::Ok, move(message)}
                               : DecodeResult{DecodeStatus::Malformed, nullopt};
    };

    switch (id) {
        case kMsgRecord: {
            const Bytes rest(body.begin() + 1, body.end());
            const auto record = decode_record(rest);
            if (!record)
                return {DecodeStatus::Malformed, nullopt};
            return {DecodeStatus::Ok, Message{RecordMsg{*record}}};
        }
        case kMsgDirectRecord: {
            const Bytes rest(body.begin() + 1, body.end());
            const auto record = decode_record(rest);
            if (!record)
                return {DecodeStatus::Malformed, nullopt};
            return {DecodeStatus::Ok, Message{DirectRecordMsg{*record}}};
        }
        case kMsgHead: {
            HeadMsg message;
            message.key = get_log_key(reader);
            message.seq = reader.get_u64();
            message.hash = reader.get_hash();
            if (!reader.ok())
                return {DecodeStatus::Truncated, nullopt};
            return finish(Message{message});
        }
        case kMsgDirectHead: {
            DirectHeadMsg message;
            message.key = get_log_key(reader);
            message.seq = reader.get_u64();
            message.hash = reader.get_hash();
            if (!reader.ok())
                return {DecodeStatus::Truncated, nullopt};
            return finish(Message{message});
        }
        case kMsgFetch: {
            FetchMsg message;
            message.key = get_log_key(reader);
            message.from = reader.get_u64();
            message.count = reader.get_u32();
            if (!reader.ok())
                return {DecodeStatus::Truncated, nullopt};
            return finish(Message{message});
        }
        case kMsgAck: {
            AckMsg message;
            message.key = get_log_key(reader);
            message.seq = reader.get_u64();
            if (!reader.ok())
                return {DecodeStatus::Truncated, nullopt};
            return finish(Message{message});
        }
        case kMsgGetHeads: {
            GetHeadsMsg message;
            message.patterns = get_patterns(reader);
            if (!reader.ok())
                return {DecodeStatus::Truncated, nullopt};
            return finish(Message{message});
        }
        case kMsgCredit: {
            CreditMsg message;
            message.credits = reader.get_u32();
            if (!reader.ok())
                return {DecodeStatus::Truncated, nullopt};
            return finish(Message{message});
        }
        case kMsgHello: {
            HelloMsg message;
            message.version = reader.get_u8();
            message.patterns = get_patterns(reader);
            if (!reader.ok())
                return {DecodeStatus::Truncated, nullopt};
            if (message.version != kProtocolVersion)
                return {DecodeStatus::UnsupportedVersion, nullopt};  // reported, not decoded
            return finish(Message{message});
        }
        default:
            return {DecodeStatus::UnknownId, nullopt};  // ignore and count; the link continues
    }
}

}  // namespace mosaic

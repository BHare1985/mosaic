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
    return LogKey{.writer = reader.get_string(), .channel = reader.get_string()};
}

void put_patterns(ByteWriter& writer, const vector<string>& patterns) {
    writer.put_u32(static_cast<uint32_t>(patterns.size()));
    for (const auto& pattern : patterns) {
        writer.put_string(pattern);
    }
}

vector<string> get_patterns(ByteReader& reader) {
    const auto count = reader.get_u32();
    vector<string> patterns;
    // A hostile count is bounded by the bytes actually present: each entry needs at least its u32
    // length prefix, so asking the reader for them fails long before anything is allocated.
    if (count > reader.remaining() / 4) {
        return patterns;
    }
    patterns.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        patterns.push_back(reader.get_string());
    }
    return patterns;
}

/// A peer endpoint as this protocol carries it: "tcp://host:port" with a non-empty host and a port
/// in range. Checked on decode because an entry that survives this becomes something a node may try
/// to dial, and "try to dial nonsense" is exactly what a hostile peer list is for (DISC-3).
}  // namespace

bool valid_peer_endpoint(const std::string& endpoint) {
    constexpr const char* kScheme = "tcp://";
    const auto scheme = sizeof("tcp://") - 1;
    // Bounded before anything is compared: sixty-four entries of four-kilobyte hostnames is a
    // quarter-megabyte frame that is all endpoint and no peer.
    if (endpoint.size() <= scheme || endpoint.size() > 128 ||
        endpoint.compare(0, scheme, kScheme) != 0) {
        return false;
    }
    const auto colon = endpoint.rfind(':');
    if (colon == string::npos || colon <= scheme || colon + 1 >= endpoint.size()) {
        return false;
    }
    if (endpoint.find(':', scheme) != colon) {
        return false;  // an unbracketed IPv6 literal is not something we can dial
    }
    // A wildcard is a *bind* address, never a reachable one: a node that advertises one is telling
    // us where it listens, not where it can be found.
    const auto host = endpoint.substr(scheme, colon - scheme);
    if (host == "*" || host == "0.0.0.0" || host == "[::]" || host == "::" || host == "[::0]") {
        return false;
    }
    uint32_t port = 0;
    for (size_t i = colon + 1; i < endpoint.size(); ++i) {
        const char c = endpoint[i];
        if (c < '0' || c > '9') {
            return false;
        }
        port = (port * 10) + static_cast<uint32_t>(c - '0');
        if (port > 65535) {
            return false;
        }
    }
    return port != 0;
}

namespace {

void put_peers(ByteWriter& writer, const vector<PeerEntry>& entries) {
    writer.put_u32(static_cast<uint32_t>(entries.size()));
    for (const auto& entry : entries) {
        writer.put_string(entry.id);
        writer.put_string(entry.pub_endpoint);
        writer.put_string(entry.router_endpoint);
        writer.put_u32(entry.hops);
        writer.put_u64(entry.age_ms);
    }
}

/// Decode a peer list. Returns empty on a clean list of zero entries, and signals failure with a
/// status through `status` rather than a partial vector: a message is believed whole or not at all.
vector<PeerEntry> get_peers(ByteReader& reader, DecodeStatus& status) {
    vector<PeerEntry> entries;
    const auto count = reader.get_u32();
    if (!reader.ok()) {
        status = DecodeStatus::Truncated;
        return entries;
    }
    if (count > kMaxPeersEntries) {
        status = DecodeStatus::TooManyEntries;
        return entries;
    }
    // Each entry needs at least a u32 length prefix per string and its two fixed fields: a hostile
    // count is bounded by the bytes actually present before anything is allocated.
    if (count > reader.remaining() / 12) {
        status = DecodeStatus::Truncated;
        return entries;
    }
    entries.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        PeerEntry entry;
        entry.id = reader.get_string();
        entry.pub_endpoint = reader.get_string();
        entry.router_endpoint = reader.get_string();
        entry.hops = reader.get_u32();
        entry.age_ms = reader.get_u64();
        if (!reader.ok()) {
            status = DecodeStatus::Truncated;
            return {};
        }
        if (entry.id.empty() || entry.id.size() > 255 || !valid_peer_endpoint(entry.pub_endpoint) ||
            !valid_peer_endpoint(entry.router_endpoint) || entry.age_ms > kMaxPeerEntryAgeMs) {
            status = DecodeStatus::Malformed;
            return {};
        }
        entries.push_back(std::move(entry));
    }
    status = DecodeStatus::Ok;
    return entries;
}

/// Decode a record from a reader over `body`, rejecting anything that does not re-encode to the
/// same bytes: a canonical encoding must have exactly one representation on the wire.
optional<Record> read_record(ByteReader& reader, const Bytes& body) {
    const auto start = reader.pos();

    Record record;
    reader.get_u8();  // record format version; only version 1 exists
    const auto algorithm = reader.get_u8();
    if (algorithm < static_cast<uint8_t>(HashAlgo::Sha256) ||
        algorithm > static_cast<uint8_t>(HashAlgo::Blake2b)) {
        return nullopt;  // an unknown tag is a malformed record, not an algorithm we can guess at
    }
    record.hash_algo = static_cast<HashAlgo>(algorithm);
    record.writer = reader.get_string();
    record.channel = reader.get_string();
    record.seq = reader.get_u64();
    record.prev = reader.get_hash();
    record.timestamp_ms = reader.get_u64();
    record.content_type = reader.get_string();
    record.payload = reader.get_bytes();
    record.sig = reader.get_bytes();
    if (!reader.ok()) {
        return nullopt;
    }

    const auto end = reader.pos();
    if (end <= start || end > body.size()) {
        return nullopt;
    }
    const Bytes consumed(body.begin() + static_cast<ptrdiff_t>(start),
                         body.begin() + static_cast<ptrdiff_t>(end));

    Bytes reencoded = canonical_bytes(record);
    ByteWriter tail(reencoded);
    tail.put_bytes(record.sig);
    if (reencoded != consumed) {
        return nullopt;  // not the canonical form of this record
    }
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
                writer.put_u8(value.spare_capacity);
            } else if constexpr (is_same_v<T, PeersMsg>) {
                put_peers(writer, value.entries);
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
        case DecodeStatus::TooManyEntries:
            return "too-many-entries";
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
        case 9:
            return kMsgPeers;
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
    if (!record || !reader.at_end()) {
        return nullopt;
    }
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
    if (frame.size() < 4) {
        return {.status = DecodeStatus::Truncated, .message = nullopt};
    }

    ByteReader header(frame);
    const auto declared = header.get_u32();
    if (declared > kMaxFrameBytes) {
        return {.status = DecodeStatus::Oversized,
                .message = nullopt};  // rejected before any allocation
    }
    if (declared < 1) {
        return {.status = DecodeStatus::Malformed, .message = nullopt};
    }
    if (frame.size() - 4 < declared) {
        return {.status = DecodeStatus::Truncated, .message = nullopt};
    }
    if (frame.size() - 4 > declared) {
        return {.status = DecodeStatus::Malformed, .message = nullopt};
    }

    const Bytes body(frame.begin() + 4, frame.end());
    ByteReader reader(body);
    const auto id = reader.get_u8();

    const auto finish = [&](Message message) {
        return reader.at_end()
                   ? DecodeResult{.status = DecodeStatus::Ok, .message = std::move(message)}
                   : DecodeResult{.status = DecodeStatus::Malformed, .message = nullopt};
    };

    switch (id) {
        case kMsgRecord: {
            const Bytes rest(body.begin() + 1, body.end());
            const auto record = decode_record(rest);
            if (!record) {
                return {.status = DecodeStatus::Malformed, .message = nullopt};
            }
            return {.status = DecodeStatus::Ok, .message = Message{RecordMsg{*record}}};
        }
        case kMsgDirectRecord: {
            const Bytes rest(body.begin() + 1, body.end());
            const auto record = decode_record(rest);
            if (!record) {
                return {.status = DecodeStatus::Malformed, .message = nullopt};
            }
            return {.status = DecodeStatus::Ok, .message = Message{DirectRecordMsg{*record}}};
        }
        case kMsgHead: {
            HeadMsg message;
            message.key = get_log_key(reader);
            message.seq = reader.get_u64();
            message.hash = reader.get_hash();
            if (!reader.ok()) {
                return {.status = DecodeStatus::Truncated, .message = nullopt};
            }
            return finish(Message{message});
        }
        case kMsgDirectHead: {
            DirectHeadMsg message;
            message.key = get_log_key(reader);
            message.seq = reader.get_u64();
            message.hash = reader.get_hash();
            if (!reader.ok()) {
                return {.status = DecodeStatus::Truncated, .message = nullopt};
            }
            return finish(Message{message});
        }
        case kMsgFetch: {
            FetchMsg message;
            message.key = get_log_key(reader);
            message.from = reader.get_u64();
            message.count = reader.get_u32();
            if (!reader.ok()) {
                return {.status = DecodeStatus::Truncated, .message = nullopt};
            }
            return finish(Message{message});
        }
        case kMsgAck: {
            AckMsg message;
            message.key = get_log_key(reader);
            message.seq = reader.get_u64();
            if (!reader.ok()) {
                return {.status = DecodeStatus::Truncated, .message = nullopt};
            }
            return finish(Message{message});
        }
        case kMsgGetHeads: {
            GetHeadsMsg message;
            message.patterns = get_patterns(reader);
            if (!reader.ok()) {
                return {.status = DecodeStatus::Truncated, .message = nullopt};
            }
            return finish(Message{message});
        }
        case kMsgCredit: {
            CreditMsg message;
            message.credits = reader.get_u32();
            if (!reader.ok()) {
                return {.status = DecodeStatus::Truncated, .message = nullopt};
            }
            return finish(Message{message});
        }
        case kMsgHello: {
            HelloMsg message;
            message.version = reader.get_u8();
            message.patterns = get_patterns(reader);
            message.spare_capacity = reader.get_u8();
            if (!reader.ok()) {
                return {.status = DecodeStatus::Truncated, .message = nullopt};
            }
            if (message.version != kProtocolVersion) {
                return {.status = DecodeStatus::UnsupportedVersion,
                        .message = nullopt};  // reported, not decoded
            }
            return finish(Message{message});
        }
        case kMsgPeers: {
            PeersMsg message;
            auto status = DecodeStatus::Ok;
            message.entries = get_peers(reader, status);
            if (status != DecodeStatus::Ok) {
                return {.status = status, .message = nullopt};
            }
            if (!reader.ok()) {
                return {.status = DecodeStatus::Truncated, .message = nullopt};
            }
            return finish(Message{message});
        }
        default:
            return {.status = DecodeStatus::UnknownId,
                    .message = nullopt};  // ignore and count; the link continues
    }
}

}  // namespace mosaic

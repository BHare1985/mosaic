// Implementation of the C ABI. Everything here is a thin adapter: no verification, no policy and no
// state of its own beyond what a C caller needs to hold a handle. Contract:
// openspec/specs/api/c-abi.

#include <mosaic/core/keystore.h>
#include <mosaic/mosaic_c.h>
#include <mosaic/node/channel_rules.h>
#include <mosaic/node/node.h>
#include <mosaic/store/sqlite_log_store.h>
#include <mosaic/version.h>

#ifdef MOSAIC_HAS_ZMQ
#include <mosaic/net/zmq_transport.h>
#endif

#include <algorithm>
#include <memory>
#include <string>

// The handles are opaque to C; they are our C++ types with C linkage names.
struct mosaic_store {
    std::unique_ptr<mosaic::SqliteLogStore> store;
};

struct mosaic_keys {
    mosaic::Keystore keystore;
#ifdef MOSAIC_HAS_SODIUM
    std::unique_ptr<mosaic::Ed25519Signer> signer;
    std::unique_ptr<mosaic::Ed25519Verifier> verifier;
#endif
};

struct mosaic_transport {
    std::unique_ptr<mosaic::Transport> transport;
#ifdef MOSAIC_HAS_ZMQ
    mosaic::ZmqTransport* zmq = nullptr;  // same object as transport, for endpoint queries
#endif
};

struct mosaic_node {
    // Order matters: the node holds references to everything above it.
    std::unique_ptr<mosaic_transport> transport;  // only set when the ABI created it
    std::unique_ptr<mosaic::Sha256Hasher> hasher;
#ifdef MOSAIC_HAS_SODIUM
    std::unique_ptr<mosaic::Ed25519Verifier> verifier;
#endif
    std::unique_ptr<mosaic::ChannelRules> rules;
    const mosaic::Signer* signer =
        nullptr;  // borrowed: the mosaic_keys handle must outlive the node
    std::unique_ptr<mosaic::Node> node;
    mosaic_record_fn callback = nullptr;
    void* user = nullptr;
};

struct mosaic_record_owned {
    std::string writer;
    std::string channel;
    std::string content_type;
    std::string payload;
    std::uint64_t seq = 0;
    std::uint64_t timestamp_ms = 0;
};

namespace {
// Per-thread by design: the ABI reports the last error straight after a failing call, and a
// process-wide buffer would hand one thread another's message.
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
thread_local std::string g_last_error;

mosaic_status fail(mosaic_status status, const std::string& message) {
    g_last_error = message;
    return status;
}

char* copy_out(const std::string& text) {
    auto* buffer = new char[text.size() + 1];
    std::ranges::copy(text, buffer);
    buffer[text.size()] = '\0';
    return buffer;
}

bool valid(const char* text) {
    return text != nullptr && *text != '\0';
}
}  // namespace

extern "C" {

const char* mosaic_version(void) {
    return mosaic::kVersion.data();
}

const char* mosaic_last_error(void) {
    return g_last_error.c_str();
}

void mosaic_free_string(char** text) {
    if (text != nullptr && *text != nullptr) {
        delete[] *text;
        *text = nullptr;
    }
}

// ---- store ----

mosaic_status mosaic_store_open(const char* path, int durability, mosaic_store** out) {
    if (out != nullptr) {
        *out = nullptr;
    }
    if (!valid(path) || out == nullptr) {
        return fail(MOSAIC_ERR_INVALID_ARGUMENT, "mosaic_store_open: path and out are required");
    }
#ifndef MOSAIC_HAS_SQLITE
    return fail(MOSAIC_ERR_UNSUPPORTED, "this build has no durable store");
#else
    const auto mode = durability == 1 ? mosaic::Durability::Batched : mosaic::Durability::PerRecord;
    auto opened = mosaic::SqliteLogStore::open(path, mode);
    if (!opened) {
        return fail(MOSAIC_ERR_IO, std::string("cannot open store: ") + path);
    }
    auto* handle = new mosaic_store();
    handle->store = std::move(*opened);
    *out = handle;
    return MOSAIC_OK;
#endif
}

void mosaic_store_free(mosaic_store** store) {
    if (store != nullptr && *store != nullptr) {
        delete *store;  // a second call is a no-op because the pointer is cleared
        *store = nullptr;
    }
}

// ---- keys ----

#ifndef MOSAIC_HAS_SODIUM
namespace {
mosaic_status need_crypto(const char* what) {
    return fail(MOSAIC_ERR_UNSUPPORTED,
                std::string(what) + ": this build has no signing support (MOSAIC_WITH_SODIUM=OFF)");
}
}  // namespace
#endif

mosaic_status mosaic_keys_load(const char* path, mosaic_keys** out) {
    if (out != nullptr) {
        *out = nullptr;
    }
    if (!valid(path) || out == nullptr) {
        return fail(MOSAIC_ERR_INVALID_ARGUMENT, "mosaic_keys_load: path and out are required");
    }
    const auto loaded = mosaic::load_keystore(path);
    if (!loaded) {
        return fail(MOSAIC_ERR_CRYPTO, std::string("cannot load keys: ") + path);
    }
    auto* handle = new mosaic_keys();
    handle->keystore = *loaded;
#ifdef MOSAIC_HAS_SODIUM
    handle->signer = std::make_unique<mosaic::Ed25519Signer>(handle->keystore.public_key,
                                                             handle->keystore.secret_key);
    handle->verifier = std::make_unique<mosaic::Ed25519Verifier>();
#else
    delete handle;
    return fail(MOSAIC_ERR_UNSUPPORTED, "this build has no signing support");
#endif
    *out = handle;
    return MOSAIC_OK;
}

mosaic_status mosaic_keys_curve(mosaic_keys* keys, char** curve_public, char** curve_secret) {
    if (curve_public != nullptr) {
        *curve_public = nullptr;
    }
    if (curve_secret != nullptr) {
        *curve_secret = nullptr;
    }
    if (keys == nullptr) {
        return fail(MOSAIC_ERR_INVALID_ARGUMENT, "mosaic_keys_curve: keys is required");
    }
#ifdef MOSAIC_HAS_SODIUM
    // The curve25519 pair derived from the Ed25519 identity: one keystore entry yields both the key
    // that signs records and the key that authenticates links (SEC-1).
    const auto curve = mosaic::curve_identity_from_keystore(keys->keystore);
    if (!curve) {
        return fail(MOSAIC_ERR_CRYPTO, "this build cannot derive curve keys from that keystore");
    }
    if (curve_public != nullptr) {
        *curve_public = copy_out(curve->public_z85);
    }
    if (curve_secret != nullptr) {
        *curve_secret = copy_out(curve->secret_z85);
    }
    return MOSAIC_OK;
#else
    return fail(MOSAIC_ERR_UNSUPPORTED, "this build has no crypto");
#endif
}

mosaic_status mosaic_keys_generate(const char* path, char** identity) {
    if (identity != nullptr) {
        *identity = nullptr;
    }
    if (!valid(path)) {
        return fail(MOSAIC_ERR_INVALID_ARGUMENT, "mosaic_keys_generate: path is required");
    }
    const auto generated = mosaic::generate_keystore();
    if (!generated) {
        return fail(MOSAIC_ERR_UNSUPPORTED, "this build has no signing support");
    }
    if (!mosaic::save_keystore(*generated, path)) {
        return fail(MOSAIC_ERR_IO, std::string("cannot write keys: ") + path);
    }
    if (identity != nullptr) {
        *identity = copy_out(generated->id());
    }
    return MOSAIC_OK;
}

void mosaic_keys_free(mosaic_keys** keys) {
    if (keys != nullptr && *keys != nullptr) {
        delete *keys;
        *keys = nullptr;
    }
}

// ---- transport ----

namespace {

mosaic_status transport_new(const char* self, const char* pub_bind, const char* router_bind,
                            const mosaic_peer* peers, size_t peer_count, const char* curve_public,
                            const char* curve_secret, bool plaintext, mosaic_transport** out) {
    if (out != nullptr) {
        *out = nullptr;
    }
    if (!valid(self) || !valid(pub_bind) || !valid(router_bind) || out == nullptr) {
        return fail(MOSAIC_ERR_INVALID_ARGUMENT,
                    "mosaic_transport_new: self, both binds and out are required");
    }
    if (!plaintext && (curve_public == nullptr || curve_secret == nullptr)) {
        return fail(MOSAIC_ERR_INVALID_ARGUMENT,
                    "mosaic_transport_new: curve keys are required - use "
                    "mosaic_transport_new_plaintext to ask for an unencrypted transport");
    }
#ifndef MOSAIC_HAS_ZMQ
    (void)peers;
    (void)peer_count;
    (void)curve_public;
    (void)curve_secret;
    (void)plaintext;
    return fail(MOSAIC_ERR_UNSUPPORTED, "this build has no network transport");
#else
    mosaic::ZmqConfig config;
    config.id = self;
    config.pub_bind = pub_bind;
    config.router_bind = router_bind;
    if (!plaintext)
        config.curve = mosaic::ZmqCurveConfig{true, curve_public, curve_secret, ""};
    for (size_t i = 0; i < peer_count; ++i) {
        mosaic::ZmqPeerEndpoint endpoint;
        endpoint.id = peers[i].id != nullptr ? peers[i].id : "";
        endpoint.pub_endpoint = peers[i].pub_endpoint != nullptr ? peers[i].pub_endpoint : "";
        endpoint.router_endpoint =
            peers[i].router_endpoint != nullptr ? peers[i].router_endpoint : endpoint.pub_endpoint;
        endpoint.curve_public_z85 = peers[i].curve_public != nullptr ? peers[i].curve_public : "";
        if (!endpoint.id.empty() && !endpoint.pub_endpoint.empty())
            config.peers.push_back(endpoint);
    }
    auto created = std::make_unique<mosaic::ZmqTransport>(config);
    if (!created->is_bound())
        return fail(MOSAIC_ERR_IO, std::string("cannot bind ") + pub_bind + " or " + router_bind);
    auto* handle = new mosaic_transport();
    handle->zmq = created.get();
    handle->transport = std::move(created);
    *out = handle;
    return MOSAIC_OK;
#endif
}

}  // namespace

mosaic_status mosaic_transport_new(const char* self, const char* pub_bind, const char* router_bind,
                                   const mosaic_peer* peers, size_t peer_count,
                                   const char* curve_public, const char* curve_secret,
                                   mosaic_transport** out) {
    return transport_new(self, pub_bind, router_bind, peers, peer_count, curve_public, curve_secret,
                         false, out);
}

mosaic_status mosaic_transport_new_plaintext(const char* self, const char* pub_bind,
                                             const char* router_bind, const mosaic_peer* peers,
                                             size_t peer_count, mosaic_transport** out) {
    return transport_new(self, pub_bind, router_bind, peers, peer_count, nullptr, nullptr, true,
                         out);
}

mosaic_status mosaic_transport_add_peer(mosaic_transport* transport, const mosaic_peer* peer) {
    if (transport == nullptr || peer == nullptr || peer->id == nullptr ||
        peer->pub_endpoint == nullptr) {
        return fail(MOSAIC_ERR_INVALID_ARGUMENT,
                    "mosaic_transport_add_peer: transport, peer.id and pub_endpoint are required");
    }
#ifdef MOSAIC_HAS_ZMQ
    mosaic::ZmqPeerEndpoint endpoint;
    endpoint.id = peer->id;
    endpoint.pub_endpoint = peer->pub_endpoint;
    endpoint.router_endpoint =
        peer->router_endpoint != nullptr ? peer->router_endpoint : peer->pub_endpoint;
    endpoint.curve_public_z85 = peer->curve_public != nullptr ? peer->curve_public : "";
    transport->zmq->add_peer(endpoint);
    return MOSAIC_OK;
#else
    return fail(MOSAIC_ERR_UNSUPPORTED, "this build has no network transport");
#endif
}

mosaic_status mosaic_transport_endpoints(const mosaic_transport* transport, char** pub_endpoint,
                                         char** router_endpoint) {
    if (pub_endpoint != nullptr) {
        *pub_endpoint = nullptr;
    }
    if (router_endpoint != nullptr) {
        *router_endpoint = nullptr;
    }
    if (transport == nullptr || transport->transport == nullptr) {
        return fail(MOSAIC_ERR_INVALID_ARGUMENT,
                    "mosaic_transport_endpoints: transport is required");
    }
#ifdef MOSAIC_HAS_ZMQ
    if (pub_endpoint != nullptr)
        *pub_endpoint = copy_out(transport->zmq->pub_endpoint());
    if (router_endpoint != nullptr)
        *router_endpoint = copy_out(transport->zmq->router_endpoint());
    return MOSAIC_OK;
#else
    return fail(MOSAIC_ERR_UNSUPPORTED, "this build has no network transport");
#endif
}

void mosaic_transport_free(mosaic_transport** transport) {
    if (transport != nullptr && *transport != nullptr) {
        delete *transport;
        *transport = nullptr;
    }
}

// ---- node ----

mosaic_status mosaic_node_new(const mosaic_node_config* config, mosaic_transport* transport,
                              mosaic_store* store, mosaic_keys* keys, mosaic_node** out) {
#ifndef MOSAIC_HAS_SODIUM
    (void)config;
    (void)transport;
    (void)store;
    (void)keys;
    if (out != nullptr)
        *out = nullptr;
    return need_crypto("mosaic_node_new");
#else
    if (out != nullptr) {
        *out = nullptr;
    }
    if (config == nullptr || !valid(config->id) || transport == nullptr || store == nullptr ||
        keys == nullptr || out == nullptr) {
        return fail(MOSAIC_ERR_INVALID_ARGUMENT,
                    "mosaic_node_new: config.id, transport, store, keys and out are required");
    }

    auto* handle = new mosaic_node();
    handle->hasher = std::make_unique<mosaic::Sha256Hasher>();
    handle->verifier = std::make_unique<mosaic::Ed25519Verifier>();

    mosaic::NodeConfig node_config;
    node_config.id = config->id;
    if (config->head_interval_ms != 0) {
        node_config.head_interval_ms = config->head_interval_ms;
    }
    if (config->fetch_batch != 0) {
        node_config.fetch_batch = config->fetch_batch;
    }
    if (config->fetch_window != 0) {
        node_config.fetch_window = config->fetch_window;
    }
    if (config->fetch_timeout_ms != 0) {
        node_config.fetch_timeout_ms = config->fetch_timeout_ms;
    }
    if (config->max_serve != 0) {
        node_config.max_serve = config->max_serve;
    }
    if (config->max_record_bytes != 0) {
        node_config.max_record_bytes = config->max_record_bytes;
    }
    node_config.min_replicas = config->min_replicas;  // zero is meaningful here: best effort

    handle->signer = keys->signer.get();
    handle->node = std::make_unique<mosaic::Node>(node_config, *transport->transport, *store->store,
                                                  *handle->hasher, *handle->verifier);
    // The callback lives in the handle and forwards to the C function pointer, so the user's
    // pointer survives.
    handle->node->on_record([handle](const mosaic::Record& record) {
        if (handle->callback == nullptr) {
            return;
        }
        mosaic_record view;
        view.writer = record.writer.c_str();
        view.channel = record.channel.c_str();
        view.seq = record.seq;
        view.timestamp_ms = record.timestamp_ms;
        view.content_type = record.content_type.c_str();
        view.payload = record.payload.data();
        view.payload_len = record.payload.size();
        handle->callback(handle->user, &view);
    });
    *out = handle;
    return MOSAIC_OK;
#endif
}

void mosaic_node_free(mosaic_node** node) {
    if (node != nullptr && *node != nullptr) {
        delete *node;
        *node = nullptr;
    }
}

mosaic_status mosaic_node_follow(mosaic_node* node, const char* pattern) {
    if (node == nullptr || !valid(pattern)) {
        return fail(MOSAIC_ERR_INVALID_ARGUMENT,
                    "mosaic_node_follow: node and pattern are required");
    }
    if (!node->node->follow(pattern)) {
        return fail(MOSAIC_ERR_INVALID_ARGUMENT, std::string("invalid pattern: ") + pattern);
    }
    return MOSAIC_OK;
}

mosaic_status mosaic_node_on_record(mosaic_node* node, mosaic_record_fn callback, void* user) {
    if (node == nullptr) {
        return fail(MOSAIC_ERR_INVALID_ARGUMENT, "mosaic_node_on_record: node is required");
    }
    node->callback = callback;
    node->user = user;
    return MOSAIC_OK;
}

mosaic_status mosaic_node_allow_writers(mosaic_node* node, const char* pattern,
                                        const char** writers, size_t count) {
    if (node == nullptr || !valid(pattern) || (count > 0 && writers == nullptr)) {
        return fail(MOSAIC_ERR_INVALID_ARGUMENT,
                    "mosaic_node_allow_writers: node, pattern and writers are required");
    }
    if (!node->rules) {
        node->rules = std::make_unique<mosaic::ChannelRules>();
    }
    std::vector<mosaic::WriterId> allowed;
    for (size_t i = 0; i < count; ++i) {
        if (writers[i] != nullptr) {
            allowed.emplace_back(writers[i]);
        }
    }
    if (!node->rules->allow(pattern, allowed)) {
        return fail(MOSAIC_ERR_INVALID_ARGUMENT, std::string("invalid pattern: ") + pattern);
    }
    node->node->set_rules(node->rules.get());
    return MOSAIC_OK;
}

mosaic_status mosaic_node_publish(mosaic_node* node, const char* channel, const char* content_type,
                                  const uint8_t* payload, size_t payload_len, uint64_t now_ms,
                                  uint64_t* out_seq) {
    if (out_seq != nullptr) {
        *out_seq = 0;
    }
    if (node == nullptr || !valid(channel) || (payload_len > 0 && payload == nullptr)) {
        return fail(MOSAIC_ERR_INVALID_ARGUMENT,
                    "mosaic_node_publish: node, channel and payload are required");
    }
    mosaic::Bytes bytes;
    if (payload_len > 0) {
        bytes.assign(payload, payload + payload_len);
    }
    const auto record =
        node->node->publish(*node->signer, channel, content_type != nullptr ? content_type : "",
                            std::move(bytes), now_ms);
    if (!record) {
        return fail(
            MOSAIC_ERR_REFUSED,
            "the node refused the record: oversized, or the channel does not allow this writer");
    }
    if (out_seq != nullptr) {
        *out_seq = record->seq;
    }
    return MOSAIC_OK;
}

void mosaic_node_pump(mosaic_node* node, uint64_t now_ms) {
    if (node != nullptr && node->node != nullptr) {
        node->node->pump(now_ms);
    }
}

void mosaic_node_tick(mosaic_node* node, uint64_t now_ms) {
    if (node != nullptr && node->node != nullptr) {
        node->node->tick(now_ms);
    }
}

// ---- copies ----

mosaic_status mosaic_record_clone(const mosaic_record* record, mosaic_record_owned** out) {
    if (out != nullptr) {
        *out = nullptr;
    }
    if (record == nullptr || out == nullptr) {
        return fail(MOSAIC_ERR_INVALID_ARGUMENT,
                    "mosaic_record_clone: record and out are required");
    }
    auto* copy = new mosaic_record_owned();
    copy->writer = record->writer != nullptr ? record->writer : "";
    copy->channel = record->channel != nullptr ? record->channel : "";
    copy->content_type = record->content_type != nullptr ? record->content_type : "";
    if (record->payload != nullptr && record->payload_len > 0) {
        copy->payload.assign(reinterpret_cast<const char*>(record->payload), record->payload_len);
    }
    copy->seq = record->seq;
    copy->timestamp_ms = record->timestamp_ms;
    *out = copy;
    return MOSAIC_OK;
}

mosaic_status mosaic_record_owned_get(const mosaic_record_owned* owned, mosaic_record* out) {
    if (owned == nullptr || out == nullptr) {
        return fail(MOSAIC_ERR_INVALID_ARGUMENT,
                    "mosaic_record_owned_get: owned and out are required");
    }
    out->writer = owned->writer.c_str();
    out->channel = owned->channel.c_str();
    out->seq = owned->seq;
    out->timestamp_ms = owned->timestamp_ms;
    out->content_type = owned->content_type.c_str();
    out->payload = reinterpret_cast<const std::uint8_t*>(owned->payload.data());
    out->payload_len = owned->payload.size();
    return MOSAIC_OK;
}

void mosaic_record_owned_free(mosaic_record_owned** owned) {
    if (owned != nullptr && *owned != nullptr) {
        delete *owned;
        *owned = nullptr;
    }
}

}  // extern "C"

#ifndef MOSAIC_C_H
#define MOSAIC_C_H

/* Mosaic C ABI: enough surface to publish and to tail over logs, for callers that are not C++.
 *
 * Contract: openspec/specs/api/c-abi.
 *
 * Rules this header keeps:
 *   - It compiles as C. No C++ types, no templates, no variadic macros, no exceptions.
 *   - Errors are values. Every call that can fail returns mosaic_status and, when useful, fills a
 * message you free.
 *   - Every handle has one owner. Constructors return a handle through an out parameter; the
 * matching free tolerates being called twice.
 *   - Records are borrowed for the duration of a callback. Copy one with mosaic_record_clone if you
 * keep it.
 *
 * Threading: everything is single-threaded, like the library. One thread calls pump/tick/publish;
 * the transport is not safe to share across threads.
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum mosaic_status {
    MOSAIC_OK = 0,
    MOSAIC_ERR_INVALID_ARGUMENT = 1, /* a null handle or a malformed string */
    MOSAIC_ERR_IO = 2,               /* the store, keystore or config could not be opened */
    MOSAIC_ERR_CRYPTO = 3,           /* keys could not be generated or loaded */
    MOSAIC_ERR_REFUSED = 4,          /* the node refused the operation (channel rule, size limit) */
    MOSAIC_ERR_UNSUPPORTED = 5       /* this build lacks the capability (no libsodium, no ZeroMQ) */
} mosaic_status;

/* Version of the library, e.g. "0.6.0". Static storage: do not free. */
const char* mosaic_version(void);

/* ---------------------------------------------------------------------------------------------------------------
 * Handles
 * ------------------------------------------------------------------------------------------------------------
 */

typedef struct mosaic_store mosaic_store;
typedef struct mosaic_keys mosaic_keys;
typedef struct mosaic_transport mosaic_transport;
typedef struct mosaic_node mosaic_node;
typedef struct mosaic_record_owned mosaic_record_owned;

/* A record as handed to a callback: every pointer is borrowed and valid only for that call. */
typedef struct mosaic_record {
    const char* writer;
    const char* channel;
    uint64_t seq;
    uint64_t timestamp_ms;
    const char* content_type;
    const uint8_t* payload;
    size_t payload_len;
} mosaic_record;

/* Receives one record per accepted sequence, in order. */
typedef void (*mosaic_record_fn)(void* user, const mosaic_record* record);

/* ---------------------------------------------------------------------------------------------------------------
 * Storage
 * ------------------------------------------------------------------------------------------------------------
 */

/* 0 = commit before reporting success; 1 = batch (a crash may lose the tail on power loss). */
mosaic_status mosaic_store_open(const char* path, int durability, mosaic_store** out);
void mosaic_store_free(mosaic_store** store);

/* ---------------------------------------------------------------------------------------------------------------
 * Keys
 * ------------------------------------------------------------------------------------------------------------
 */

/* Load the key material written by `Mosaic keygen` or mosaic_keys_generate. */
mosaic_status mosaic_keys_load(const char* path, mosaic_keys** out);

/* Generate a new key pair, save it with owner-only permissions, and copy its hex identity into
 * `identity`. The caller frees `identity` with mosaic_free_string. */
mosaic_status mosaic_keys_generate(const char* path, char** identity);
void mosaic_keys_free(mosaic_keys** keys);

/* ---------------------------------------------------------------------------------------------------------------
 * Transport
 * ------------------------------------------------------------------------------------------------------------
 */

/* One peer to dial. Endpoints are ZeroMQ strings such as "tcp://127.0.0.1:6100". */
typedef struct mosaic_peer {
    const char* id;
    const char* pub_endpoint;
    const char* router_endpoint;
    const char* curve_public; /* may be NULL when links are not encrypted */
} mosaic_peer;

/* self, the two bind addresses, an optional peer array, and optional CURVE keys (NULL disables
 * encryption). */
mosaic_status mosaic_transport_new(const char* self, const char* pub_bind, const char* router_bind,
                                   const mosaic_peer* peers, size_t peer_count,
                                   const char* curve_public, const char* curve_secret,
                                   mosaic_transport** out);

/* Dial a peer discovered after construction (needed when the bind address asked for an ephemeral
 * port, and by discovery). Idempotent for a peer already wired. */
mosaic_status mosaic_transport_add_peer(mosaic_transport* transport, const mosaic_peer* peer);

/* The endpoints actually bound, useful when the bind address asked for an ephemeral port. */
mosaic_status mosaic_transport_endpoints(const mosaic_transport* transport, char** pub_endpoint,
                                         char** router_endpoint);

void mosaic_transport_free(mosaic_transport** transport);

/* ---------------------------------------------------------------------------------------------------------------
 * Node
 * ------------------------------------------------------------------------------------------------------------
 */

/* Everything the node needs. Fields left at zero take the library defaults. */
typedef struct mosaic_node_config {
    const char* id;
    uint32_t head_interval_ms;
    uint32_t fetch_batch;
    uint32_t fetch_window;
    uint32_t fetch_timeout_ms;
    uint32_t max_serve;
    uint32_t min_replicas;
    uint32_t max_record_bytes;
} mosaic_node_config;

mosaic_status mosaic_node_new(const mosaic_node_config* config, mosaic_transport* transport,
                              mosaic_store* store, mosaic_keys* keys, mosaic_node** out);
void mosaic_node_free(mosaic_node** node);

/* Follow a channel pattern. Records on matching channels are replicated and passed to the callback.
 */
mosaic_status mosaic_node_follow(mosaic_node* node, const char* pattern);

/* Called once per accepted record, in sequence order, for records from the network. */
mosaic_status mosaic_node_on_record(mosaic_node* node, mosaic_record_fn callback, void* user);

/* Restrict who may write channels matching `pattern` to the listed writer identities. Absent rules
 * leave channels open. */
mosaic_status mosaic_node_allow_writers(mosaic_node* node, const char* pattern,
                                        const char** writers, size_t count);

/* Append to our own log on `channel` and announce it. `out_seq` receives the assigned sequence
 * number. */
mosaic_status mosaic_node_publish(mosaic_node* node, const char* channel, const char* content_type,
                                  const uint8_t* payload, size_t payload_len, uint64_t now_ms,
                                  uint64_t* out_seq);

/* Handle everything waiting, then run timers. Call both from your loop with your own clock. */
void mosaic_node_pump(mosaic_node* node, uint64_t now_ms);
void mosaic_node_tick(mosaic_node* node, uint64_t now_ms);

/* ---------------------------------------------------------------------------------------------------------------
 * Copies and diagnostics
 * ------------------------------------------------------------------------------------------------------------
 */

/* Copy a record handed to a callback so you can keep it. Free it with mosaic_record_owned_free. */
mosaic_status mosaic_record_clone(const mosaic_record* record, mosaic_record_owned** out);

/* The copied record. Its strings and payload live as long as the owned record. */
mosaic_status mosaic_record_owned_get(const mosaic_record_owned* owned, mosaic_record* out);

void mosaic_record_owned_free(mosaic_record_owned** owned);

/* The last error message for this thread, or an empty string. Valid until the next call on this
 * thread. */
const char* mosaic_last_error(void);

/* Free a string the library returned (identity, endpoints). Tolerates NULL. */
void mosaic_free_string(char** text);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* MOSAIC_C_H */

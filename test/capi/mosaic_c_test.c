/* The C ABI, exercised from C: two nodes in one process, one publishes over a real link, the other
 * tails. This file is compiled by a C compiler, which is the point: the header must be usable
 * without C++. */

#include <mosaic/mosaic_c.h>

#include <stdio.h>
#include <string.h>

static int g_failures = 0;

static void expect(int condition, const char* what) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s (%s)\n", what, mosaic_last_error());
        ++g_failures;
    }
}

static int g_received = 0;
static char g_last_payload[64];
static mosaic_record_owned* g_kept =
    NULL; /* a copy taken from inside the callback, which outlives it */

static void on_record(void* user, const mosaic_record* record) {
    (void)user;
    ++g_received;
    size_t length = record->payload_len < sizeof(g_last_payload) - 1 ? record->payload_len
                                                                     : sizeof(g_last_payload) - 1;
    memcpy(g_last_payload, record->payload, length);
    g_last_payload[length] = '\0';
    if (g_kept == NULL)
        expect(mosaic_record_clone(record, &g_kept) == MOSAIC_OK,
               "clone a record out of the callback");
}

int main(void) {
    const char* keys_path = "capi-test.key";
    const char* store_a_path = "capi-test-a.db";
    const char* store_b_path = "capi-test-b.db";
    int step;
    char* identity = NULL;
    char* a_pub = NULL;
    char* a_router = NULL;
    char* b_pub = NULL;
    char* b_router = NULL;
    mosaic_keys* keys = NULL;
    mosaic_store* store_a = NULL;
    mosaic_store* store_b = NULL;
    mosaic_transport* transport_a = NULL;
    mosaic_transport* transport_b = NULL;
    mosaic_node* writer = NULL;
    mosaic_node* follower = NULL;
    mosaic_node_config writer_config;
    mosaic_node_config follower_config;
    mosaic_peer peer_a;
    mosaic_peer peer_b;
    const char* payload = "hello-from-c";
    uint64_t seq = 0;
    uint64_t now = 0;

    remove(keys_path);
    remove(store_a_path);
    remove(store_b_path);

    printf("mosaic %s\n", mosaic_version());

    /* A build without signing cannot do any of this. That is a property of the build, not a
     * failure. */
    {
        const mosaic_status generated = mosaic_keys_generate(keys_path, &identity);
        if (generated == MOSAIC_ERR_UNSUPPORTED) {
            printf("C ABI: ok (no signing support in this build; skipped)\n");
            return 0;
        }
        expect(generated == MOSAIC_OK, "generate keys");
    }
    expect(identity != NULL && strlen(identity) == 64, "identity is a hex public key");
    mosaic_free_string(&identity);

    expect(mosaic_keys_load(keys_path, &keys) == MOSAIC_OK, "load keys");
    expect(mosaic_store_open(store_a_path, 0, &store_a) == MOSAIC_OK, "open store a");
    expect(mosaic_store_open(store_b_path, 0, &store_b) == MOSAIC_OK, "open store b");

    /* Plaintext is something a caller asks for. Missing keys are an error, not a quiet downgrade:
     * this is the check that the secure path cannot be skipped by forgetting an argument. */
    {
        mosaic_transport* nowhere = NULL;
        expect(mosaic_transport_new("a", "tcp://127.0.0.1:*", "tcp://127.0.0.1:*", NULL, 0, NULL,
                                    NULL, &nowhere) == MOSAIC_ERR_INVALID_ARGUMENT,
               "transport without keys is refused");
        expect(nowhere == NULL, "no transport was created");
    }

    /* A core-only build has no transport. Say so and pass: the point of this suite is the ABI, and
     * it runs wherever the ABI can run. The two-node half needs a network. */
    if (mosaic_transport_new_plaintext("a", "tcp://127.0.0.1:*", "tcp://127.0.0.1:*", NULL, 0,
                                       &transport_a) == MOSAIC_ERR_UNSUPPORTED) {
        printf(g_failures == 0
                   ? "C ABI: ok (no network transport in this build; two-node half skipped)\n"
                   : "C ABI: %d failure(s)\n",
               g_failures);
        mosaic_store_free(&store_b);
        mosaic_store_free(&store_a);
        mosaic_keys_free(&keys);
        remove(keys_path);
        remove(store_a_path);
        remove(store_b_path);
        return g_failures == 0 ? 0 : 1;
    }
    expect(mosaic_transport_new_plaintext("b", "tcp://127.0.0.1:*", "tcp://127.0.0.1:*", NULL, 0,
                                          &transport_b) == MOSAIC_OK,
           "transport b");

    /* And the encrypted path the ABI is meant to be used with: the curve pair comes out of a
     * keystore, so a binding has no excuse to skip it. */
    {
        char* curve_public = NULL;
        char* curve_secret = NULL;
        const mosaic_status curve = mosaic_keys_curve(keys, &curve_public, &curve_secret);
        if (curve == MOSAIC_OK) {
            expect(curve_public != NULL && strlen(curve_public) == 40, "curve public is z85");
            expect(curve_secret != NULL && strlen(curve_secret) == 40, "curve secret is z85");
            mosaic_transport* encrypted = NULL;
            expect(mosaic_transport_new("encrypted", "tcp://127.0.0.1:*", "tcp://127.0.0.1:*", NULL,
                                        0, curve_public, curve_secret, &encrypted) == MOSAIC_OK,
                   "transport with keystore curve keys");
            mosaic_transport_free(&encrypted);
        } else {
            expect(curve == MOSAIC_ERR_UNSUPPORTED, "no crypto means no curve keys, not a crash");
        }
        mosaic_free_string(&curve_public);
        mosaic_free_string(&curve_secret);
    }
    expect(mosaic_transport_endpoints(transport_a, &a_pub, &a_router) == MOSAIC_OK, "endpoints a");
    expect(mosaic_transport_endpoints(transport_b, &b_pub, &b_router) == MOSAIC_OK, "endpoints b");

    peer_b.id = "b";
    peer_b.pub_endpoint = b_pub;
    peer_b.router_endpoint = b_router;
    peer_b.curve_public = NULL;
    peer_a.id = "a";
    peer_a.pub_endpoint = a_pub;
    peer_a.router_endpoint = a_router;
    peer_a.curve_public = NULL;
    expect(mosaic_transport_add_peer(transport_a, &peer_b) == MOSAIC_OK, "a dials b");
    expect(mosaic_transport_add_peer(transport_b, &peer_a) == MOSAIC_OK, "b dials a");

    memset(&writer_config, 0, sizeof(writer_config));
    writer_config.id = "a";
    writer_config.head_interval_ms = 200;
    memset(&follower_config, 0, sizeof(follower_config));
    follower_config.id = "b";
    follower_config.head_interval_ms = 200;

    expect(mosaic_node_new(&writer_config, transport_a, store_a, keys, &writer) == MOSAIC_OK,
           "writer node");
    expect(mosaic_node_new(&follower_config, transport_b, store_b, keys, &follower) == MOSAIC_OK,
           "follower node");
    expect(mosaic_node_on_record(follower, on_record, NULL) == MOSAIC_OK, "follower callback");
    expect(mosaic_node_follow(follower, "events.>") == MOSAIC_OK, "follower follows");
    expect(mosaic_node_follow(follower, "bad..pattern") == MOSAIC_ERR_INVALID_ARGUMENT,
           "invalid pattern refused");

    for (step = 0; step < 400 && g_received == 0; ++step) {
        now += 25;
        if (step == 4) {
            expect(mosaic_node_publish(writer, "events.a", "text/plain", (const uint8_t*)payload,
                                       strlen(payload), now, &seq) == MOSAIC_OK,
                   "publish");
            expect(seq == 0, "first record has sequence 0");
        }
        mosaic_node_pump(writer, now);
        mosaic_node_tick(writer, now);
        mosaic_node_pump(follower, now);
        mosaic_node_tick(follower, now);
    }
    expect(g_received == 1, "the follower received exactly one record");
    expect(strcmp(g_last_payload, payload) == 0, "payload arrived intact");

    /* The copy must still be intact now the callback returned. */
    if (g_kept != NULL) {
        mosaic_record kept;
        expect(mosaic_record_owned_get(g_kept, &kept) == MOSAIC_OK, "read the kept record");
        expect(kept.payload_len == strlen(payload) &&
                   memcmp(kept.payload, payload, kept.payload_len) == 0,
               "the kept copy has the payload");
        expect(strcmp(kept.channel, "events.a") == 0, "the kept copy has the channel");
        expect(kept.seq == 0, "the kept copy has the sequence");
        mosaic_record_owned_free(&g_kept);
        mosaic_record_owned_free(&g_kept);
        expect(g_kept == NULL, "double free of a kept record is a no-op");
    }

    mosaic_node_free(&writer);
    mosaic_node_free(&writer);
    expect(writer == NULL, "double free is a no-op");

    mosaic_free_string(&a_pub);
    mosaic_free_string(&a_router);
    mosaic_free_string(&b_pub);
    mosaic_free_string(&b_router);
    mosaic_transport_free(&transport_a);
    mosaic_transport_free(&transport_b);
    mosaic_store_free(&store_a);
    mosaic_store_free(&store_b);
    mosaic_keys_free(&keys);

    remove(keys_path);
    remove(store_a_path);
    remove(store_b_path);
    remove("capi-test-a.db-wal");
    remove("capi-test-b.db-wal");
    remove("capi-test-a.db-shm");
    remove("capi-test-b.db-shm");

    printf(g_failures == 0 ? "C ABI: ok\n" : "C ABI: %d failure(s)\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}

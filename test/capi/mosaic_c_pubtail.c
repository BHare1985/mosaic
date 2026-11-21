/* Two processes, one ABI: `mosaic_c_pubtail tail ...` prints records, `mosaic_c_pubtail pub ...`
 * sends them. The shape the CLI demo has, reached through nothing but mosaic_c.h. */

#include <mosaic/mosaic_c.h>

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static volatile sig_atomic_t g_stop = 0;
static void on_signal(int signal_number) {
    (void)signal_number;
    g_stop = 1;
}

static void on_record(void* user, const mosaic_record* record) {
    (void)user;
    printf("record seq=%llu channel=%s writer-prefix=%.8s payload=%.*s\n",
           (unsigned long long)record->seq, record->channel, record->writer,
           (int)record->payload_len, (const char*)record->payload);
    fflush(stdout);
}

static void sleep_ms(int ms) {
    /* No threads and no timers: the loop just needs to yield. */
    struct timespec request;
    request.tv_sec = ms / 1000;
    request.tv_nsec = (long)(ms % 1000) * 1000000L;
    nanosleep(&request, NULL);
}

static int make_endpoint(char* buffer, size_t size, int port) {
    return snprintf(buffer, size, "tcp://127.0.0.1:%d", port) < (int)size;
}

int main(int argc, char** argv) {
    char self_pub[64], self_router[64], peer_pub[64], peer_router[64];
    char keys_path[64], store_path[64];
    const char* mode;
    const char* channel = "events.demo";
    const char* payload = "hello from another process";
    int self_port, self_router_port, peer_port, peer_router_port;
    int count = 5;
    char* identity = NULL;
    mosaic_keys* keys = NULL;
    mosaic_store* store = NULL;
    mosaic_transport* transport = NULL;
    mosaic_node* node = NULL;
    mosaic_node_config config;
    mosaic_peer peer;
    uint64_t now = 0;

    if (argc < 6) {
        fprintf(stderr,
                "usage: %s pub|tail self_pub_port self_router_port peer_pub_port peer_router_port "
                "[payload] [count]\n",
                argv[0]);
        return 2;
    }
    mode = argv[1];
    self_port = atoi(argv[2]);
    self_router_port = atoi(argv[3]);
    peer_port = atoi(argv[4]);
    peer_router_port = atoi(argv[5]);
    if (argc > 6)
        payload = argv[6];
    if (argc > 7)
        count = atoi(argv[7]);

    if (!make_endpoint(self_pub, sizeof(self_pub), self_port) ||
        !make_endpoint(self_router, sizeof(self_router), self_router_port) ||
        !make_endpoint(peer_pub, sizeof(peer_pub), peer_port) ||
        !make_endpoint(peer_router, sizeof(peer_router), peer_router_port)) {
        fprintf(stderr, "endpoint formatting failed\n");
        return 2;
    }

    snprintf(keys_path, sizeof(keys_path), "capi-pubtail-%s.key", mode);
    snprintf(store_path, sizeof(store_path), "capi-pubtail-%s.db", mode);
    if (mosaic_keys_generate(keys_path, &identity) != MOSAIC_OK) {
        fprintf(stderr, "keys: %s\n", mosaic_last_error());
        return 1;
    }
    mosaic_free_string(&identity);
    if (mosaic_keys_load(keys_path, &keys) != MOSAIC_OK ||
        mosaic_store_open(store_path, 1, &store) != MOSAIC_OK ||
        /* Plaintext on purpose and by name: this pair exists to prove durability and the ABI across
         * two processes, and it has no argument with which to exchange CURVE keys. The encrypted
         * ABI path is exercised in mosaic_c_test.c, where both keystores exist. */
        mosaic_transport_new_plaintext(mode, self_pub, self_router, NULL, 0, &transport) !=
            MOSAIC_OK) {
        fprintf(stderr, "setup: %s\n", mosaic_last_error());
        return 1;
    }

    peer.id = strcmp(mode, "pub") == 0 ? "tail" : "pub";
    peer.pub_endpoint = peer_pub;
    peer.router_endpoint = peer_router;
    peer.curve_public = NULL;
    if (mosaic_transport_add_peer(transport, &peer) != MOSAIC_OK) {
        fprintf(stderr, "peer: %s\n", mosaic_last_error());
        return 1;
    }

    memset(&config, 0, sizeof(config));
    config.id = mode;
    config.head_interval_ms = 200;
    if (mosaic_node_new(&config, transport, store, keys, &node) != MOSAIC_OK ||
        mosaic_node_on_record(node, on_record, NULL) != MOSAIC_OK ||
        mosaic_node_follow(node, "events.>") != MOSAIC_OK) {
        fprintf(stderr, "node: %s\n", mosaic_last_error());
        return 1;
    }

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    if (strcmp(mode, "pub") == 0) {
        int sent = 0;
        /* Give the tail a moment to subscribe: the ABI has no slow-joiner detection yet. */
        for (int i = 0; i < 60; ++i) {
            mosaic_node_pump(node, now += 50);
            mosaic_node_tick(node, now);
            sleep_ms(10);
        }
        while (sent < count && !g_stop) {
            uint64_t seq = 0;
            char line[128];
            snprintf(line, sizeof(line), "%s #%d", payload, sent);
            if (mosaic_node_publish(node, channel, "text/plain", (const uint8_t*)line, strlen(line),
                                    now, &seq) != MOSAIC_OK) {
                fprintf(stderr, "publish: %s\n", mosaic_last_error());
                return 1;
            }
            ++sent;
            for (int i = 0; i < 10; ++i) {
                mosaic_node_pump(node, now += 40);
                mosaic_node_tick(node, now);
                sleep_ms(10);
            }
        }
        printf("published %d record(s)\n", sent);
    } else {
        /* Tail until told to stop. */
        while (!g_stop) {
            mosaic_node_pump(node, now += 50);
            mosaic_node_tick(node, now);
            sleep_ms(20);
        }
    }

    mosaic_node_free(&node);
    mosaic_transport_free(&transport);
    mosaic_store_free(&store);
    mosaic_keys_free(&keys);
    return 0;
}

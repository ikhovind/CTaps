#include <picoquic.h>
#include <picoquic_utils.h>
#include <picosocks.h>
#include <picoquic_packet_loop.h>
#include <picoquic_set_textlog.h>
#include <picoquic_bbr.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <unistd.h>

#define ALPN         "simple-ping"
#define CERT_PATH    "/home/ikhovind/Documents/Skole/taps/test/quic/cert.pem"
#define KEY_PATH     "/home/ikhovind/Documents/Skole/taps/test/quic/key.pem"
#define DEFAULT_PORT 4433

typedef struct {
    uint64_t stream_id;
    int      is_request_complete;
} stream_context_t;

typedef struct {
    int dummy;
} server_context_t;

static stream_context_t *create_stream_context(uint64_t stream_id) {
    stream_context_t *ctx = malloc(sizeof(stream_context_t));
    if (ctx) {
        memset(ctx, 0, sizeof(stream_context_t));
        ctx->stream_id = stream_id;
    }
    return ctx;
}

static void delete_stream_context(stream_context_t *ctx) {
    free(ctx);
}

static int server_callback(picoquic_cnx_t *cnx, uint64_t stream_id,
                           uint8_t *bytes, size_t length,
                           picoquic_call_back_event_t fin_or_event,
                           void *callback_ctx, void *stream_ctx) {
    server_context_t *server_ctx = (server_context_t *)callback_ctx;
    stream_context_t *s_ctx = (stream_context_t *)stream_ctx;
    int ret = 0;
    int send_fin = 1;

    if (!callback_ctx || callback_ctx == picoquic_get_default_callback_context(picoquic_get_quic_ctx(cnx))) {
        server_ctx = malloc(sizeof(server_context_t));
        if (!server_ctx) {
            picoquic_close(cnx, PICOQUIC_ERROR_MEMORY);
            return -1;
        }
        memset(server_ctx, 0, sizeof(server_context_t));
        picoquic_set_callback(cnx, server_callback, server_ctx);
    }

    switch (fin_or_event) {
    case picoquic_callback_stream_data:
        send_fin = 0;

    case picoquic_callback_stream_fin:
        if (!s_ctx) {
            s_ctx = create_stream_context(stream_id);
            if (!s_ctx || picoquic_set_app_stream_ctx(cnx, stream_id, s_ctx) != 0) {
                fprintf(stderr, "[SERVER] Failed to create stream context\n");
                picoquic_reset_stream(cnx, stream_id, 0x101);
                return -1;
            }
        }

        if (length > 0) {
            const char   prefix[] = "Pong: ";
            const size_t pfx_len  = sizeof(prefix) - 1;
            uint8_t     *resp     = malloc(pfx_len + length);
            if (!resp) {
                picoquic_reset_stream(cnx, stream_id, PICOQUIC_ERROR_MEMORY);
                return -1;
            }
            memcpy(resp, prefix, pfx_len);
            memcpy(resp + pfx_len, bytes, length);

            char printbuf[256];
            size_t plen = length < sizeof(printbuf) - 1 ? length : sizeof(printbuf) - 1;
            memcpy(printbuf, bytes, plen);
            printbuf[plen] = '\0';

            /* Log local and peer addresses at send time */
            struct sockaddr* peer_addr  = NULL;
            struct sockaddr* local_addr = NULL;
            int peer_len = 0, local_len = 0;
            picoquic_get_peer_addr(cnx, &peer_addr);
            picoquic_get_local_addr(cnx, &local_addr);

            char peer_ip[INET6_ADDRSTRLEN]  = "?";
            char local_ip[INET6_ADDRSTRLEN] = "?";
            uint16_t peer_port = 0, local_port = 0;

            if (peer_addr && peer_addr->sa_family == AF_INET) {
                struct sockaddr_in* s = (struct sockaddr_in*)peer_addr;
                inet_ntop(AF_INET, &s->sin_addr, peer_ip, sizeof(peer_ip));
                peer_port = ntohs(s->sin_port);
            } else if (peer_addr && peer_addr->sa_family == AF_INET6) {
                struct sockaddr_in6* s = (struct sockaddr_in6*)peer_addr;
                inet_ntop(AF_INET6, &s->sin6_addr, peer_ip, sizeof(peer_ip));
                peer_port = ntohs(s->sin6_port);
            }
            if (local_addr && local_addr->sa_family == AF_INET) {
                struct sockaddr_in* s = (struct sockaddr_in*)local_addr;
                inet_ntop(AF_INET, &s->sin_addr, local_ip, sizeof(local_ip));
                local_port = ntohs(s->sin_port);
            } else if (local_addr && local_addr->sa_family == AF_INET6) {
                struct sockaddr_in6* s = (struct sockaddr_in6*)local_addr;
                inet_ntop(AF_INET6, &s->sin6_addr, local_ip, sizeof(local_ip));
                local_port = ntohs(s->sin6_port);
            }

            printf("[stream %llu] ping='%s' -> pong from %s:%u to %s:%u\n",
                   (unsigned long long)stream_id, printbuf,
                   local_ip, local_port, peer_ip, peer_port);

            /* Do NOT set fin=1 — client reuses the stream for the second ping */
            ret = picoquic_add_to_stream(cnx, stream_id, resp, pfx_len + length, send_fin);
            free(resp);
            if (ret != 0)
                fprintf(stderr, "[SERVER] picoquic_add_to_stream error: %d\n", ret);
        }
        else {
            ret = picoquic_add_to_stream(cnx, stream_id, NULL, 0, send_fin);
        }
        break;

    case picoquic_callback_stream_reset:
    case picoquic_callback_stop_sending:
        if (s_ctx) {
            picoquic_reset_stream(cnx, stream_id, 0);
            delete_stream_context(s_ctx);
            picoquic_set_app_stream_ctx(cnx, stream_id, NULL);
        }
        break;

    case picoquic_callback_close:
    case picoquic_callback_application_close:
        if (server_ctx && server_ctx != picoquic_get_default_callback_context(picoquic_get_quic_ctx(cnx))) {
            free(server_ctx);
        }
        picoquic_set_callback(cnx, server_callback, NULL);
        break;

    default:
        fprintf(stderr, "[SERVER] Unhandled event: %d\n", fin_or_event);
        break;
    }
    return ret;
}

int main(int argc, char *argv[]) {
    int port = DEFAULT_PORT;
    if (argc > 1) port = atoi(argv[1]);

    printf("QUIC ping server on port %d (ALPN: %s)\n", port, ALPN);

    server_context_t *default_ctx = malloc(sizeof(server_context_t));
    if (!default_ctx) { fprintf(stderr, "OOM\n"); return -1; }
    memset(default_ctx, 0, sizeof(server_context_t));

    picoquic_quic_t *quic = picoquic_create(
        8, CERT_PATH, KEY_PATH, NULL, ALPN,
        server_callback, default_ctx,
        NULL, NULL, NULL,
        picoquic_current_time(), NULL, NULL, NULL, 0);

    if (!quic) {
        fprintf(stderr, "Failed to create QUIC context\n");
        free(default_ctx);
        return -1;
    }

    int ret = picoquic_packet_loop(quic, port, 0, 0, 0, 0, NULL, NULL);

    if (ret != 0) {
        fprintf(stderr, "Packet loop error: %d\n", ret);
    }

    picoquic_free(quic);
    free(default_ctx);
    return ret;
}

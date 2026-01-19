#include "../common/protocol.h"
#include "../common/file_generator.h"
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

#define ALPN "benchmark"
#define CERT_PATH RESOURCE_FOLDER "/cert.pem"
#define KEY_PATH RESOURCE_FOLDER "/key.pem"

typedef struct {
    uint64_t stream_id;
    size_t request_len;
    uint8_t *file_data;
    size_t file_size;
    size_t bytes_sent;
    int is_request_complete;
    int is_sending;
} stream_context_t;

typedef struct {
    stream_context_t *first_stream;
    uint8_t *large_file_data;
    size_t large_file_size;
    uint8_t *short_file_data;
    size_t short_file_size;
} server_context_t;

static uint8_t *large_file_data_global = NULL;
static size_t large_file_size_global = 0;
static uint8_t *short_file_data_global = NULL;
static size_t short_file_size_global = 0;

static void load_files() {
    FILE *fp;

    if (access("large_file.dat", F_OK) != 0) {
        generate_test_file("large_file.dat", LARGE_FILE_SIZE);
    }
    if (access("short_file.dat", F_OK) != 0) {
        generate_test_file("short_file.dat", SHORT_FILE_SIZE);
    }

    fp = fopen("large_file.dat", "rb");
    if (fp) {
        fseek(fp, 0, SEEK_END);
        large_file_size_global = ftell(fp);
        fseek(fp, 0, SEEK_SET);
        large_file_data_global = malloc(large_file_size_global);
        fread(large_file_data_global, 1, large_file_size_global, fp);
        fclose(fp);
        printf("Loaded large file: %zu bytes\n", large_file_size_global);
    }

    fp = fopen("short_file.dat", "rb");
    if (fp) {
        fseek(fp, 0, SEEK_END);
        short_file_size_global = ftell(fp);
        fseek(fp, 0, SEEK_SET);
        short_file_data_global = malloc(short_file_size_global);
        fread(short_file_data_global, 1, short_file_size_global, fp);
        fclose(fp);
        printf("Loaded short file: %zu bytes\n", short_file_size_global);
    }
}

static stream_context_t *create_stream_context(uint64_t stream_id) {
    stream_context_t *ctx = malloc(sizeof(stream_context_t));
    if (ctx) {
        memset(ctx, 0, sizeof(stream_context_t));
        ctx->stream_id = stream_id;
    }
    return ctx;
}

static void delete_stream_context(stream_context_t *ctx) {
    if (ctx) {
        free(ctx);
    }
}

static int server_callback(picoquic_cnx_t *cnx, uint64_t stream_id,
                          uint8_t *bytes, size_t length,
                          picoquic_call_back_event_t fin_or_event,
                          void *callback_ctx, void *stream_ctx) {
    server_context_t *server_ctx = (server_context_t *)callback_ctx;
    stream_context_t *s_ctx = (stream_context_t *)stream_ctx;
    int ret = 0;

    if (callback_ctx == NULL || callback_ctx == picoquic_get_default_callback_context(picoquic_get_quic_ctx(cnx))) {
        server_ctx = malloc(sizeof(server_context_t));
        if (!server_ctx) {
            picoquic_close(cnx, PICOQUIC_ERROR_MEMORY);
            return -1;
        }
        memset(server_ctx, 0, sizeof(server_context_t));
        server_ctx->large_file_data = large_file_data_global;
        server_ctx->large_file_size = large_file_size_global;
        server_ctx->short_file_data = short_file_data_global;
        server_ctx->short_file_size = short_file_size_global;
        picoquic_set_callback(cnx, server_callback, server_ctx);
    }

    switch (fin_or_event) {
    case picoquic_callback_stream_data:
    case picoquic_callback_stream_fin:
        if (!s_ctx) {
            printf("[SERVER CB] Received new stream, creating context\n");
            s_ctx = create_stream_context(stream_id);
            if (!s_ctx || picoquic_set_app_stream_ctx(cnx, stream_id, s_ctx) != 0) {
                fprintf(stderr, "[SERVER CB] Failed to create stream context!\n");
                picoquic_reset_stream(cnx, stream_id, 0x101);
                return -1;
            }
            printf("[SERVER CB] Stream context created successfully\n");
        }

        printf("[SERVER CB] Checking request: complete=%d, length=%zu, bytes=%p\n", s_ctx->is_request_complete, length, (void*)bytes);
        if (!s_ctx->is_request_complete && length > 0) {
            printf("[SERVER CB] Entering data copy block\n");
            size_t to_copy = length;

            if (fin_or_event == picoquic_callback_stream_fin) {
                printf( "[SERVER CB] Request is complete, parsing...\n");
                s_ctx->is_request_complete = 1;

                if (strncmp((char *)bytes, REQUEST_LARGE, strlen(REQUEST_LARGE)) == 0) {
                    printf("[Stream %llu] Request: LARGE, num bytes: %zu\n", (unsigned long long)stream_id, server_ctx->large_file_size);
                    picoquic_add_to_stream(cnx, stream_id, server_ctx->large_file_data, server_ctx->large_file_size, 1);
                } else if (strncmp((char *)bytes, REQUEST_SHORT, strlen(REQUEST_SHORT)) == 0) {
                    printf("[Stream %llu] Request: SHORT, num bytes: %zu\n", (unsigned long long)stream_id, server_ctx->short_file_size);
                    picoquic_add_to_stream(cnx, stream_id, server_ctx->short_file_data, server_ctx->short_file_size, 1);
                } else {
                    fprintf(stderr, "[SERVER CB] Unknown request on stream %llu\n",
                            (unsigned long long)stream_id);
                }
            }
        }

        if (s_ctx->is_sending) {
            picoquic_mark_active_stream(cnx, stream_id, 1, s_ctx);
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
        fprintf(stderr, "[SERVER CB] Unhandled event: %d\n", fin_or_event);
        break;
    }
    return ret;
}

int main(int argc, char *argv[]) {
    int port = DEFAULT_PORT;
    int ret = 0;
    picoquic_quic_t *quic = NULL;

    if (argc > 1) {
        port = atoi(argv[1]);
    }

    printf("QUIC Server starting on port %d\n", port);
    printf("ALPN: %s\n", ALPN);

    load_files();

    if (!large_file_data_global || !short_file_data_global) {
        fprintf(stderr, "Failed to load files\n");
        return -1;
    }

    server_context_t *default_ctx = malloc(sizeof(server_context_t));
    if (!default_ctx) {
        fprintf(stderr, "Failed to allocate default context\n");
        return -1;
    }
    memset(default_ctx, 0, sizeof(server_context_t));
    default_ctx->large_file_data = large_file_data_global;
    default_ctx->large_file_size = large_file_size_global;
    default_ctx->short_file_data = short_file_data_global;
    default_ctx->short_file_size = short_file_size_global;

    if (access(CERT_PATH, F_OK) == 0) {
        printf("No problems accessing certificate file: %s\n", CERT_PATH);
    }
    else {
        printf("Cannot access certificate file: %s\n", CERT_PATH);
    }


    quic = picoquic_create(8, CERT_PATH, KEY_PATH, NULL, ALPN,
                          server_callback, default_ctx, NULL, NULL, NULL,
                          picoquic_current_time(), NULL, NULL, NULL, 0);
    picoquic_set_textlog(quic, "server_debug.log");

    if (!quic) {
        fprintf(stderr, "Failed to create QUIC context\n");
        return -1;
    }

    /* Set MTU to 1500 bytes for fair comparison with TCP MSS=1460 */
    picoquic_set_mtu_max(quic, 1500);
    printf("Set QUIC maximum MTU to 1500 bytes\n");

    picoquic_set_default_congestion_algorithm(quic, picoquic_bbr_algorithm);

    printf("Server listening on port %d\n", port);

    ret = picoquic_packet_loop(quic, port, 0, 0, 0, 0, NULL, NULL);

    if (quic) {
        picoquic_free(quic);
    }

    if (default_ctx) {
        free(default_ctx);
    }

    if (large_file_data_global) {
        free(large_file_data_global);
    }
    if (short_file_data_global) {
        free(short_file_data_global);
    }
    printf("Server exiting with code %d\n", ret);

    return ret;
}

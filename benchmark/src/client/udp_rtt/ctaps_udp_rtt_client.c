#include <arpa/inet.h>
#include <ctaps.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <stdint.h>
#include <stdlib.h>

#define MSG_SIZE   64
#define N_ITER      100000

static struct timespec g_t_start;
static uint64_t g_rtts[N_ITER];
static int g_iter = 0;
static FILE *g_out = NULL;

static uint64_t timespec_to_ns(const struct timespec *ts) {
    return (uint64_t)ts->tv_sec * 1000000000ULL + (uint64_t)ts->tv_nsec;
}

static void send_next_ping(ct_connection_t *connection);

void respond_and_continue_on_message_received(ct_connection_t *connection,
                                              ct_message_t *received_message,
                                              ct_message_context_t *message_context) {
    (void)received_message;
    (void)message_context;

    struct timespec t_end;
    if (clock_gettime(CLOCK_MONOTONIC_RAW, &t_end) != 0) {
        perror("clock_gettime end");
        ct_connection_close(connection);
        return;
    }

    if (g_iter < N_ITER) {
        uint64_t dt_ns = timespec_to_ns(&t_end) - timespec_to_ns(&g_t_start);
        g_rtts[g_iter] = dt_ns;
        g_iter++;
    }

    if (g_iter >= N_ITER) {
        if (g_out) {
            for (int i = 0; i < N_ITER; ++i) {
                fprintf(g_out, "%llu\n",
                        (unsigned long long)g_rtts[i]);
            }
            fclose(g_out);
            g_out = NULL;
            printf("Wrote %d RTT samples to %s\n", N_ITER, (char*)ct_connection_get_callback_context(connection));
        }

        ct_connection_close(connection);
        return;
    }

    send_next_ping(connection);
}

static void send_next_ping(ct_connection_t *connection) {
    char payload[MSG_SIZE];
    memset(payload, 'A', sizeof(payload));

    ct_message_t *message = ct_message_new_with_content(payload, sizeof(payload));

    // Timestamp *before* send
    if (clock_gettime(CLOCK_MONOTONIC_RAW, &g_t_start) != 0) {
        perror("clock_gettime start");
        ct_message_free(message);
        ct_connection_close(connection);
        return;
    }

    ct_send_message(connection, message);
    ct_message_free(message);

    ct_receive_callbacks_t receive_callbacks = {
        .receive_callback = respond_and_continue_on_message_received,
    };

    ct_receive_message(connection, &receive_callbacks);
}

void ping_on_ready(ct_connection_t *connection) {
    printf("Connection ready, starting RTT test: %d iterations, msg size %d bytes\n",
           N_ITER, MSG_SIZE);

    g_out = fopen(ct_connection_get_callback_context(connection), "w");
    if (!g_out) {

        printf("failed starting first ping");
        perror("fopen");
        ct_connection_close(connection);
        return;
    }

    g_iter = 0;
    printf("starting first ping");
    send_next_ping(connection);
}

void free_on_connection_closed(ct_connection_t *connection) {
    printf("Connection closed, cleaning up\n");
    ct_connection_free(connection);

    if (g_out) {
        fclose(g_out);
        g_out = NULL;
    }
}

int main(int argc, char** argv) {
    if (argc >= 2) {
        printf("Usage: %s [out_directory]\n", argv[0]);
    }
    const char* filename = "rtt_ctaps_ns.txt";
    char out_file[512];

    if (argc == 2) {
        snprintf(out_file, sizeof(out_file), "%s/%s", argv[1], filename);
    } else {
        snprintf(out_file, sizeof(out_file), "%s", filename);
    }
    ct_initialize();
    ct_set_log_level(CT_LOG_ERROR);

    ct_remote_endpoint_t *remote_endpoint = ct_remote_endpoint_new();
    ct_remote_endpoint_with_ipv4(remote_endpoint, inet_addr("127.0.0.1"));
    ct_remote_endpoint_with_port(remote_endpoint, 5000);
    const ct_remote_endpoint_t* remotes[] = {remote_endpoint};

    ct_transport_properties_t *tp = ct_transport_properties_new();
    ct_transport_properties_set_reliability(tp, PROHIBIT);
    ct_transport_properties_set_preserve_order(tp, PROHIBIT);
    ct_transport_properties_set_congestion_control(tp, PROHIBIT);

    ct_preconnection_t *preconnection = ct_preconnection_new(
        NULL, 0, remotes, 1, tp, NULL);

    ct_connection_callbacks_t connection_callbacks = {
        .ready = ping_on_ready,
        .closed = free_on_connection_closed,
        .per_connection_context = out_file
    };

    ct_preconnection_initiate(preconnection, &connection_callbacks);

    ct_start_event_loop();

    ct_preconnection_free(preconnection);
    ct_transport_properties_free(tp);
    ct_remote_endpoint_free(remote_endpoint);

    ct_close();
    return 0;
}

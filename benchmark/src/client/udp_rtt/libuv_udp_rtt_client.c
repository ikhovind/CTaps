#define _GNU_SOURCE
#include <uv.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <stdint.h>
#include <stdlib.h>

#define PORT        5000
#define MSG_SIZE    64
#define N_WARMUP    1000
#define N_ITER      100000

static uv_loop_t *loop;
static uv_udp_t udp_handle;
static struct sockaddr_in server_addr;

static struct timespec g_t_start;
static uint64_t g_rtts[N_ITER];
static int g_iter = 0;
static int g_warmup = 0;
static FILE *g_out = NULL;

static char sendbuf[MSG_SIZE];
static char g_out_file[512];

static uint64_t timespec_to_ns(const struct timespec *ts) {
    return (uint64_t)ts->tv_sec * 1000000000ULL + (uint64_t)ts->tv_nsec;
}

static void send_next_ping(void);

static void alloc_buf(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf) {
    (void)handle;
    buf->base = malloc(suggested_size);
    buf->len  = suggested_size;
}

static void on_recv(uv_udp_t *handle, ssize_t nread, const uv_buf_t *buf,
                    const struct sockaddr *addr, unsigned flags) {
    (void)addr;
    (void)flags;
    free(buf->base);

    if (nread <= 0) return;

    if (g_warmup < N_WARMUP) {
        g_warmup++;
        if (g_warmup == N_WARMUP) {
            printf("Starting RTT measurements: %d iterations, msg size %d bytes\n",
                   N_ITER, MSG_SIZE);
        }
        send_next_ping();
        return;
    }

    struct timespec t_end;
    if (clock_gettime(CLOCK_MONOTONIC_RAW, &t_end) != 0) {
        perror("clock_gettime end");
        uv_udp_recv_stop(handle);
        uv_close((uv_handle_t *)handle, NULL);
        return;
    }

    g_rtts[g_iter++] = timespec_to_ns(&t_end) - timespec_to_ns(&g_t_start);

    if (g_iter >= N_ITER) {
        uv_udp_recv_stop(handle);
        for (int i = 0; i < N_ITER; i++)
            fprintf(g_out, "%llu\n", (unsigned long long)g_rtts[i]);
        fclose(g_out);
        g_out = NULL;
        printf("Wrote %d RTT samples to %s\n", N_ITER, g_out_file);
        uv_close((uv_handle_t *)handle, NULL);
        return;
    }

    send_next_ping();
}

static void on_send(uv_udp_send_t *req, int status) {
    free(req);
    if (status < 0)
        fprintf(stderr, "send error: %s\n", uv_strerror(status));
}

static void send_next_ping(void) {
    if (g_warmup >= N_WARMUP) {
        if (clock_gettime(CLOCK_MONOTONIC_RAW, &g_t_start) != 0) {
            perror("clock_gettime start");
            return;
        }
    }

    uv_udp_send_t *req = malloc(sizeof(uv_udp_send_t));
    uv_buf_t buf = uv_buf_init(sendbuf, MSG_SIZE);
    uv_udp_send(req, &udp_handle, &buf, 1,
                (const struct sockaddr *)&server_addr, on_send);
}

int main(int argc, char** argv) {
    if (argc >= 2) {
        printf("Usage: %s [out_directory]\n", argv[0]);
    }
    const char *filename = "rtt_libuv_ns.txt";
    if (argc == 2) {
        snprintf(g_out_file, sizeof(g_out_file), "%s/%s", argv[1], filename);
    } else {
        snprintf(g_out_file, sizeof(g_out_file), "%s", filename);
    }

    loop = uv_default_loop();

    uv_udp_init(loop, &udp_handle);

    struct sockaddr_in local;
    uv_ip4_addr("127.0.0.1", 0, &local);
    uv_udp_bind(&udp_handle, (const struct sockaddr *)&local, 0);

    uv_ip4_addr("127.0.0.1", PORT, &server_addr);

    g_out = fopen(g_out_file, "w");
    if (!g_out) {
        perror("fopen");
        return 1;
    }

    memset(sendbuf, 'A', sizeof(sendbuf));

    uv_udp_recv_start(&udp_handle, alloc_buf, on_recv);

    printf("Warming up: %d iterations\n", N_WARMUP);
    send_next_ping();

    uv_run(loop, UV_RUN_DEFAULT);
    uv_loop_close(loop);
    return 0;
}

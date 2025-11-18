#ifndef CTAPS_TEST_UTIL_C
#define CTAPS_TEST_UTIL_C
#include "ctaps.h"

typedef struct {
    pthread_mutex_t* waiting_mutex;
    pthread_cond_t* waiting_cond;
    int* num_reads;
    int expected_num_reads;
} ct_call_back_waiter_t;

typedef struct {
    ct_message_t** message;
    ct_call_back_waiter_t* cb_waiter;
} ct_message_receiver_t;

void wait_for_callback(ct_call_back_waiter_t* cb_waiter) {
    pthread_mutex_lock(cb_waiter->waiting_mutex);
    while (*cb_waiter->num_reads < cb_waiter->expected_num_reads) {
        pthread_cond_wait(cb_waiter->waiting_cond, cb_waiter->waiting_mutex);
    }
    pthread_mutex_unlock(cb_waiter->waiting_mutex);
}

void increment_reads(ct_connection_t* connection, ct_call_back_waiter_t* cb_waiter) {
    pthread_mutex_lock(cb_waiter->waiting_mutex);
    (*cb_waiter->num_reads)++;
    if (*cb_waiter->num_reads >= cb_waiter->expected_num_reads) {
        pthread_cond_signal(cb_waiter->waiting_cond);
    }
    pthread_mutex_unlock(cb_waiter->waiting_mutex);
}

int receive_message_cb(ct_connection_t* connection, ct_message_t** received_message, void* user_data) {
    ct_message_receiver_t* message_receiver = (ct_message_receiver_t*)user_data;
    ct_message_t** message = message_receiver->message;

    // want to extract actual pointer so we can later free it
    *message = *received_message;

    increment_reads(connection, message_receiver->cb_waiter);

    if (*message_receiver->cb_waiter->num_reads >= message_receiver->cb_waiter->expected_num_reads) {
        ct_connection_close(connection);
    }

    return 0;
}

int connection_ready_cb(ct_connection_t* connection, void* user_data) {
    printf("ct_connection_t ready callback\n");

    ct_call_back_waiter_t* cb_waiter = (ct_call_back_waiter_t*) user_data;

    increment_reads(connection, cb_waiter);
    return 0;
}

#endif // CTAPS_TEST_UTIL_C
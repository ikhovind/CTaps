#ifndef CTAPS_TEST_UTIL_C
#define CTAPS_TEST_UTIL_C
#include "connections/preconnection/preconnection.h"
#include "connections/connection/connection.h"
#include "ctaps.h"
#include "endpoints/remote/remote_endpoint.h"
#include "transport_properties/transport_properties.h"

typedef struct {
    pthread_mutex_t* waiting_mutex;
    pthread_cond_t* waiting_cond;
    int* num_reads;
    int expected_num_reads;
} CallBackWaiter;

typedef struct {
    Message** message;
    CallBackWaiter* cb_waiter;
} MessageReceiver;

void wait_for_callback(CallBackWaiter* cb_waiter) {
    pthread_mutex_lock(cb_waiter->waiting_mutex);
    while (*cb_waiter->num_reads < cb_waiter->expected_num_reads) {
        pthread_cond_wait(cb_waiter->waiting_cond, cb_waiter->waiting_mutex);
    }
    pthread_mutex_unlock(cb_waiter->waiting_mutex);
}

void increment_reads(Connection* connection, CallBackWaiter* cb_waiter) {
    pthread_mutex_lock(cb_waiter->waiting_mutex);
    (*cb_waiter->num_reads)++;
    if (*cb_waiter->num_reads >= cb_waiter->expected_num_reads) {
        pthread_cond_signal(cb_waiter->waiting_cond);
    }
    pthread_mutex_unlock(cb_waiter->waiting_mutex);
}

int receive_message_cb(Connection* connection, Message** received_message, void* user_data) {
    MessageReceiver* message_receiver = (MessageReceiver*)user_data;
    Message** message = message_receiver->message;

    // want to extract actual pointer so we can later free it
    *message = *received_message;

    increment_reads(connection, message_receiver->cb_waiter);

    if (*message_receiver->cb_waiter->num_reads >= message_receiver->cb_waiter->expected_num_reads) {
        connection_close(connection);
    }

    return 0;
}

int connection_ready_cb(Connection* connection, void* user_data) {
    printf("Connection ready callback\n");

    CallBackWaiter* cb_waiter = (CallBackWaiter*) user_data;

    increment_reads(connection, cb_waiter);
    return 0;
}

#endif // CTAPS_TEST_UTIL_C
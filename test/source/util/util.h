//
// Created by ikhovind on 25.08.25.
//

#ifndef UTIL_H
#define UTIL_H

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

void wait_for_callback(ct_call_back_waiter_t* cb_waiter);

void increment_reads(ct_connection_t* connection, ct_call_back_waiter_t* cb_waiter);
int receive_message_cb(ct_connection_t* connection, ct_message_t** received_message, void* user_data);
int connection_ready_cb(ct_connection_t* connection, void* user_data);

#endif //UTIL_H
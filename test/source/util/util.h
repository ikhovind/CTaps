//
// Created by ikhovind on 25.08.25.
//

#ifndef UTIL_H
#define UTIL_H

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

void wait_for_callback(CallBackWaiter* cb_waiter);

void increment_reads(Connection* connection, CallBackWaiter* cb_waiter);
int receive_message_cb(Connection* connection, Message** received_message, void* user_data);
int connection_ready_cb(Connection* connection, void* user_data);

#endif //UTIL_H
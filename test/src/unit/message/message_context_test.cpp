#include <gmock/gmock-matchers.h>

#include "gtest/gtest.h"
#include "fff.h"
extern "C" {
#include "ctaps.h"
#include "ctaps_internal.h"
#include "fff.h"
}

#include "fixtures/awaiting_fixture.cpp"

TEST(MessageContextUnitTests, GetsLocalEndpoint) {
    ct_message_context_t* msg_ctx = ct_message_context_new();

    ct_local_endpoint_t* local_ep = ct_local_endpoint_new();
    local_ep->port = 1234;
    local_ep->data.resolved_address.ss_family = AF_INET;

    msg_ctx->local_endpoint = local_ep;

    const ct_local_endpoint_t* retrieved_local_ep = ct_message_context_get_local_endpoint(msg_ctx);

    ASSERT_NE(retrieved_local_ep, nullptr);
    ASSERT_EQ(retrieved_local_ep->port, 1234);
    ASSERT_EQ(retrieved_local_ep->data.resolved_address.ss_family, AF_INET);
    ASSERT_EQ(msg_ctx->local_endpoint, retrieved_local_ep);

    ct_message_context_free(msg_ctx);
}

TEST(MessageContextUnitTests, GetsRemoteEndpoint) {
    ct_message_context_t* msg_ctx = ct_message_context_new();

    ct_remote_endpoint_t* remote_ep = ct_remote_endpoint_new();
    remote_ep->port = 5678;
    remote_ep->data.resolved_address.ss_family = AF_INET6;

    msg_ctx->remote_endpoint = remote_ep;

    const ct_remote_endpoint_t* retrieved_remote_ep = ct_message_context_get_remote_endpoint(msg_ctx);

    ASSERT_NE(retrieved_remote_ep, nullptr);
    ASSERT_EQ(retrieved_remote_ep->port, 5678);
    ASSERT_EQ(retrieved_remote_ep->data.resolved_address.ss_family, AF_INET6);
    ASSERT_EQ(msg_ctx->remote_endpoint, retrieved_remote_ep);

    ct_message_context_free(msg_ctx);
}

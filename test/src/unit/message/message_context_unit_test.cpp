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

// Message context property wrapper tests
TEST(MessageContextUnitTests, SetAndGetUint64) {
    ct_message_context_t* msg_ctx = ct_message_context_new();
    ASSERT_NE(msg_ctx, nullptr);

    ct_message_context_set_uint64(msg_ctx, MSG_LIFETIME, 5000);

    EXPECT_EQ(ct_message_context_get_uint64(msg_ctx, MSG_LIFETIME), 5000);

    ct_message_context_free(msg_ctx);
}

TEST(MessageContextUnitTests, SetAndGetUint32) {
    ct_message_context_t* msg_ctx = ct_message_context_new();
    ASSERT_NE(msg_ctx, nullptr);

    ct_message_context_set_uint32(msg_ctx, MSG_PRIORITY, 50);

    EXPECT_EQ(ct_message_context_get_uint32(msg_ctx, MSG_PRIORITY), 50);

    ct_message_context_free(msg_ctx);
}

TEST(MessageContextUnitTests, SetAndGetBoolean) {
    ct_message_context_t* msg_ctx = ct_message_context_new();
    ASSERT_NE(msg_ctx, nullptr);

    ct_message_context_set_boolean(msg_ctx, MSG_ORDERED, false);

    EXPECT_FALSE(ct_message_context_get_boolean(msg_ctx, MSG_ORDERED));

    ct_message_context_free(msg_ctx);
}

TEST(MessageContextUnitTests, SetAndGetCapacityProfile) {
    ct_message_context_t* msg_ctx = ct_message_context_new();
    ASSERT_NE(msg_ctx, nullptr);

    ct_message_context_set_capacity_profile(msg_ctx, MSG_CAPACITY_PROFILE, CAPACITY_PROFILE_LOW_LATENCY_INTERACTIVE);

    EXPECT_EQ(ct_message_properties_get_capacity_profile(ct_message_context_get_message_properties(msg_ctx)), CAPACITY_PROFILE_LOW_LATENCY_INTERACTIVE);

    ct_message_context_free(msg_ctx);
}

// Null pointer tests for message context wrappers
TEST(MessageContextUnitTests, SetUint64HandlesNullContext) {
    ct_message_context_set_uint64(nullptr, MSG_LIFETIME, 5000);
    SUCCEED();
}

TEST(MessageContextUnitTests, GetUint64ReturnsZeroForNullContext) {
    EXPECT_EQ(ct_message_context_get_uint64(nullptr, MSG_LIFETIME), 0);
}

TEST(MessageContextUnitTests, SetUint32HandlesNullContext) {
    ct_message_context_set_uint32(nullptr, MSG_PRIORITY, 50);
    SUCCEED();
}

TEST(MessageContextUnitTests, GetUint32ReturnsZeroForNullContext) {
    EXPECT_EQ(ct_message_context_get_uint32(nullptr, MSG_PRIORITY), 0);
}

TEST(MessageContextUnitTests, SetBooleanHandlesNullContext) {
    ct_message_context_set_boolean(nullptr, MSG_ORDERED, true);
    SUCCEED();
}

TEST(MessageContextUnitTests, GetBooleanReturnsFalseForNullContext) {
    EXPECT_FALSE(ct_message_context_get_boolean(nullptr, MSG_ORDERED));
}

TEST(MessageContextUnitTests, SetCapacityProfileHandlesNullContext) {
    ct_message_context_set_capacity_profile(nullptr, MSG_CAPACITY_PROFILE, CAPACITY_PROFILE_LOW_LATENCY_INTERACTIVE);
    SUCCEED();
}

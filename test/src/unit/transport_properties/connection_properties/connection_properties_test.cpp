#include <gmock/gmock-matchers.h>

#include "gtest/gtest.h"
extern "C" {
#include "ctaps.h"
#include "ctaps_internal.h"
#include "transport_property/connection_properties/connection_properties.h"
}

// Getter tests

TEST(ConnectionPropertiesTest, getRecvChecksumLenReturnsCorrectValue) {
    ct_connection_properties_t conn_props;
    ct_connection_properties_build(&conn_props);
    conn_props.list[RECV_CHECKSUM_LEN].value.uint32_val = 16;

    uint64_t recv_checksum_len = ct_connection_properties_get_recv_checksum_len(&conn_props);

    ASSERT_EQ(recv_checksum_len, 16);
}

TEST(ConnectionPropertiesTest, getRecvChecksumLenHandlesNullPointer) {
    uint64_t recv_checksum_len = ct_connection_properties_get_recv_checksum_len(nullptr);

    ASSERT_EQ(recv_checksum_len, 0);
}

TEST(ConnectionPropertiesTest, getConnPriorityReturnsCorrectValue) {
    ct_connection_properties_t conn_props;
    ct_connection_properties_build(&conn_props);
    conn_props.list[CONN_PRIORITY].value.uint32_val = 50;

    uint32_t conn_priority = ct_connection_properties_get_conn_priority(&conn_props);

    ASSERT_EQ(conn_priority, 50);
}

TEST(ConnectionPropertiesTest, getConnPriorityHandlesNullPointer) {
    uint32_t conn_priority = ct_connection_properties_get_conn_priority(nullptr);

    ASSERT_EQ(conn_priority, 0);
}

TEST(ConnectionPropertiesTest, getConnTimeoutReturnsCorrectValue) {
    ct_connection_properties_t conn_props;
    ct_connection_properties_build(&conn_props);
    conn_props.list[CONN_TIMEOUT].value.uint32_val = 30000;

    uint32_t conn_timeout = ct_connection_properties_get_conn_timeout(&conn_props);

    ASSERT_EQ(conn_timeout, 30000);
}

TEST(ConnectionPropertiesTest, getConnTimeoutHandlesNullPointer) {
    uint32_t conn_timeout = ct_connection_properties_get_conn_timeout(nullptr);

    ASSERT_EQ(conn_timeout, 0);
}

TEST(ConnectionPropertiesTest, getKeepAliveTimeoutReturnsCorrectValue) {
    ct_connection_properties_t conn_props;
    ct_connection_properties_build(&conn_props);
    conn_props.list[KEEP_ALIVE_TIMEOUT].value.uint32_val = 60000;

    uint32_t keep_alive_timeout = ct_connection_properties_get_keep_alive_timeout(&conn_props);

    ASSERT_EQ(keep_alive_timeout, 60000);
}

TEST(ConnectionPropertiesTest, getKeepAliveTimeoutHandlesNullPointer) {
    uint32_t keep_alive_timeout = ct_connection_properties_get_keep_alive_timeout(nullptr);

    ASSERT_EQ(keep_alive_timeout, 0);
}

TEST(ConnectionPropertiesTest, getConnSchedulerReturnsCorrectValue) {
    ct_connection_properties_t conn_props;
    ct_connection_properties_build(&conn_props);
    conn_props.list[CONN_SCHEDULER].value.enum_val = CONN_SCHEDULER_WEIGHTED_FAIR_QUEUEING;

    ct_connection_scheduler_enum_t conn_scheduler = ct_connection_properties_get_conn_scheduler(&conn_props);

    ASSERT_EQ(conn_scheduler, CONN_SCHEDULER_WEIGHTED_FAIR_QUEUEING);
}

TEST(ConnectionPropertiesTest, getConnSchedulerHandlesNullPointer) {
    ct_connection_scheduler_enum_t conn_scheduler = ct_connection_properties_get_conn_scheduler(nullptr);

    ASSERT_EQ(conn_scheduler, CONN_SCHEDULER_WEIGHTED_FAIR_QUEUEING);
}

TEST(ConnectionPropertiesTest, getConnCapacityProfileReturnsCorrectValue) {
    ct_connection_properties_t conn_props;
    ct_connection_properties_build(&conn_props);
    conn_props.list[CONN_CAPACITY_PROFILE].value.enum_val = CAPACITY_PROFILE_LOW_LATENCY_INTERACTIVE;

    ct_capacity_profile_enum_t capacity_profile = ct_connection_properties_get_conn_capacity_profile(&conn_props);

    ASSERT_EQ(capacity_profile, CAPACITY_PROFILE_LOW_LATENCY_INTERACTIVE);
}

TEST(ConnectionPropertiesTest, getConnCapacityProfileHandlesNullPointer) {
    ct_capacity_profile_enum_t capacity_profile = ct_connection_properties_get_conn_capacity_profile(nullptr);

    ASSERT_EQ(capacity_profile, CAPACITY_PROFILE_BEST_EFFORT);
}

TEST(ConnectionPropertiesTest, getMultipathPolicyReturnsCorrectValue) {
    ct_connection_properties_t conn_props;
    ct_connection_properties_build(&conn_props);
    conn_props.list[MULTIPATH_POLICY].value.enum_val = MULTIPATH_POLICY_AGGREGATE;

    ct_multipath_policy_enum_t multipath_policy = ct_connection_properties_get_multipath_policy(&conn_props);

    ASSERT_EQ(multipath_policy, MULTIPATH_POLICY_AGGREGATE);
}

TEST(ConnectionPropertiesTest, getMultipathPolicyHandlesNullPointer) {
    ct_multipath_policy_enum_t multipath_policy = ct_connection_properties_get_multipath_policy(nullptr);

    ASSERT_EQ(multipath_policy, MULTIPATH_POLICY_HANDOVER);
}

TEST(ConnectionPropertiesTest, getMinSendRateReturnsCorrectValue) {
    ct_connection_properties_t conn_props;
    ct_connection_properties_build(&conn_props);
    conn_props.list[MIN_SEND_RATE].value.uint64_val = 1000000;

    uint64_t min_send_rate = ct_connection_properties_get_min_send_rate(&conn_props);

    ASSERT_EQ(min_send_rate, 1000000);
}

TEST(ConnectionPropertiesTest, getMinSendRateHandlesNullPointer) {
    uint64_t min_send_rate = ct_connection_properties_get_min_send_rate(nullptr);

    ASSERT_EQ(min_send_rate, 0);
}

TEST(ConnectionPropertiesTest, getMinRecvRateReturnsCorrectValue) {
    ct_connection_properties_t conn_props;
    ct_connection_properties_build(&conn_props);
    conn_props.list[MIN_RECV_RATE].value.uint64_val = 2000000;

    uint64_t min_recv_rate = ct_connection_properties_get_min_recv_rate(&conn_props);

    ASSERT_EQ(min_recv_rate, 2000000);
}

TEST(ConnectionPropertiesTest, getMinRecvRateHandlesNullPointer) {
    uint64_t min_recv_rate = ct_connection_properties_get_min_recv_rate(nullptr);

    ASSERT_EQ(min_recv_rate, 0);
}

TEST(ConnectionPropertiesTest, getMaxSendRateReturnsCorrectValue) {
    ct_connection_properties_t conn_props;
    ct_connection_properties_build(&conn_props);
    conn_props.list[MAX_SEND_RATE].value.uint64_val = 10000000;

    uint64_t max_send_rate = ct_connection_properties_get_max_send_rate(&conn_props);

    ASSERT_EQ(max_send_rate, 10000000);
}

TEST(ConnectionPropertiesTest, getMaxSendRateHandlesNullPointer) {
    uint64_t max_send_rate = ct_connection_properties_get_max_send_rate(nullptr);

    ASSERT_EQ(max_send_rate, 0);
}

TEST(ConnectionPropertiesTest, getMaxRecvRateReturnsCorrectValue) {
    ct_connection_properties_t conn_props;
    ct_connection_properties_build(&conn_props);
    conn_props.list[MAX_RECV_RATE].value.uint64_val = 20000000;

    uint64_t max_recv_rate = ct_connection_properties_get_max_recv_rate(&conn_props);

    ASSERT_EQ(max_recv_rate, 20000000);
}

TEST(ConnectionPropertiesTest, getMaxRecvRateHandlesNullPointer) {
    uint64_t max_recv_rate = ct_connection_properties_get_max_recv_rate(nullptr);

    ASSERT_EQ(max_recv_rate, 0);
}

TEST(ConnectionPropertiesTest, getGroupConnLimitReturnsCorrectValue) {
    ct_connection_properties_t conn_props;
    ct_connection_properties_build(&conn_props);
    conn_props.list[GROUP_CONN_LIMIT].value.uint64_val = 100;

    uint64_t group_conn_limit = ct_connection_properties_get_group_conn_limit(&conn_props);

    ASSERT_EQ(group_conn_limit, 100);
}

TEST(ConnectionPropertiesTest, getGroupConnLimitHandlesNullPointer) {
    uint64_t group_conn_limit = ct_connection_properties_get_group_conn_limit(nullptr);

    ASSERT_EQ(group_conn_limit, 0);
}

TEST(ConnectionPropertiesTest, getIsolateSessionReturnsCorrectValue) {
    ct_connection_properties_t conn_props;
    ct_connection_properties_build(&conn_props);
    conn_props.list[ISOLATE_SESSION].value.bool_val = true;

    bool isolate_session = ct_connection_properties_get_isolate_session(&conn_props);

    ASSERT_TRUE(isolate_session);
}

TEST(ConnectionPropertiesTest, getIsolateSessionHandlesNullPointer) {
    bool isolate_session = ct_connection_properties_get_isolate_session(nullptr);

    ASSERT_FALSE(isolate_session);
}

TEST(ConnectionPropertiesTest, getStateReturnsCorrectValue) {
    ct_connection_properties_t conn_props;
    ct_connection_properties_build(&conn_props);
    conn_props.list[STATE].value.enum_val = CONN_STATE_ESTABLISHED;

    ct_connection_state_enum_t state = ct_connection_properties_get_state(&conn_props);

    ASSERT_EQ(state, CONN_STATE_ESTABLISHED);
}

TEST(ConnectionPropertiesTest, getStateHandlesNullPointer) {
    ct_connection_state_enum_t state = ct_connection_properties_get_state(nullptr);

    ASSERT_EQ(state, CONN_STATE_CLOSED);
}

TEST(ConnectionPropertiesTest, getCanSendReturnsCorrectValue) {
    ct_connection_properties_t conn_props;
    ct_connection_properties_build(&conn_props);
    conn_props.list[CAN_SEND].value.bool_val = true;

    bool can_send = ct_connection_properties_get_can_send(&conn_props);

    ASSERT_TRUE(can_send);
}

TEST(ConnectionPropertiesTest, getCanSendHandlesNullPointer) {
    bool can_send = ct_connection_properties_get_can_send(nullptr);

    ASSERT_FALSE(can_send);
}

TEST(ConnectionPropertiesTest, getCanReceiveReturnsCorrectValue) {
    ct_connection_properties_t conn_props;
    ct_connection_properties_build(&conn_props);
    conn_props.list[CAN_RECEIVE].value.bool_val = true;

    bool can_receive = ct_connection_properties_get_can_receive(&conn_props);

    ASSERT_TRUE(can_receive);
}

TEST(ConnectionPropertiesTest, getCanReceiveHandlesNullPointer) {
    bool can_receive = ct_connection_properties_get_can_receive(nullptr);

    ASSERT_FALSE(can_receive);
}

TEST(ConnectionPropertiesTest, getSingularTransmissionMsgMaxLenReturnsCorrectValue) {
    ct_connection_properties_t conn_props;
    ct_connection_properties_build(&conn_props);
    conn_props.list[SINGULAR_TRANSMISSION_MSG_MAX_LEN].value.uint64_val = 65535;

    uint64_t max_len = ct_connection_properties_get_singular_transmission_msg_max_len(&conn_props);

    ASSERT_EQ(max_len, 65535);
}

TEST(ConnectionPropertiesTest, getSingularTransmissionMsgMaxLenHandlesNullPointer) {
    uint64_t max_len = ct_connection_properties_get_singular_transmission_msg_max_len(nullptr);

    ASSERT_EQ(max_len, 0);
}

TEST(ConnectionPropertiesTest, getSendMessageMaxLenReturnsCorrectValue) {
    ct_connection_properties_t conn_props;
    ct_connection_properties_build(&conn_props);
    conn_props.list[SEND_MESSAGE_MAX_LEN].value.uint64_val = 1048576;

    uint64_t max_len = ct_connection_properties_get_send_message_max_len(&conn_props);

    ASSERT_EQ(max_len, 1048576);
}

TEST(ConnectionPropertiesTest, getSendMessageMaxLenHandlesNullPointer) {
    uint64_t max_len = ct_connection_properties_get_send_message_max_len(nullptr);

    ASSERT_EQ(max_len, 0);
}

TEST(ConnectionPropertiesTest, getRecvMessageMaxLenReturnsCorrectValue) {
    ct_connection_properties_t conn_props;
    ct_connection_properties_build(&conn_props);
    conn_props.list[RECV_MESSAGE_MAX_LEN].value.uint64_val = 2097152;

    uint64_t max_len = ct_connection_properties_get_recv_message_max_len(&conn_props);

    ASSERT_EQ(max_len, 2097152);
}

TEST(ConnectionPropertiesTest, getRecvMessageMaxLenHandlesNullPointer) {
    uint64_t max_len = ct_connection_properties_get_recv_message_max_len(nullptr);

    ASSERT_EQ(max_len, 0);
}

TEST(ConnectionPropertiesTest, getUserTimeoutValueMsReturnsCorrectValue) {
    ct_connection_properties_t conn_props;
    ct_connection_properties_build(&conn_props);
    conn_props.list[USER_TIMEOUT_VALUE_MS].value.uint32_val = 5000;

    uint32_t timeout = ct_connection_properties_get_user_timeout_value_ms(&conn_props);

    ASSERT_EQ(timeout, 5000);
}

TEST(ConnectionPropertiesTest, getUserTimeoutValueMsHandlesNullPointer) {
    uint32_t timeout = ct_connection_properties_get_user_timeout_value_ms(nullptr);

    ASSERT_EQ(timeout, 0);
}

TEST(ConnectionPropertiesTest, getUserTimeoutEnabledReturnsCorrectValue) {
    ct_connection_properties_t conn_props;
    ct_connection_properties_build(&conn_props);
    conn_props.list[USER_TIMEOUT_ENABLED].value.bool_val = true;

    bool enabled = ct_connection_properties_get_user_timeout_enabled(&conn_props);

    ASSERT_TRUE(enabled);
}

TEST(ConnectionPropertiesTest, getUserTimeoutEnabledHandlesNullPointer) {
    bool enabled = ct_connection_properties_get_user_timeout_enabled(nullptr);

    ASSERT_FALSE(enabled);
}

TEST(ConnectionPropertiesTest, getUserTimeoutChangeableReturnsCorrectValue) {
    ct_connection_properties_t conn_props;
    ct_connection_properties_build(&conn_props);
    conn_props.list[USER_TIMEOUT_CHANGEABLE].value.bool_val = false;

    bool changeable = ct_connection_properties_get_user_timeout_changeable(&conn_props);

    ASSERT_FALSE(changeable);
}

TEST(ConnectionPropertiesTest, getUserTimeoutChangeableHandlesNullPointer) {
    bool changeable = ct_connection_properties_get_user_timeout_changeable(nullptr);

    ASSERT_FALSE(changeable);
}

// Setter tests

TEST(ConnectionPropertiesTest, setRecvChecksumLenSetsCorrectValue) {
    ct_connection_properties_t conn_props;
    ct_connection_properties_build(&conn_props);

    ct_connection_properties_set_recv_checksum_len(&conn_props, 32);

    ASSERT_EQ(ct_connection_properties_get_recv_checksum_len(&conn_props), 32);
}

TEST(ConnectionPropertiesTest, setRecvChecksumLenHandlesNullPointer) {
    ct_connection_properties_set_recv_checksum_len(nullptr, 32);
    // Test passes if no crash occurs
}

TEST(ConnectionPropertiesTest, setConnPrioritySetsCorrectValue) {
    ct_connection_properties_t conn_props;
    ct_connection_properties_build(&conn_props);

    ct_connection_properties_set_conn_priority(&conn_props, 75);

    ASSERT_EQ(ct_connection_properties_get_conn_priority(&conn_props), 75);
}

TEST(ConnectionPropertiesTest, setConnPriorityHandlesNullPointer) {
    ct_connection_properties_set_conn_priority(nullptr, 75);
    // Test passes if no crash occurs
}

TEST(ConnectionPropertiesTest, setConnTimeoutSetsCorrectValue) {
    ct_connection_properties_t conn_props;
    ct_connection_properties_build(&conn_props);

    ct_connection_properties_set_conn_timeout(&conn_props, 45000);

    ASSERT_EQ(ct_connection_properties_get_conn_timeout(&conn_props), 45000);
}

TEST(ConnectionPropertiesTest, setConnTimeoutHandlesNullPointer) {
    ct_connection_properties_set_conn_timeout(nullptr, 45000);
    // Test passes if no crash occurs
}

TEST(ConnectionPropertiesTest, setKeepAliveTimeoutSetsCorrectValue) {
    ct_connection_properties_t conn_props;
    ct_connection_properties_build(&conn_props);

    ct_connection_properties_set_keep_alive_timeout(&conn_props, 90000);

    ASSERT_EQ(ct_connection_properties_get_keep_alive_timeout(&conn_props), 90000);
}

TEST(ConnectionPropertiesTest, setKeepAliveTimeoutHandlesNullPointer) {
    ct_connection_properties_set_keep_alive_timeout(nullptr, 90000);
    // Test passes if no crash occurs
}

TEST(ConnectionPropertiesTest, setConnSchedulerSetsCorrectValue) {
    ct_connection_properties_t conn_props;
    ct_connection_properties_build(&conn_props);

    ct_connection_properties_set_conn_scheduler(&conn_props, CONN_SCHEDULER_WEIGHTED_FAIR_QUEUEING);

    ASSERT_EQ(ct_connection_properties_get_conn_scheduler(&conn_props), CONN_SCHEDULER_WEIGHTED_FAIR_QUEUEING);
}

TEST(ConnectionPropertiesTest, setConnSchedulerHandlesNullPointer) {
    ct_connection_properties_set_conn_scheduler(nullptr, CONN_SCHEDULER_WEIGHTED_FAIR_QUEUEING);
    // Test passes if no crash occurs
}

TEST(ConnectionPropertiesTest, setConnCapacityProfileSetsCorrectValue) {
    ct_connection_properties_t conn_props;
    ct_connection_properties_build(&conn_props);

    ct_connection_properties_set_conn_capacity_profile(&conn_props, CAPACITY_PROFILE_SCAVENGER);

    ASSERT_EQ(ct_connection_properties_get_conn_capacity_profile(&conn_props), CAPACITY_PROFILE_SCAVENGER);
}

TEST(ConnectionPropertiesTest, setConnCapacityProfileHandlesNullPointer) {
    ct_connection_properties_set_conn_capacity_profile(nullptr, CAPACITY_PROFILE_SCAVENGER);
    // Test passes if no crash occurs
}

TEST(ConnectionPropertiesTest, setMultipathPolicySetsCorrectValue) {
    ct_connection_properties_t conn_props;
    ct_connection_properties_build(&conn_props);

    ct_connection_properties_set_multipath_policy(&conn_props, MULTIPATH_POLICY_INTERACTIVE);

    ASSERT_EQ(ct_connection_properties_get_multipath_policy(&conn_props), MULTIPATH_POLICY_INTERACTIVE);
}

TEST(ConnectionPropertiesTest, setMultipathPolicyHandlesNullPointer) {
    ct_connection_properties_set_multipath_policy(nullptr, MULTIPATH_POLICY_INTERACTIVE);
    // Test passes if no crash occurs
}

TEST(ConnectionPropertiesTest, setMinSendRateSetsCorrectValue) {
    ct_connection_properties_t conn_props;
    ct_connection_properties_build(&conn_props);

    ct_connection_properties_set_min_send_rate(&conn_props, 500000);

    ASSERT_EQ(ct_connection_properties_get_min_send_rate(&conn_props), 500000);
}

TEST(ConnectionPropertiesTest, setMinSendRateHandlesNullPointer) {
    ct_connection_properties_set_min_send_rate(nullptr, 500000);
    // Test passes if no crash occurs
}

TEST(ConnectionPropertiesTest, setMinRecvRateSetsCorrectValue) {
    ct_connection_properties_t conn_props;
    ct_connection_properties_build(&conn_props);

    ct_connection_properties_set_min_recv_rate(&conn_props, 750000);

    ASSERT_EQ(ct_connection_properties_get_min_recv_rate(&conn_props), 750000);
}

TEST(ConnectionPropertiesTest, setMinRecvRateHandlesNullPointer) {
    ct_connection_properties_set_min_recv_rate(nullptr, 750000);
    // Test passes if no crash occurs
}

TEST(ConnectionPropertiesTest, setMaxSendRateSetsCorrectValue) {
    ct_connection_properties_t conn_props;
    ct_connection_properties_build(&conn_props);

    ct_connection_properties_set_max_send_rate(&conn_props, 5000000);

    ASSERT_EQ(ct_connection_properties_get_max_send_rate(&conn_props), 5000000);
}

TEST(ConnectionPropertiesTest, setMaxSendRateHandlesNullPointer) {
    ct_connection_properties_set_max_send_rate(nullptr, 5000000);
    // Test passes if no crash occurs
}

TEST(ConnectionPropertiesTest, setMaxRecvRateSetsCorrectValue) {
    ct_connection_properties_t conn_props;
    ct_connection_properties_build(&conn_props);

    ct_connection_properties_set_max_recv_rate(&conn_props, 7500000);

    ASSERT_EQ(ct_connection_properties_get_max_recv_rate(&conn_props), 7500000);
}

TEST(ConnectionPropertiesTest, setMaxRecvRateHandlesNullPointer) {
    ct_connection_properties_set_max_recv_rate(nullptr, 7500000);
    // Test passes if no crash occurs
}

TEST(ConnectionPropertiesTest, setGroupConnLimitSetsCorrectValue) {
    ct_connection_properties_t conn_props;
    ct_connection_properties_build(&conn_props);

    ct_connection_properties_set_group_conn_limit(&conn_props, 50);

    ASSERT_EQ(ct_connection_properties_get_group_conn_limit(&conn_props), 50);
}

TEST(ConnectionPropertiesTest, setGroupConnLimitHandlesNullPointer) {
    ct_connection_properties_set_group_conn_limit(nullptr, 50);
    // Test passes if no crash occurs
}

TEST(ConnectionPropertiesTest, setIsolateSessionSetsCorrectValue) {
    ct_connection_properties_t conn_props;
    ct_connection_properties_build(&conn_props);

    ct_connection_properties_set_isolate_session(&conn_props, true);

    ASSERT_TRUE(ct_connection_properties_get_isolate_session(&conn_props));
}

TEST(ConnectionPropertiesTest, setIsolateSessionHandlesNullPointer) {
    ct_connection_properties_set_isolate_session(nullptr, true);
    // Test passes if no crash occurs
}

TEST(ConnectionPropertiesTest, setUserTimeoutValueMsSetsCorrectValue) {
    ct_connection_properties_t conn_props;
    ct_connection_properties_build(&conn_props);

    ct_connection_properties_set_user_timeout_value_ms(&conn_props, 10000);

    ASSERT_EQ(ct_connection_properties_get_user_timeout_value_ms(&conn_props), 10000);
}

TEST(ConnectionPropertiesTest, setUserTimeoutValueMsHandlesNullPointer) {
    ct_connection_properties_set_user_timeout_value_ms(nullptr, 10000);
    // Test passes if no crash occurs
}

TEST(ConnectionPropertiesTest, setUserTimeoutEnabledSetsCorrectValue) {
    ct_connection_properties_t conn_props;
    ct_connection_properties_build(&conn_props);

    ct_connection_properties_set_user_timeout_enabled(&conn_props, true);

    ASSERT_TRUE(ct_connection_properties_get_user_timeout_enabled(&conn_props));
}

TEST(ConnectionPropertiesTest, setUserTimeoutEnabledHandlesNullPointer) {
    ct_connection_properties_set_user_timeout_enabled(nullptr, true);
    // Test passes if no crash occurs
}

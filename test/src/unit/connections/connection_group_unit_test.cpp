#include <gmock/gmock-matchers.h>
#include "gtest/gtest.h"
#include "fff.h"
#include "fixtures/integration_fixture.h"
extern "C" {
#include <stdio.h>
#include "ctaps.h"
#include "ctaps_internal.h"
#include <connection/connection.h>
#include <logging/log.h>
#include <connection/connection_group.h>
#include <connection/socket_manager/socket_manager.h>
#include <util/uuid_util.h>

DEFINE_FFF_GLOBALS;
FAKE_VALUE_FUNC(int, fake_protocol_close, ct_connection_t*);
FAKE_VOID_FUNC(fake_protocol_abort, ct_connection_t*);
FAKE_VALUE_FUNC(int, __wrap_ct_connection_set_active_remote_endpoint, ct_connection_t*, const ct_remote_endpoint_t*);
FAKE_VALUE_FUNC(int, __wrap_ct_connection_set_active_local_endpoint, ct_connection_t*, const ct_local_endpoint_t*);
FAKE_VOID_FUNC(free_connection_state, ct_connection_t*);
FAKE_VOID_FUNC(__wrap_ct_socket_manager_free_connection_state, ct_connection_t*);
FAKE_VOID_FUNC(__wrap_ct_connection_abort, ct_connection_t*);
}

// ── Fixture ───────────────────────────────────────────────────────────────────

class ConnectionGroupUnitTests : public ::testing::Test {
protected:
    static constexpr int num_connections = 4;

    ct_connection_group_t* group = nullptr;
    ct_connection_t connections[num_connections];

    void SetUp() override {
        RESET_FAKE(fake_protocol_close);
        RESET_FAKE(fake_protocol_abort);
        RESET_FAKE(__wrap_ct_connection_set_active_remote_endpoint);
        RESET_FAKE(__wrap_ct_connection_set_active_local_endpoint);
        RESET_FAKE(__wrap_ct_connection_abort);
        FFF_RESET_HISTORY();

        __wrap_ct_connection_set_active_remote_endpoint_fake.return_val = 0;
        __wrap_ct_connection_set_active_local_endpoint_fake.return_val = 0;

        group = ct_connection_group_new();
        for (int i = 0; i < num_connections; i++) {
            memset(&connections[i], 0, sizeof(ct_connection_t));
            snprintf(connections[i].uuid, sizeof(connections[i].uuid), "test-uuid-%d", i);
            g_hash_table_insert(group->connections, connections[i].uuid, &connections[i]);
        }
    }

    void TearDown() override {
        g_hash_table_remove_all(group->connections);
        ct_connection_group_free(group);

    }

    ct_remote_endpoint_t dummy_remote = {0};
    ct_local_endpoint_t dummy_local = {0};
};

// ── abort_all ─────────────────────────────────────────────────────────────────

TEST_F(ConnectionGroupUnitTests, AbortAll_AllOpen_AbortsAll) {
    for (int i = 0; i < num_connections; i++) {
        ct_connection_mark_as_established(&connections[i]);
    }

    ct_connection_group_abort_all(group);

    EXPECT_EQ(__wrap_ct_connection_abort_fake.call_count, num_connections);
}

TEST_F(ConnectionGroupUnitTests, AbortAll_AllClosed_AbortsNone) {
    for (int i = 0; i < num_connections; i++) {
        ct_connection_mark_as_closed(&connections[i]);
    }

    ct_connection_group_abort_all(group);

    EXPECT_EQ(__wrap_ct_connection_abort_fake.call_count, 0);
}

TEST_F(ConnectionGroupUnitTests, AbortAll_MixedStates_SkipsOnlyClosed) {
    ct_connection_mark_as_established(&connections[0]);  // should abort
    ct_connection_mark_as_closed(&connections[1]);       // should skip
    ct_connection_mark_as_established(&connections[2]);  // should abort
    ct_connection_mark_as_closing(&connections[3]);      // should abort

    ct_connection_group_abort_all(group);

    EXPECT_EQ(__wrap_ct_connection_abort_fake.call_count, 3);
}

// ── set_active_remote_endpoint ────────────────────────────────────────────────

TEST_F(ConnectionGroupUnitTests, setActiveRemoteEndpoint_NullGroupDies) {
    EXPECT_DEATH(ct_connection_group_set_active_remote_endpoint(nullptr, &dummy_remote), "");
    EXPECT_EQ(__wrap_ct_connection_set_active_remote_endpoint_fake.call_count, 0u);
}

TEST_F(ConnectionGroupUnitTests, setActiveRemoteEndpoint_NullEndpointDies) {
    EXPECT_DEATH(ct_connection_group_set_active_remote_endpoint(group, nullptr), "");
    EXPECT_EQ(__wrap_ct_connection_set_active_remote_endpoint_fake.call_count, 0u);
}

TEST_F(ConnectionGroupUnitTests, setActiveRemoteEndpoint_CallsSetterOnAllConnections) {
    EXPECT_EQ(ct_connection_group_set_active_remote_endpoint(group, &dummy_remote), 0);
    EXPECT_EQ(__wrap_ct_connection_set_active_remote_endpoint_fake.call_count, (unsigned)num_connections);
    for (int i = 0; i < num_connections; i++) {
        EXPECT_EQ(__wrap_ct_connection_set_active_remote_endpoint_fake.arg1_history[i], &dummy_remote);
    }
}

TEST_F(ConnectionGroupUnitTests, setActiveRemoteEndpoint_ContinuesAndReturnsErrorIfAnySetterFails) {

    int setter_returns[] = {0, -EINVAL, 0, 0};
    SET_RETURN_SEQ(__wrap_ct_connection_set_active_remote_endpoint, setter_returns, 4);

    EXPECT_NE(ct_connection_group_set_active_remote_endpoint(group, &dummy_remote), 0);
    EXPECT_EQ(__wrap_ct_connection_set_active_remote_endpoint_fake.call_count, (unsigned)num_connections);
}

// ── set_active_local_endpoint ─────────────────────────────────────────────────

TEST_F(ConnectionGroupUnitTests, setActiveLocalEndpoint_NullGroupDies) {
    EXPECT_DEATH(ct_connection_group_set_active_local_endpoint(nullptr, &dummy_local), "");
    EXPECT_EQ(__wrap_ct_connection_set_active_local_endpoint_fake.call_count, 0u);
}

TEST_F(ConnectionGroupUnitTests, setActiveLocalEndpoint_NullEndpointDies) {
    EXPECT_DEATH(ct_connection_group_set_active_local_endpoint(group, nullptr), "");
}

TEST_F(ConnectionGroupUnitTests, setActiveLocalEndpoint_CallsSetterOnAllConnections) {
    EXPECT_EQ(ct_connection_group_set_active_local_endpoint(group, &dummy_local), 0);
    EXPECT_EQ(__wrap_ct_connection_set_active_local_endpoint_fake.call_count, (unsigned)num_connections);
    for (int i = 0; i < num_connections; i++) {
        EXPECT_EQ(__wrap_ct_connection_set_active_local_endpoint_fake.arg1_history[i], &dummy_local);
    }
}

TEST_F(ConnectionGroupUnitTests, setActiveLocalEndpoint_ContinuesAndReturnsErrorIfAnySetterFails) {
    int setter_returns[] = {0, -EINVAL, 0, 0};
    SET_RETURN_SEQ(__wrap_ct_connection_set_active_local_endpoint, setter_returns, 4);

    EXPECT_NE(ct_connection_group_set_active_local_endpoint(group, &dummy_local), 0);
    EXPECT_EQ(__wrap_ct_connection_set_active_local_endpoint_fake.call_count, (unsigned)num_connections);
}

#include <gmock/gmock-matchers.h>
#include "gtest/gtest.h"
#include "fff.h"
#include "fixtures/integration_fixture.h"
extern "C" {
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
}

// ── Fixture ───────────────────────────────────────────────────────────────────

class ConnectionGroupUnitTests : public ::testing::Test {
protected:
    static constexpr int kNumConnections = 4;

    ct_connection_group_t* group = nullptr;
    ct_connection_t* connections[kNumConnections];

    void SetUp() override {
        RESET_FAKE(fake_protocol_close);
        RESET_FAKE(fake_protocol_abort);
        RESET_FAKE(__wrap_ct_connection_set_active_remote_endpoint);
        RESET_FAKE(__wrap_ct_connection_set_active_local_endpoint);
        FFF_RESET_HISTORY();

        __wrap_ct_connection_set_active_remote_endpoint_fake.return_val = 0;
        __wrap_ct_connection_set_active_local_endpoint_fake.return_val = 0;

        group = generate_connection_group(kNumConnections);

        int i = 0;
        GHashTableIter iter;
        gpointer key, value;
        g_hash_table_iter_init(&iter, group->connections);
        while (g_hash_table_iter_next(&iter, &key, &value)) {
            connections[i++] = (ct_connection_t*)value;
        }
    }

    void TearDown() override {
        for (int i = 0; i < kNumConnections; i++) {
            ct_connection_free(connections[i]);
        }
    }

    ct_remote_endpoint_t dummy_remote = {0};
    ct_local_endpoint_t dummy_local = {0};
};

// ── abort_all ─────────────────────────────────────────────────────────────────

TEST_F(ConnectionGroupUnitTests, abortAllAbortsOnlyOpenOrClosingConnections) {
    ct_protocol_impl_t protocol_impl;
    protocol_impl.abort = fake_protocol_abort;
    protocol_impl.name = "fake_protocol";
    ct_socket_manager_t* socket_manager = ct_socket_manager_new(&protocol_impl, NULL);

    for (int i = 0; i < kNumConnections; i++) {
        connections[i]->socket_manager = socket_manager;
    }

    ct_connection_mark_as_established(connections[0]);  // should abort
    ct_connection_mark_as_closed(connections[1]);       // should skip
    ct_connection_mark_as_established(connections[2]);  // should abort
    ct_connection_mark_as_closing(connections[3]);      // should abort

    ct_connection_group_abort_all(group);

    EXPECT_EQ(g_hash_table_size(group->connections), 4);
    EXPECT_EQ(fake_protocol_abort_fake.call_count, 3);

    // Order is non-deterministic due to hash table iteration
    ct_connection_t* aborted[3] = {
        fake_protocol_abort_fake.arg0_history[0],
        fake_protocol_abort_fake.arg0_history[1],
        fake_protocol_abort_fake.arg0_history[2],
    };
    EXPECT_TRUE(connections[0] == aborted[0] || connections[0] == aborted[1] || connections[0] == aborted[2]);
    EXPECT_TRUE(connections[2] == aborted[0] || connections[2] == aborted[1] || connections[2] == aborted[2]);
    EXPECT_TRUE(connections[3] == aborted[0] || connections[3] == aborted[1] || connections[3] == aborted[2]);
    EXPECT_NE(aborted[0], aborted[1]);
    EXPECT_NE(aborted[1], aborted[2]);
    EXPECT_NE(aborted[0], aborted[2]);
}

// ── set_active_remote_endpoint ────────────────────────────────────────────────

TEST_F(ConnectionGroupUnitTests, setActiveRemoteEndpoint_NullGroup_ReturnsEINVAL) {
    EXPECT_EQ(ct_connection_group_set_active_remote_endpoint(nullptr, &dummy_remote), -EINVAL);
    EXPECT_EQ(__wrap_ct_connection_set_active_remote_endpoint_fake.call_count, 0u);
}

TEST_F(ConnectionGroupUnitTests, setActiveRemoteEndpoint_NullEndpoint_ReturnsEINVAL) {
    EXPECT_EQ(ct_connection_group_set_active_remote_endpoint(group, nullptr), -EINVAL);
    EXPECT_EQ(__wrap_ct_connection_set_active_remote_endpoint_fake.call_count, 0u);
}

TEST_F(ConnectionGroupUnitTests, setActiveRemoteEndpoint_CallsSetterOnAllConnections) {
    EXPECT_EQ(ct_connection_group_set_active_remote_endpoint(group, &dummy_remote), 0);
    EXPECT_EQ(__wrap_ct_connection_set_active_remote_endpoint_fake.call_count, (unsigned)kNumConnections);
    for (int i = 0; i < kNumConnections; i++) {
        EXPECT_EQ(__wrap_ct_connection_set_active_remote_endpoint_fake.arg1_history[i], &dummy_remote);
    }
}

TEST_F(ConnectionGroupUnitTests, setActiveRemoteEndpoint_ContinuesAndReturnsErrorIfAnySetterFails) {

    int setter_returns[] = {0, -EINVAL, 0, 0};
    SET_RETURN_SEQ(__wrap_ct_connection_set_active_remote_endpoint, setter_returns, 4);

    EXPECT_NE(ct_connection_group_set_active_remote_endpoint(group, &dummy_remote), 0);
    EXPECT_EQ(__wrap_ct_connection_set_active_remote_endpoint_fake.call_count, (unsigned)kNumConnections);
}

// ── set_active_local_endpoint ─────────────────────────────────────────────────

TEST_F(ConnectionGroupUnitTests, setActiveLocalEndpoint_NullGroup_ReturnsEINVAL) {
    EXPECT_EQ(ct_connection_group_set_active_local_endpoint(nullptr, &dummy_local), -EINVAL);
    EXPECT_EQ(__wrap_ct_connection_set_active_local_endpoint_fake.call_count, 0u);
}

TEST_F(ConnectionGroupUnitTests, setActiveLocalEndpoint_NullEndpoint_ReturnsEINVAL) {
    EXPECT_EQ(ct_connection_group_set_active_local_endpoint(group, nullptr), -EINVAL);
    EXPECT_EQ(__wrap_ct_connection_set_active_local_endpoint_fake.call_count, 0u);
}

TEST_F(ConnectionGroupUnitTests, setActiveLocalEndpoint_CallsSetterOnAllConnections) {
    EXPECT_EQ(ct_connection_group_set_active_local_endpoint(group, &dummy_local), 0);
    EXPECT_EQ(__wrap_ct_connection_set_active_local_endpoint_fake.call_count, (unsigned)kNumConnections);
    for (int i = 0; i < kNumConnections; i++) {
        EXPECT_EQ(__wrap_ct_connection_set_active_local_endpoint_fake.arg1_history[i], &dummy_local);
    }
}

TEST_F(ConnectionGroupUnitTests, setActiveLocalEndpoint_ContinuesAndReturnsErrorIfAnySetterFails) {
    int setter_returns[] = {0, -EINVAL, 0, 0};
    SET_RETURN_SEQ(__wrap_ct_connection_set_active_local_endpoint, setter_returns, 4);

    EXPECT_NE(ct_connection_group_set_active_local_endpoint(group, &dummy_local), 0);
    EXPECT_EQ(__wrap_ct_connection_set_active_local_endpoint_fake.call_count, (unsigned)kNumConnections);
}

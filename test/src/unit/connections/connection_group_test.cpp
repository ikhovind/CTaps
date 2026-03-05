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
}




TEST(ConnectionGroupUnitTests, abortAllabortsOnlyOpenOrClosingConnections) {
    RESET_FAKE(fake_protocol_abort);

    ct_protocol_impl_t protocol_impl;
    protocol_impl.abort = fake_protocol_abort;
    protocol_impl.name = "fake_protocol";

    ct_socket_manager_t* socket_manager = ct_socket_manager_new(&protocol_impl, NULL);

    ct_connection_group_t* group = generate_connection_group(4);
    ct_connection_t* connections[4];

    int counter = 0;
    GHashTableIter iter;
    gpointer key, value;
    g_hash_table_iter_init(&iter, group->connections);
    while (g_hash_table_iter_next(&iter, &key, &value)) {
        ct_connection_t* conn = (ct_connection_t*)value;
        conn->socket_manager = socket_manager;
        connections[counter++] = conn;
    }

    // Connection 1: Established (should be abortd)
    ct_connection_mark_as_established(connections[0]);

    // Connection 2: Already closed (should be skipped)
    ct_connection_mark_as_closed(connections[1]);

    // Connection 3: Established (should be abortd)
    ct_connection_mark_as_established(connections[2]);

    // Connection 4: closing, should be aborted
    ct_connection_mark_as_closing(connections[3]);

    // Call abort_all
    ct_connection_group_abort_all(group);

    // check length of internal hash table
    EXPECT_EQ(g_hash_table_size(group->connections), 4);

    // Verify: abort called exactly twice (conn1 and conn3, not conn2)
    EXPECT_EQ(fake_protocol_abort_fake.call_count, 3);

    // Verify the correct connections were passed to abort
    // The order might vary due to hash table iteration, so check both are present
    ct_connection_t* abortd_conn1 = fake_protocol_abort_fake.arg0_history[0];
    ct_connection_t* abortd_conn2 = fake_protocol_abort_fake.arg0_history[1];
    ct_connection_t* abortd_conn3 = fake_protocol_abort_fake.arg0_history[2];

    EXPECT_TRUE(connections[0] == abortd_conn1 || connections[0] == abortd_conn2 || connections[0] == abortd_conn3);
    EXPECT_TRUE(connections[2] == abortd_conn1 || connections[2] == abortd_conn2 || connections[2] == abortd_conn3);
    EXPECT_TRUE(connections[3] == abortd_conn1 || connections[3] == abortd_conn2 || connections[3] == abortd_conn3);

    EXPECT_FALSE(abortd_conn1 == abortd_conn2);
    EXPECT_FALSE(abortd_conn2 == abortd_conn3);
    EXPECT_FALSE(abortd_conn1 == abortd_conn3);

    // Cleanup
    ct_connection_free(connections[0]);
    ct_connection_free(connections[1]);
    ct_connection_free(connections[2]);
    ct_connection_free(connections[3]);
}

TEST(ConnectionGroupUnitTests, setActiveEndpointInvokesOnEachConnection) {
    ct_connection_group_t* group = generate_connection_group(4);

    ct_connection_t* connections[4];

    int counter = 0;
    GHashTableIter iter;
    gpointer key, value;
    g_hash_table_iter_init(&iter, group->connections);
    while (g_hash_table_iter_next(&iter, &key, &value)) {
        ct_connection_t* conn = (ct_connection_t*)value;
        connections[counter++] = conn;
    }

    ct_remote_endpoint_t dummy = {0};

    int rc = ct_connection_group_set_active_remote_endpoint(group, &dummy);

    ASSERT_EQ(__wrap_ct_connection_set_active_remote_endpoint_fake.call_count, 4);
    for (int i = 0; i < 4; i++) {
        ASSERT_EQ(__wrap_ct_connection_set_active_remote_endpoint_fake.arg0_history[i], connections[i]);
        ASSERT_EQ(__wrap_ct_connection_set_active_remote_endpoint_fake.arg1_history[i], &dummy);
    }

    ct_connection_free(connections[0]);
    ct_connection_free(connections[1]);
    ct_connection_free(connections[2]);
    ct_connection_free(connections[3]);
}

TEST(ConnectionGroupUnitTests, setActiveEndpointHandlesNullGroup) {
    ct_remote_endpoint_t dummy;
    int rc = ct_connection_group_set_active_remote_endpoint(nullptr, &dummy);
}

TEST(ConnectionGroupUnitTests, setActiveEndpointHandlesNullRemote) {
    ct_connection_group_t* group = generate_connection_group(4);

    ct_connection_t* connections[4];

    int counter = 0;
    GHashTableIter iter;
    gpointer key, value;
    g_hash_table_iter_init(&iter, group->connections);
    while (g_hash_table_iter_next(&iter, &key, &value)) {
        ct_connection_t* conn = (ct_connection_t*)value;
        connections[counter++] = conn;
    }

    ct_remote_endpoint_t dummy = {0};

    int rc = ct_connection_group_set_active_remote_endpoint(group, nullptr);

    ASSERT_EQ(__wrap_ct_connection_set_active_remote_endpoint_fake.call_count, 0);

    ct_connection_free(connections[0]);
    ct_connection_free(connections[1]);
    ct_connection_free(connections[2]);
    ct_connection_free(connections[3]);
}

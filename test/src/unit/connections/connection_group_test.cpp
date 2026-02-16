#include <gmock/gmock-matchers.h>

#include "gtest/gtest.h"
#include "fff.h"
extern "C" {
#include "ctaps.h"
#include "ctaps_internal.h"
#include <connection/connection.h>
#include <logging/log.h>
#include <connection/connection_group.h>
#include <connection/socket_manager/socket_manager.h>
#include <util/uuid_util.h>
#include "fixtures/awaiting_fixture.cpp"
}

DEFINE_FFF_GLOBALS;
FAKE_VALUE_FUNC(int, fake_protocol_close, ct_connection_t*, ct_on_connection_close_cb);
FAKE_VOID_FUNC(fake_protocol_abort, ct_connection_t*);



TEST(ConnectionGroupUnitTests, CloseAllClosesOnlyOpenConnections) {
    GTEST_SKIP(); // Until we finish refactoring.
    RESET_FAKE(fake_protocol_close);
    fake_protocol_close_fake.return_val = 0;

    // Create a shared connection group
    ct_connection_group_t group;


    generate_uuid_string(group.connection_group_id);
    group.connections = g_hash_table_new(g_str_hash, g_str_equal);
    group.connection_group_state = NULL;

    ct_protocol_impl_t protocol_impl;
    protocol_impl.close = fake_protocol_close;

    ct_socket_manager_t* socket_manager = ct_socket_manager_new(&protocol_impl, NULL);

    // Connection 1: Established (should be closed)
    ct_connection_t conn1;
    memset(&conn1, 0, sizeof(ct_connection_t));
    conn1.socket_manager = socket_manager;
    generate_uuid_string(conn1.uuid);
    ct_connection_mark_as_established(&conn1);
    ct_connection_group_add_connection(&group, &conn1);

    // Connection 2: Already closing (should be skipped)
    ct_connection_t conn2;
    memset(&conn2, 0, sizeof(ct_connection_t));
    conn2.socket_manager = socket_manager;
    generate_uuid_string(conn2.uuid);
    ct_connection_mark_as_closing(&conn2);
    ct_connection_group_add_connection(&group, &conn2);

    // Connection 3: Established (should be closed)
    ct_connection_t conn3;
    memset(&conn3, 0, sizeof(ct_connection_t));
    conn3.socket_manager = socket_manager;
    generate_uuid_string(conn3.uuid);
    ct_connection_mark_as_established(&conn3);
    ct_connection_group_add_connection(&group, &conn3);

    // Connection 4: closing
    ct_connection_t conn4;
    memset(&conn4, 0, sizeof(ct_connection_t));
    conn4.socket_manager = socket_manager;
    generate_uuid_string(conn4.uuid);
    ct_connection_mark_as_closed(&conn4);
    ct_connection_group_add_connection(&group, &conn4);


    // Call close_all
    ct_connection_group_close_all(&group);

    // check length of internal hash table
    EXPECT_EQ(g_hash_table_size(group.connections), 4);

    // Verify: close called exactly twice (conn1 and conn3, not conn2)
    EXPECT_EQ(fake_protocol_close_fake.call_count, 2);

    // Verify the correct connections were passed to close
    // The order might vary due to hash table iteration, so check both are present
    ct_connection_t* closed_conn1 = fake_protocol_close_fake.arg0_history[0];
    ct_connection_t* closed_conn2 = fake_protocol_close_fake.arg0_history[1];

    // Both closed connections should be either conn1 or conn3, but not conn2
    EXPECT_TRUE((closed_conn1 == &conn1 && closed_conn2 == &conn3) ||
                (closed_conn1 == &conn3 && closed_conn2 == &conn1));

    // Cleanup
    ct_connection_free_content(&conn1);
    ct_connection_free_content(&conn2);
    ct_connection_free_content(&conn3);
    g_hash_table_destroy(group.connections);
}

TEST(ConnectionGroupUnitTests, abortAllabortsOnlyOpenOrClosingConnections) {
    RESET_FAKE(fake_protocol_abort);

    ct_protocol_impl_t protocol_impl;
    protocol_impl.abort = fake_protocol_abort;

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

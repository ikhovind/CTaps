#include <gmock/gmock-matchers.h>

#include "gtest/gtest.h"
#include "fff.h"
extern "C" {
#include "ctaps.h"
#include "ctaps_internal.h"
#include <connection/connection.h>
#include <logging/log.h>
#include <connection/connection_group.h>
#include <util/uuid_util.h>
}

DEFINE_FFF_GLOBALS;
FAKE_VALUE_FUNC(int, fake_protocol_close, ct_connection_t*);
FAKE_VOID_FUNC(fake_protocol_abort, ct_connection_t*);


TEST(ConnectionGroupUnitTests, CloseAllClosesOnlyOpenConnections) {
    RESET_FAKE(fake_protocol_close);
    fake_protocol_close_fake.return_val = 0;

    // Create a shared connection group
    ct_connection_group_t group;
    generate_uuid_string(group.connection_group_id);
    group.connections = g_hash_table_new(g_str_hash, g_str_equal);
    group.num_active_connections = 0;
    group.connection_group_state = NULL;

    // Connection 1: Established (should be closed)
    ct_connection_t conn1;
    memset(&conn1, 0, sizeof(ct_connection_t));
    generate_uuid_string(conn1.uuid);
    ct_connection_mark_as_established(&conn1);
    conn1.protocol.close = fake_protocol_close;
    ct_connection_group_add_connection(&group, &conn1);

    // Connection 2: Already closing (should be skipped)
    ct_connection_t conn2;
    memset(&conn2, 0, sizeof(ct_connection_t));
    generate_uuid_string(conn2.uuid);
    ct_connection_mark_as_closing(&conn2);
    conn2.protocol.close = fake_protocol_close;
    ct_connection_group_add_connection(&group, &conn2);

    // Connection 3: Established (should be closed)
    ct_connection_t conn3;
    memset(&conn3, 0, sizeof(ct_connection_t));
    generate_uuid_string(conn3.uuid);
    ct_connection_mark_as_established(&conn3);
    conn3.protocol.close = fake_protocol_close;
    ct_connection_group_add_connection(&group, &conn3);

    // Connection 4: closing
    ct_connection_t conn4;
    memset(&conn4, 0, sizeof(ct_connection_t));
    generate_uuid_string(conn4.uuid);
    ct_connection_mark_as_closed(&conn4);
    conn4.protocol.close = fake_protocol_close;
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

    // Create a shared connection group
    // Connection 1: Established (should be abortd)
    ct_connection_t* conn1 = (ct_connection_t*)malloc(sizeof(ct_connection_t));
    ct_connection_build_with_new_connection_group(conn1);
    ct_connection_mark_as_established(conn1);
    conn1->protocol.abort = fake_protocol_abort;

    ct_connection_group_t* group = conn1->connection_group;

    // Connection 2: Already closed (should be skipped)
    ct_connection_t* conn2 = create_empty_connection_with_uuid();;
    ct_connection_mark_as_closed(conn2);
    conn2->protocol.abort = fake_protocol_abort;
    ct_connection_group_add_connection(group, conn2);

    // Connection 3: Established (should be abortd)
    ct_connection_t* conn3 = create_empty_connection_with_uuid();
    ct_connection_mark_as_established(conn3);
    conn3->protocol.abort = fake_protocol_abort;
    ct_connection_group_add_connection(group, conn3);

    // Connection 4: closing, should be aborted
    ct_connection_t* conn4 = create_empty_connection_with_uuid();
    ct_connection_mark_as_closing(conn4);
    conn4->protocol.abort = fake_protocol_abort;
    ct_connection_group_add_connection(group, conn4);


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

    EXPECT_TRUE(conn1 == abortd_conn1 || conn1 == abortd_conn2 || conn1 == abortd_conn3);
    EXPECT_TRUE(conn3 == abortd_conn1 || conn3 == abortd_conn2 || conn3 == abortd_conn3);
    EXPECT_TRUE(conn4 == abortd_conn1 || conn4 == abortd_conn2 || conn4 == abortd_conn3);

    EXPECT_FALSE(abortd_conn1 == abortd_conn2);
    EXPECT_FALSE(abortd_conn2 == abortd_conn3);
    EXPECT_FALSE(abortd_conn1 == abortd_conn3);

    // Cleanup
    log_info("Freeing connection 1");
    ct_connection_free(conn1);
    log_info("Freeing connection 2");
    ct_connection_free(conn2);
    log_info("Freeing connection 3");
    ct_connection_free(conn3);
    log_info("Freeing connection 4");
    ct_connection_free(conn4);
}

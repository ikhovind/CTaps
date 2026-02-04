#include "gtest/gtest.h"
#include "fff.h"

extern "C" {
#include "ctaps.h"
#include "logging/log.h"
#include "ctaps_internal.h"
#include "connection/socket_manager/socket_manager.h"
#include <protocol/udp/udp.h>
#include "connection/connection.h"
#include <uv.h>
}

DEFINE_FFF_GLOBALS;

// Capture the close callback passed to uv_close
static uv_close_cb captured_close_cb = nullptr;
static uv_handle_t* captured_handle = nullptr;

extern "C" {
  FAKE_VOID_FUNC(faked_uv_close, uv_handle_t*, uv_close_cb);
  FAKE_VALUE_FUNC(int, faked_uv_udp_recv_stop, uv_udp_t*);

  // Mock for the user's closed callback
  FAKE_VALUE_FUNC(int, mock_closed_cb, ct_connection_t*);
  FAKE_VALUE_FUNC(int, mock_connection_error, ct_connection_t*);

  void __wrap_uv_close(uv_handle_t* handle, uv_close_cb close_cb) {
    log_debug("Mock uv_close called");
    captured_handle = handle;
    captured_close_cb = close_cb;
    faked_uv_close(handle, close_cb);
    close_cb(handle);
  }

  int __wrap_uv_udp_recv_stop(uv_udp_t* handle) {
    return faked_uv_udp_recv_stop(handle);
  }
}

class UdpCloseCallbackTest : public ::testing::Test {
protected:
  void SetUp() override {
    ct_initialize();
    ct_set_log_level(CT_LOG_DEBUG);
    RESET_FAKE(faked_uv_close);
    RESET_FAKE(faked_uv_udp_recv_stop);
    RESET_FAKE(mock_closed_cb);
    FFF_RESET_HISTORY();
    captured_close_cb = nullptr;
    captured_handle = nullptr;

    ct_local_endpoint_t* local_endpoint = ct_local_endpoint_new();

    memset(&connection, 0, sizeof(ct_connection_t));
    ct_connection_build_with_new_connection_group(&connection);
    connection.connection_group->socket_manager = ct_socket_manager_new(&udp_protocol_interface, nullptr);
    connection.local_endpoint = ct_local_endpoint_new();
    free(local_endpoint);
    connection.connection_callbacks.closed = mock_closed_cb;
    connection.connection_callbacks.connection_error = mock_connection_error;
    log_debug("Initializing first connection");
    int rc = connection.connection_group->socket_manager->protocol_impl->init(&connection, nullptr);
    ASSERT_EQ(rc, 0);

    memset(&connection2, 0, sizeof(ct_connection_t));
    ct_local_endpoint_t* local_endpoint2 = 
    connection2.local_endpoint = ct_local_endpoint_new();
    free(local_endpoint2);
    connection2.connection_callbacks.closed = mock_closed_cb;
    connection2.connection_callbacks.connection_error = mock_connection_error;
    log_debug("Initializing second connection");
    ASSERT_EQ(rc, 0);
  }

  void TearDown() override {
    ct_connection_free_content(&connection);
    ct_connection_free_content(&connection2);
    ct_close();
  }

  ct_connection_t connection;
  ct_connection_t connection2;
};

TEST_F(UdpCloseCallbackTest, closedCallbackInvokedOnConnectionClose) {
  // Act: close the UDP connection
  connection.connection_group->socket_manager->protocol_impl->close(&connection);

  // Verify uv_close was called
  ASSERT_EQ(faked_uv_close_fake.call_count, 1);
  ASSERT_NE(captured_close_cb, nullptr);
  ASSERT_EQ(mock_closed_cb_fake.call_count, 1);
  ASSERT_EQ(mock_closed_cb_fake.arg0_val, &connection);
  ASSERT_TRUE(ct_connection_is_closed(&connection));
}

TEST_F(UdpCloseCallbackTest, ConnectionErrorCallbackInvokedOnConnectionAbort) {
  // Act: abort the UDP connection
  ct_connection_abort(&connection);

  // Verify uv_close was called
  ASSERT_EQ(faked_uv_close_fake.call_count, 1);
  ASSERT_NE(captured_close_cb, nullptr);
  ASSERT_EQ(mock_closed_cb_fake.call_count, 0);
  ASSERT_EQ(mock_connection_error_fake.call_count, 1);
  ASSERT_EQ(mock_connection_error_fake.arg0_val, &connection);
}

TEST_F(UdpCloseCallbackTest, ClosedCallbackInvokedOnGroupClose) {
  // Arrange: set up a connection group with two connections
  ct_connection_group_add_connection(connection.connection_group, &connection2);
  connection2.connection_group->socket_manager->protocol_impl->init(&connection2, nullptr);

  // Act: close the UDP connection
  ct_connection_close_group(&connection2);

  // Verify uv_close was called
  ASSERT_EQ(faked_uv_close_fake.call_count, 2);
  ASSERT_NE(captured_close_cb, nullptr);
  ASSERT_EQ(mock_closed_cb_fake.call_count, 2);
  ASSERT_EQ(mock_closed_cb_fake.arg0_history[0], &connection);
  ASSERT_EQ(mock_closed_cb_fake.arg0_history[1], &connection2);
}

TEST_F(UdpCloseCallbackTest, ConnectionErrorCallbackInvokedOnGroupAbort) {
  // Arrange: set up a connection group with two connections
  ct_connection_group_add_connection(connection.connection_group, &connection2);
  connection2.connection_group->socket_manager->protocol_impl->init(&connection2, nullptr);

  // Act: close the UDP connection
  ct_connection_abort_group(&connection);

  // Verify uv_close was called
  ASSERT_EQ(faked_uv_close_fake.call_count, 2);
  ASSERT_NE(captured_close_cb, nullptr);
  ASSERT_EQ(mock_connection_error_fake.call_count, 2);
  ASSERT_EQ(mock_connection_error_fake.arg0_history[0], &connection);
  ASSERT_EQ(mock_connection_error_fake.arg0_history[1], &connection2);
}

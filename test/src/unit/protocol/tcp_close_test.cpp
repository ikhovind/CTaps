#include "gtest/gtest.h"
#include "fff.h"

extern "C" {
#include "ctaps.h"
#include "ctaps_internal.h"
#include "endpoint/remote_endpoint.h"
#include "endpoint/local_endpoint.h"
#include "connection/connection.h"
#include "connection/socket_manager/socket_manager.h"
#include <protocol/tcp/tcp.h>
#include <uv.h>
#include "logging/log.h"
#include "util/uuid_util.h"

// Declare internal function for testing
void tcp_on_read(uv_stream_t* handle, ssize_t nread, const uv_buf_t* buf);
}


DEFINE_FFF_GLOBALS;

// Capture the close callback passed to uv_close
static uv_close_cb captured_close_cb = nullptr;
static uv_handle_t* captured_handle = nullptr;

extern "C" {
  FAKE_VOID_FUNC(faked_uv_close, uv_handle_t*, uv_close_cb);
  FAKE_VOID_FUNC(faked_uv_tcp_close_reset, uv_handle_t*, uv_close_cb);

  FAKE_VALUE_FUNC(int, mock_closed_cb, ct_connection_t*);
  FAKE_VALUE_FUNC(int, mock_connection_error, ct_connection_t*);

  void __wrap_uv_close(uv_handle_t* handle, uv_close_cb close_cb) {
    log_debug("Mock uv_close called");
    captured_handle = handle;
    captured_close_cb = close_cb;
    faked_uv_close(handle, close_cb);
    close_cb(handle);
  }

  void __wrap_uv_tcp_close_reset(uv_handle_t* handle, uv_close_cb close_cb) {
    log_debug("Mock tcp_close_reset called");
    captured_handle = handle;
    captured_close_cb = close_cb;
    faked_uv_tcp_close_reset(handle, close_cb);
    close_cb(handle);
  }
}

class TcpCloseCallbackTest : public ::testing::Test {
protected:
  void SetUp() override {
    ct_initialize();
    ct_set_log_level(CT_LOG_DEBUG);
    RESET_FAKE(faked_uv_close);
    RESET_FAKE(faked_uv_tcp_close_reset);
    RESET_FAKE(mock_closed_cb);
    RESET_FAKE(mock_connection_error);
    FFF_RESET_HISTORY();
    captured_close_cb = nullptr;
    captured_handle = nullptr;

    ct_local_endpoint_t* local_endpoint = ct_local_endpoint_new();
    ct_remote_endpoint_t* remote_endpoint = ct_remote_endpoint_new();

    struct sockaddr_in sa = {
        .sin_family = AF_INET,
        .sin_port   = htons(12345),                 /* port in network byte order */
        .sin_addr   = { .s_addr = htonl(INADDR_LOOPBACK) } /* 127.0.0.1 */
    };

    ct_remote_endpoint_from_sockaddr(remote_endpoint, (struct sockaddr_storage*)&sa);

    ct_connection_callbacks_t attempt_callbacks = {
      .connection_error = mock_connection_error,
      .closed = mock_closed_cb,
    };

    connection = ct_connection_create_client(
      &tcp_protocol_interface,
      local_endpoint,
      remote_endpoint,
      NULL,
      &attempt_callbacks,
      NULL
    );

    log_debug("Conenction remote endpoint: %p", (void*)ct_connection_get_remote_endpoint(connection));
    log_debug("Connection remote endpoint resolved address: %p", (void*)remote_endpoint_get_resolved_address(ct_connection_get_remote_endpoint(connection)));

    log_debug("Initializing first connection");
    log_debug("Connection ptr: %p", (void*)connection);
    log_debug("Connection group %p", (void*)connection->connection_group);
    log_debug("Connection group's socket manager %p", (void*)connection->connection_group->socket_manager);
    log_debug("protocl impl %p", (void*)connection->connection_group->socket_manager->protocol_impl);
    connection->connection_group->socket_manager->protocol_impl->init(connection, nullptr);

    // connection2 is set up minimally - it will be added to connection's group in group tests
    connection2 = (ct_connection_t*)calloc(1, sizeof(ct_connection_t));
    generate_uuid_string(connection2->uuid);
    connection2->local_endpoint = ct_local_endpoint_new();
    connection2->connection_callbacks = attempt_callbacks;
    connection2->received_callbacks = g_queue_new();
    connection2->received_messages = g_queue_new();
    connection2->remote_endpoint = ct_remote_endpoint_deep_copy(remote_endpoint);
    log_debug("Initializing second connection");

    ct_remote_endpoint_free(remote_endpoint);
    ct_local_endpoint_free(local_endpoint);
  }

  void TearDown() override {
    ct_connection_free(connection);
    ct_connection_free(connection2);
    ct_close();
  }

  ct_connection_t* connection;
  ct_connection_t* connection2;
};

TEST_F(TcpCloseCallbackTest, ClosedCallbackInvokedOnConnectionClose) {
  // Act: close the TCP connection
  connection->connection_group->socket_manager->protocol_impl->close(connection);

  // Verify uv_close was called
  ASSERT_EQ(faked_uv_close_fake.call_count, 1);
  ASSERT_NE(captured_close_cb, nullptr);
  ASSERT_EQ(mock_closed_cb_fake.call_count, 1);
  ASSERT_EQ(mock_closed_cb_fake.arg0_val, connection);
  ASSERT_TRUE(ct_connection_is_closed(connection));
}

TEST_F(TcpCloseCallbackTest, ConnectionErrorCallbackInvokedOnConnectionAbort) {
  // Act: abort the TCP connection
  ct_connection_abort(connection);

  ASSERT_EQ(faked_uv_tcp_close_reset_fake.call_count, 1);
  ASSERT_NE(captured_close_cb, nullptr);
  ASSERT_EQ(mock_closed_cb_fake.call_count, 0);
  ASSERT_EQ(mock_connection_error_fake.call_count, 1);
  ASSERT_EQ(mock_connection_error_fake.arg0_val, connection);
}

TEST_F(TcpCloseCallbackTest, ClosedCallbackInvokedOnGroupClose) {
  // Arrange: set up a connection group with two connections
  ct_connection_group_add_connection(connection->connection_group, connection2);
  connection2->connection_group->socket_manager->protocol_impl->init(connection2, nullptr);

  // Act: close the TCP connection
  ct_connection_close_group(connection2);

  // Verify uv_close was called
  ASSERT_EQ(faked_uv_close_fake.call_count, 2);
  ASSERT_NE(captured_close_cb, nullptr);
  ASSERT_EQ(mock_closed_cb_fake.call_count, 2);
  // Check both connections received closed callback (order not guaranteed due to hash table iteration)
  bool conn1_found = (mock_closed_cb_fake.arg0_history[0] == connection ||
                      mock_closed_cb_fake.arg0_history[1] == connection);
  bool conn2_found = (mock_closed_cb_fake.arg0_history[0] == connection2 ||
                      mock_closed_cb_fake.arg0_history[1] == connection2);
  ASSERT_TRUE(conn1_found);
  ASSERT_TRUE(conn2_found);
}

TEST_F(TcpCloseCallbackTest, ConnectionErrorCallbackInvokedOnGroupAbort) {
  // Arrange: set up a connection group with two connections
  ct_connection_group_add_connection(connection->connection_group, connection2);
  connection2->connection_group->socket_manager->protocol_impl->init(connection2, nullptr);

  // Act: close the TCP connection
  ct_connection_abort_group(connection);

  ASSERT_EQ(faked_uv_tcp_close_reset_fake.call_count, 2);
  ASSERT_NE(captured_close_cb, nullptr);
  ASSERT_EQ(mock_connection_error_fake.call_count, 2);
  // Check both connections received error callback (order not guaranteed due to hash table iteration)
  bool conn1_found = (mock_connection_error_fake.arg0_history[0] == connection ||
                      mock_connection_error_fake.arg0_history[1] == connection);
  bool conn2_found = (mock_connection_error_fake.arg0_history[0] == connection2 ||
                      mock_connection_error_fake.arg0_history[1] == connection2);
  ASSERT_TRUE(conn1_found);
  ASSERT_TRUE(conn2_found);
}

TEST_F(TcpCloseCallbackTest, ConnectionErrorInvokedOnAbortByPeer) {
  // Simulate connection abort by peer
  uv_buf_t buf = {0};
  tcp_on_read(reinterpret_cast<uv_stream_t*>(connection->internal_connection_state), UV_ECONNRESET, &buf);

  ASSERT_NE(captured_close_cb, nullptr);
  ASSERT_EQ(faked_uv_close_fake.call_count, 1);
  ASSERT_EQ(mock_closed_cb_fake.call_count, 0);
  ASSERT_EQ(mock_connection_error_fake.call_count, 1);
  ASSERT_EQ(mock_connection_error_fake.arg0_val, connection);
}

TEST_F(TcpCloseCallbackTest, ConnectionClosedInvokedOnGracefulCloseByPeer) {
  // Simulate connection abort by peer
  uv_buf_t buf = {0};
  tcp_on_read(reinterpret_cast<uv_stream_t*>(connection->internal_connection_state), UV_EOF, &buf);

  ASSERT_NE(captured_close_cb, nullptr);
  ASSERT_EQ(faked_uv_close_fake.call_count, 1);
  ASSERT_EQ(mock_closed_cb_fake.call_count, 1);
  ASSERT_EQ(mock_closed_cb_fake.arg0_val, connection);
}

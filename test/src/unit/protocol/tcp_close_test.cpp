#include "gtest/gtest.h"
#include "fff.h"

extern "C" {
#include "ctaps.h"
#include "ctaps_internal.h"
#include "connection/connection.h"
#include <protocol/tcp/tcp.h>
#include <uv.h>
#include "logging/log.h"

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
    RESET_FAKE(mock_closed_cb);
    RESET_FAKE(mock_connection_error);
    RESET_FAKE(faked_uv_tcp_close_reset);
    FFF_RESET_HISTORY();
    captured_close_cb = nullptr;
    captured_handle = nullptr;

    ct_local_endpoint_t* local_endpoint = ct_local_endpoint_new();
    ct_remote_endpoint_t* remote_endpoint = ct_remote_endpoint_new();
    ct_remote_endpoint_with_ipv4(remote_endpoint, INADDR_LOOPBACK);
    ct_remote_endpoint_with_port(remote_endpoint, 8080);

    memset(&connection, 0, sizeof(ct_connection_t));
    ct_connection_build_with_new_connection_group(&connection);
    connection.protocol = tcp_protocol_interface;
    connection.local_endpoint = *local_endpoint;
    connection.remote_endpoint = *remote_endpoint;
    connection.connection_callbacks.closed = mock_closed_cb;
    connection.connection_callbacks.connection_error = mock_connection_error;
    log_debug("Initializing first connection");
    connection.protocol.init(&connection, nullptr);

    memset(&connection2, 0, sizeof(ct_connection_t));
    ct_local_endpoint_t* local_endpoint2 = ct_local_endpoint_new();
    connection2.protocol = tcp_protocol_interface;
    connection2.local_endpoint = *local_endpoint2;
    connection2.remote_endpoint = *remote_endpoint;
    connection2.connection_callbacks.closed = mock_closed_cb;
    connection2.connection_callbacks.connection_error = mock_connection_error;
    log_debug("Initializing second connection");
    connection2.protocol.init(&connection2, nullptr);
  }

  void TearDown() override {
    ct_connection_free_content(&connection);
    ct_close();
  }

  ct_connection_t connection;
  ct_connection_t connection2;
};

TEST_F(TcpCloseCallbackTest, ClosedCallbackInvokedOnConnectionClose) {
  // Act: close the TCP connection
  connection.protocol.close(&connection);

  // Verify uv_close was called
  ASSERT_EQ(faked_uv_close_fake.call_count, 1);
  ASSERT_NE(captured_close_cb, nullptr);
  ASSERT_EQ(mock_closed_cb_fake.call_count, 1);
  ASSERT_EQ(mock_closed_cb_fake.arg0_val, &connection);
  ASSERT_TRUE(ct_connection_is_closed(&connection));
}

TEST_F(TcpCloseCallbackTest, ConnectionErrorCallbackInvokedOnConnectionAbort) {
  // Act: abort the TCP connection
  ct_connection_abort(&connection);

  ASSERT_EQ(faked_uv_tcp_close_reset_fake.call_count, 1);
  ASSERT_NE(captured_close_cb, nullptr);
  ASSERT_EQ(mock_closed_cb_fake.call_count, 0);
  ASSERT_EQ(mock_connection_error_fake.call_count, 1);
  ASSERT_EQ(mock_connection_error_fake.arg0_val, &connection);
}

TEST_F(TcpCloseCallbackTest, ClosedCallbackInvokedOnGroupClose) {
  // Arrange: set up a connection group with two connections
  ct_connection_group_add_connection(connection.connection_group, &connection2);

  // Act: close the TCP connection
  ct_connection_close_group(&connection2);

  // Verify uv_close was called
  ASSERT_EQ(faked_uv_close_fake.call_count, 2);
  ASSERT_NE(captured_close_cb, nullptr);
  ASSERT_EQ(mock_closed_cb_fake.call_count, 2);
  ASSERT_EQ(mock_closed_cb_fake.arg0_history[0], &connection);
  ASSERT_EQ(mock_closed_cb_fake.arg0_history[1], &connection2);
}

TEST_F(TcpCloseCallbackTest, ConnectionErrorCallbackInvokedOnGroupAbort) {
  // Arrange: set up a connection group with two connections
  ct_connection_group_add_connection(connection.connection_group, &connection2);

  // Act: close the TCP connection
  ct_connection_abort_group(&connection);

  ASSERT_EQ(faked_uv_tcp_close_reset_fake.call_count, 2);
  ASSERT_NE(captured_close_cb, nullptr);
  ASSERT_EQ(mock_connection_error_fake.call_count, 2);
  ASSERT_EQ(mock_connection_error_fake.arg0_history[0], &connection);
  ASSERT_EQ(mock_connection_error_fake.arg0_history[1], &connection2);
}

TEST_F(TcpCloseCallbackTest, ConnectionErrorInvokedOnAbortByPeer) {
  // Simulate connection abort by peer
  uv_buf_t buf = {0};
  tcp_on_read(reinterpret_cast<uv_stream_t*>(connection.internal_connection_state), UV_ECONNRESET, &buf);

  ASSERT_NE(captured_close_cb, nullptr);
  ASSERT_EQ(faked_uv_close_fake.call_count, 1);
  ASSERT_EQ(mock_closed_cb_fake.call_count, 0);
  ASSERT_EQ(mock_connection_error_fake.call_count, 1);
  ASSERT_EQ(mock_connection_error_fake.arg0_val, &connection);
}

TEST_F(TcpCloseCallbackTest, ConnectionClosedInvokedOnGracefulCloseByPeer) {
  // Simulate connection abort by peer
  uv_buf_t buf = {0};
  tcp_on_read(reinterpret_cast<uv_stream_t*>(connection.internal_connection_state), UV_EOF, &buf);

  ASSERT_NE(captured_close_cb, nullptr);
  ASSERT_EQ(faked_uv_close_fake.call_count, 1);
  ASSERT_EQ(mock_closed_cb_fake.call_count, 1);
  ASSERT_EQ(mock_closed_cb_fake.arg0_val, &connection);
}

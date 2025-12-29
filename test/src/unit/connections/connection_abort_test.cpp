#include <gmock/gmock-matchers.h>

#include "gtest/gtest.h"
#include "fff.h"
extern "C" {
#include "ctaps.h"
#include "ctaps_internal.h"
#include <connection/connection.h>
#include <protocol/tcp/tcp.h>
#include <protocol/udp/udp.h>
#include <uv.h>
}

DEFINE_FFF_GLOBALS;

// Mock libuv functions
extern "C" {
  FAKE_VALUE_FUNC(int, faked_uv_tcp_close_reset, uv_tcp_t*, uv_close_cb);
  FAKE_VOID_FUNC(faked_uv_close, uv_handle_t*, uv_close_cb);
  FAKE_VALUE_FUNC(int, faked_uv_udp_recv_stop, uv_udp_t*);


  int __wrap_uv_tcp_close_reset(uv_tcp_t* handle, uv_close_cb close_cb) {
    return faked_uv_tcp_close_reset(handle, close_cb);
  }

  void __wrap_uv_close(uv_tcp_t* handle, uv_close_cb close_cb) {
    faked_uv_close((uv_handle_t*)handle, close_cb);
  }

  int __wrap_uv_udp_recv_stop(uv_udp_t* handle) {
    return faked_uv_udp_recv_stop(handle);
  }
}

class ConnectionAbortTest : public ::testing::Test {
protected:
  void SetUp() override {
    RESET_FAKE(faked_uv_tcp_close_reset);
    RESET_FAKE(faked_uv_close);
    RESET_FAKE(faked_uv_udp_recv_stop);
    FFF_RESET_HISTORY();

    // Initialize TCP connection
    memset(&tcp_connection, 0, sizeof(ct_connection_t));
    ct_connection_build_with_new_connection_group(&tcp_connection);
    tcp_connection.protocol = tcp_protocol_interface;
    tcp_connection.socket_type = CONNECTION_SOCKET_TYPE_STANDALONE;
    memset(&mock_tcp_handle, 0, sizeof(uv_tcp_t));
    tcp_connection.internal_connection_state = (uv_handle_t*)&mock_tcp_handle;
    ct_connection_mark_as_established(&tcp_connection);

    // Initialize UDP connection
    memset(&udp_connection, 0, sizeof(ct_connection_t));
    ct_connection_build_with_new_connection_group(&udp_connection);
    udp_connection.protocol = udp_protocol_interface;
    udp_connection.socket_type = CONNECTION_SOCKET_TYPE_STANDALONE;
    memset(&mock_udp_handle, 0, sizeof(uv_udp_t));
    udp_connection.internal_connection_state = (uv_handle_t*)&mock_udp_handle;
    ct_connection_mark_as_established(&udp_connection);
  }

  void TearDown() override {
    ct_connection_free_content(&tcp_connection);
    ct_connection_free_content(&udp_connection);
  }

  // TCP connection and mocks
  ct_connection_t tcp_connection;
  uv_tcp_t mock_tcp_handle;

  // UDP connection and mocks
  ct_connection_t udp_connection;
  uv_udp_t mock_udp_handle;
};

// ==================== TCP Tests ====================

// Test Case 1: Verify tcp_abort uses uv_tcp_close_reset (not uv_close)
TEST_F(ConnectionAbortTest, abortTcpConnectionSendsReset) {
  // Execute: abort the TCP connection
  ct_connection_abort(&tcp_connection);

  // Verify: uv_tcp_close_reset was called exactly once
  ASSERT_EQ(faked_uv_tcp_close_reset_fake.call_count, 1);
  ASSERT_EQ(faked_uv_tcp_close_reset_fake.arg0_val, &mock_tcp_handle);

  // Verify: uv_close was NOT called
  ASSERT_EQ(faked_uv_close_fake.call_count, 0);
  ASSERT_TRUE(ct_connection_is_closed(&tcp_connection));
}

// Test Case 2: Verify tcp_close uses uv_close (contrast with abort)
TEST_F(ConnectionAbortTest, tcpCloseUsesGracefulShutdown) {
  // Execute: close the TCP connection (gracefully, not abort)
  tcp_close(&tcp_connection);

  // Verify: uv_close was called exactly once
  ASSERT_EQ(faked_uv_close_fake.call_count, 1);

  // Verify: uv_tcp_close_reset was NOT called
  ASSERT_EQ(faked_uv_tcp_close_reset_fake.call_count, 0);
  ASSERT_TRUE(ct_connection_is_closed(&tcp_connection));
}

// ==================== UDP Tests ====================

// Test Case 3: Verify UDP abort stops receiving and closes handle
TEST_F(ConnectionAbortTest, abortUdpConnectionStopsRecvAndCloses) {
  // Execute: abort the UDP connection
  ct_connection_abort(&udp_connection);

  // Verify: uv_udp_recv_stop was called exactly once
  ASSERT_EQ(faked_uv_udp_recv_stop_fake.call_count, 1);
  ASSERT_EQ(faked_uv_udp_recv_stop_fake.arg0_val, &mock_udp_handle);

  // Verify: uv_close was called exactly once
  ASSERT_EQ(faked_uv_close_fake.call_count, 1);
  ASSERT_TRUE(ct_connection_is_closed(&udp_connection));
}

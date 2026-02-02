#include <unordered_set>
#include "gtest/gtest.h"
#include "fff.h"

extern "C" {
#include "connection/connection.h"
#include "ctaps.h"
#include "ctaps_internal.h"
#include "logging/log.h"
#include <picoquic.h>
#include <protocol/quic/quic.h>
#include <uv.h>

// Declare internal callback for testing
int picoquic_callback(picoquic_cnx_t* cnx,
    uint64_t stream_id, uint8_t* bytes, size_t length,
    picoquic_call_back_event_t fin_or_event, void* callback_ctx, void* v_stream_ctx);
}

DEFINE_FFF_GLOBALS;

std::unordered_set<uv_handle_t*> closed_handles;

extern "C" {
  FAKE_VALUE_FUNC(int, faked_socket_manager_remove_connection_group, struct ct_socket_manager_s*, struct sockaddr_storage*);
  FAKE_VOID_FUNC(faked_uv_close, uv_handle_t*, uv_close_cb);
  FAKE_VALUE_FUNC(int, faked_uv_udp_recv_stop, uv_udp_t*);

  FAKE_VALUE_FUNC(int, mock_closed_cb, ct_connection_t*);
  FAKE_VALUE_FUNC(int, mock_connection_error, ct_connection_t*);

  int __wrap_socket_manager_remove_connection_group(struct ct_socket_manager_s* sm, struct sockaddr_storage* addr) {
    return faked_socket_manager_remove_connection_group(sm, addr);
  }

  void __wrap_uv_close(uv_handle_t* handle, uv_close_cb close_cb) {
    log_debug("Mock uv_close called");
    log_debug("Handle being closed: %p", (void*)handle);
    closed_handles.insert(handle);
    faked_uv_close(handle, close_cb);
    close_cb(handle);
  }

  int __wrap_uv_udp_recv_stop(uv_udp_t* handle) {
    return faked_uv_udp_recv_stop(handle);
  }
}

class QuicCloseTest : public ::testing::Test {
protected:
  void SetUp() override {
    FFF_RESET_HISTORY();

    ct_initialize();
    ct_set_log_level(CT_LOG_TRACE);
    RESET_FAKE(faked_socket_manager_remove_connection_group);
    RESET_FAKE(mock_closed_cb);
    FFF_RESET_HISTORY();

    ct_security_parameters_t* security_parameters = ct_security_parameters_new();
    ASSERT_NE(security_parameters, nullptr);
    const char* alpn_strings = "simple-ping";
    ct_sec_param_set_property_string_array(security_parameters, ALPN, (const char**)&alpn_strings, 1);

    ct_certificate_bundles_t* client_bundles = ct_certificate_bundles_new();
    ct_certificate_bundles_add_cert(client_bundles, TEST_RESOURCE_DIR "/cert.pem", TEST_RESOURCE_DIR "/key.pem");
    ct_sec_param_set_property_certificate_bundles(security_parameters, CLIENT_CERTIFICATE, client_bundles);
    ct_certificate_bundles_free(client_bundles);

    ct_local_endpoint_t* local_endpoint = ct_local_endpoint_new();
    ct_remote_endpoint_t* remote_endpoint = ct_remote_endpoint_new();
    ct_remote_endpoint_with_ipv4(remote_endpoint, INADDR_LOOPBACK);
    ct_remote_endpoint_with_port(remote_endpoint, 8080);

    connection = create_empty_connection_with_uuid();
    ct_connection_build_with_new_connection_group(connection);

    connection->security_parameters = security_parameters;
    connection->protocol = quic_protocol_interface;
    connection->local_endpoint = *local_endpoint;
    connection->remote_endpoint = *remote_endpoint;
    connection->connection_callbacks.closed = mock_closed_cb;
    connection->connection_callbacks.connection_error = mock_connection_error;
    log_debug("Initializing first connection");
    int rc = connection->protocol.init(connection, nullptr);
    ASSERT_EQ(rc, 0);

    connection2 = create_empty_connection_with_uuid();
    ct_connection_build_with_new_connection_group(connection2);
    ct_local_endpoint_t* local_endpoint2 = ct_local_endpoint_new();
    connection2->security_parameters = security_parameters;
    connection2->protocol = quic_protocol_interface;
    connection2->local_endpoint = *local_endpoint2;
    connection2->remote_endpoint = *remote_endpoint;
    connection2->connection_callbacks.closed = mock_closed_cb;
    connection2->connection_callbacks.connection_error = mock_connection_error;
    log_debug("Initializing second connection");
    rc = connection2->protocol.init(connection2, nullptr);
    ASSERT_EQ(rc, 0);

    ct_connection_group_free(connection2->connection_group);
    connection2->connection_group = nullptr;
  }

  void TearDown() override {
    // Clean up - states are stack allocated so don't let them be freed
    //ct_connection_free_content(&connection);
  }

  ct_connection_t* connection;
  ct_connection_t* connection2;
};

TEST_F(QuicCloseTest, ClosedCallbackInvokedOnTransportCloseCallback) {
  // Simulate picoquic detecting remote close
  picoquic_callback(
      nullptr,
      0,
      nullptr,
      0,
      picoquic_callback_close,
      connection->connection_group,
      nullptr
  );

  // get connection ct_quic_group_state_s
  ct_quic_group_state_t* group_state = ct_connection_get_quic_group_state(connection);

  // Close timer and udp socket
  ASSERT_EQ(faked_uv_close_fake.call_count, 2);
  ASSERT_TRUE(closed_handles.count((uv_handle_t*)group_state->udp_handle) == 1);
  ASSERT_TRUE(closed_handles.count((uv_handle_t*)group_state->quic_context->timer_handle) == 1);

  ASSERT_EQ(mock_closed_cb_fake.call_count, 1);
  ASSERT_EQ(mock_closed_cb_fake.arg0_val, connection);
  ASSERT_TRUE(ct_connection_is_closed(connection));
}

TEST_F(QuicCloseTest, ClosedCallbackInvokedOnApplicationCloseCallback) {
  picoquic_callback(
      nullptr,
      0,
      nullptr,
      0,
      picoquic_callback_application_close,
      connection->connection_group,
      nullptr
  );

  // get connection ct_quic_group_state_s
  ct_quic_group_state_t* group_state = ct_connection_get_quic_group_state(connection);

  // Close timer and udp socket
  ASSERT_EQ(faked_uv_close_fake.call_count, 2);
  ASSERT_TRUE(closed_handles.find((uv_handle_t*)group_state->udp_handle) != closed_handles.end());
  ASSERT_TRUE(closed_handles.find((uv_handle_t*)group_state->quic_context->timer_handle) != closed_handles.end());

  ASSERT_EQ(mock_closed_cb_fake.call_count, 1);
  ASSERT_EQ(mock_closed_cb_fake.arg0_val, connection);
  ASSERT_TRUE(ct_connection_is_closed(connection));
}

TEST_F(QuicCloseTest, ClosedCallbackInvokedOnConnectionGroupOnTransportCloseCallback) {
  ct_connection_group_add_connection(connection->connection_group, connection2);

  // Simulate picoquic detecting remote close
  picoquic_callback(
      nullptr,
      0,
      nullptr,
      0,
      picoquic_callback_close,
      connection->connection_group,
      nullptr
  );

  // get connection ct_quic_group_state_s
  ct_quic_group_state_t* group_state = ct_connection_get_quic_group_state(connection);

  // Close timer and udp socket
  ASSERT_EQ(faked_uv_close_fake.call_count, 2);
  ASSERT_TRUE(closed_handles.count((uv_handle_t*)group_state->udp_handle) == 1);
  ASSERT_TRUE(closed_handles.count((uv_handle_t*)group_state->quic_context->timer_handle) == 1);

  ASSERT_EQ(mock_closed_cb_fake.call_count, 2);

  std::unordered_set<ct_connection_t*> closed_connections;
  closed_connections.insert(mock_closed_cb_fake.arg0_history[0]);
  closed_connections.insert(mock_closed_cb_fake.arg0_history[1]);

  ASSERT_EQ(closed_connections.count(connection), 1);
  ASSERT_EQ(closed_connections.count(connection2), 1);

  ASSERT_TRUE(ct_connection_is_closed(connection));
  ASSERT_TRUE(ct_connection_is_closed(connection2));
}

TEST_F(QuicCloseTest, ClosedCallbackInvokedOnConnectionGroupOnApplicationCloseCallback) {
  ct_connection_group_add_connection(connection->connection_group, connection2);

  // Simulate picoquic detecting remote close
  picoquic_callback(
      nullptr,
      0,
      nullptr,
      0,
      picoquic_callback_application_close,
      connection->connection_group,
      nullptr
  );

  // get connection ct_quic_group_state_s
  ct_quic_group_state_t* group_state = ct_connection_get_quic_group_state(connection);

  // Close timer and udp socket
  ASSERT_EQ(faked_uv_close_fake.call_count, 2);
  ASSERT_TRUE(closed_handles.count((uv_handle_t*)group_state->udp_handle) == 1);
  ASSERT_TRUE(closed_handles.count((uv_handle_t*)group_state->quic_context->timer_handle) == 1);

  ASSERT_EQ(mock_closed_cb_fake.call_count, 2);

  std::unordered_set<ct_connection_t*> closed_connections;
  closed_connections.insert(mock_closed_cb_fake.arg0_history[0]);
  closed_connections.insert(mock_closed_cb_fake.arg0_history[1]);

  ASSERT_EQ(closed_connections.count(connection), 1);
  ASSERT_EQ(closed_connections.count(connection2), 1);

  ASSERT_TRUE(ct_connection_is_closed(connection));
  ASSERT_TRUE(ct_connection_is_closed(connection2));
}

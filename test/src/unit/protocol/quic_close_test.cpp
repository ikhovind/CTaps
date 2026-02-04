#include <unordered_set>
#include "gtest/gtest.h"
#include "fff.h"

extern "C" {
#include "connection/connection.h"
#include "security_parameter/security_parameters.h"
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
  FAKE_VALUE_FUNC(int, faked_picoquic_close, picoquic_cnx_t*, uint64_t);
  FAKE_VOID_FUNC(faked_picoquic_close_immediate, picoquic_cnx_t*);
  FAKE_VALUE_FUNC(int, faked_picoquic_add_to_stream, picoquic_cnx_t*, uint64_t, const uint8_t*, size_t, int);
  FAKE_VALUE_FUNC(int, faked_picoquic_reset_stream, picoquic_cnx_t*, uint64_t, uint64_t);
  FAKE_VALUE_FUNC(uint64_t, faked_picoquic_get_remote_error, picoquic_cnx_t*);
  FAKE_VALUE_FUNC(uint64_t, faked_picoquic_get_application_error, picoquic_cnx_t*);

  FAKE_VALUE_FUNC(int, mock_closed_cb, ct_connection_t*);
  FAKE_VALUE_FUNC(int, mock_connection_error, ct_connection_t*);

  int __wrap_socket_manager_remove_connection_group(struct ct_socket_manager_s* sm, struct sockaddr_storage* addr) {
    return faked_socket_manager_remove_connection_group(sm, addr);
  }

  int __wrap_picoquic_close(picoquic_cnx_t* cnx, uint64_t application_reason_code) {
    faked_picoquic_close(cnx, application_reason_code);
    return 0;
  }

  void __wrap_picoquic_close_immediate(picoquic_cnx_t* cnx) {
    faked_picoquic_close_immediate(cnx);
  }

  uint64_t __wrap_picoquic_get_remote_error(picoquic_cnx_t* cnx) {
    return faked_picoquic_get_remote_error(cnx);
  }

  uint64_t __wrap_picoquic_get_application_error(picoquic_cnx_t* cnx) {
    return faked_picoquic_get_application_error(cnx);
  }


  int __wrap_picoquic_add_to_stream(picoquic_cnx_t* cnx,
    uint64_t stream_id, const uint8_t* data, size_t length, int set_fin) {
    faked_picoquic_add_to_stream(cnx, stream_id, data, length, set_fin);
    return 0;
  }

  int __wrap_picoquic_reset_stream(picoquic_cnx_t* cnx,
      uint64_t stream_id, uint64_t local_stream_error) {
    faked_picoquic_reset_stream(cnx, stream_id, local_stream_error);
    return 0;
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
    RESET_FAKE(faked_picoquic_close);
    RESET_FAKE(faked_picoquic_close_immediate);
    RESET_FAKE(faked_uv_close);
    RESET_FAKE(faked_uv_udp_recv_stop);
    RESET_FAKE(faked_picoquic_get_remote_error);
    RESET_FAKE(faked_picoquic_get_application_error);
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
    ct_quic_stream_state_t* stream_state = ct_connection_get_stream_state(connection);
    stream_state->stream_initialized = true;
    ct_connection_set_can_send(connection, true);
    stream_state->stream_id = 0;


    connection2 = create_empty_connection_with_uuid();
    ct_connection_build_with_new_connection_group(connection2);
    ct_local_endpoint_t* local_endpoint2 = ct_local_endpoint_new();
    connection2->security_parameters = ct_security_parameters_deep_copy(security_parameters);
    connection2->protocol = quic_protocol_interface;
    connection2->local_endpoint = *local_endpoint2;
    connection2->remote_endpoint = *remote_endpoint;
    connection2->connection_callbacks.closed = mock_closed_cb;
    connection2->connection_callbacks.connection_error = mock_connection_error;
    log_debug("Initializing second connection");
    rc = connection2->protocol.init(connection2, nullptr);
    ASSERT_EQ(rc, 0);
    stream_state = ct_connection_get_stream_state(connection2);
    stream_state->stream_initialized = true;
    stream_state->stream_id = 4;

    ct_connection_set_can_send(connection2, true);

    ct_connection_group_free(connection2->connection_group);
    connection2->connection_group = nullptr;

    free(local_endpoint);
    free(local_endpoint2);
    free(remote_endpoint);
  }

  void TearDown() override {
    ct_connection_free(connection);
    ct_connection_free(connection2);
  }

  ct_connection_t* connection;
  ct_connection_t* connection2;
};

TEST_F(QuicCloseTest, PicoquicRemoteClose_WithoutError_InvokesClosedCallbackOnSingleConnection) {
  faked_picoquic_get_remote_error_fake.return_val = 0;
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

TEST_F(QuicCloseTest, PicoquicRemoteClose_WithError_InvokesErrorCallbackOnSingleConnection) {
  faked_picoquic_get_remote_error_fake.return_val = 8;
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

  ASSERT_EQ(mock_connection_error_fake.call_count, 1);
  ASSERT_EQ(mock_connection_error_fake.arg0_val, connection);
  ASSERT_TRUE(ct_connection_is_closed(connection));
}

TEST_F(QuicCloseTest, PicoquicRemoteClose_WithoutError_InvokesClosedCallbackOnConnectionGroup) {
  faked_picoquic_get_remote_error_fake.return_val = 0;
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

TEST_F(QuicCloseTest, PicoquicRemoteClose_WithError_InvokesErrorCallbackOnConnectionGroup) {
  faked_picoquic_get_remote_error_fake.return_val = 2849;
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

  ASSERT_EQ(mock_connection_error_fake.call_count, 2);

  std::unordered_set<ct_connection_t*> closed_connections;
  closed_connections.insert(mock_connection_error_fake.arg0_history[0]);
  closed_connections.insert(mock_connection_error_fake.arg0_history[1]);

  ASSERT_EQ(closed_connections.count(connection), 1);
  ASSERT_EQ(closed_connections.count(connection2), 1);

  ASSERT_TRUE(ct_connection_is_closed(connection));
  ASSERT_TRUE(ct_connection_is_closed(connection2));
}


TEST_F(QuicCloseTest, PicoquicApplicationClose_WithoutError_InvokesClosedCallbackOnSingleConnection) {
  faked_picoquic_get_application_error_fake.return_val = 0;

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

TEST_F(QuicCloseTest, PicoquicApplicationClose_WithError_InvokesErrorCallbackOnSingleConnection) {
  faked_picoquic_get_application_error_fake.return_val = 1;

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

  ASSERT_EQ(mock_connection_error_fake.call_count, 1);
  ASSERT_EQ(mock_connection_error_fake.arg0_val, connection);
  ASSERT_TRUE(ct_connection_is_closed(connection));
}

TEST_F(QuicCloseTest, PicoquicApplicationClose_WithoutError_InvokesClosedCallbackOnConnectionGroup) {
  faked_picoquic_get_application_error_fake.return_val = 0;
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


TEST_F(QuicCloseTest, PicoquicApplicationClose_WithError_InvokesErrorCallbackOnConnectionGroup) {
  faked_picoquic_get_application_error_fake.return_val = 999;
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

  ASSERT_EQ(mock_connection_error_fake.call_count, 2);

  std::unordered_set<ct_connection_t*> closed_connections;
  closed_connections.insert(mock_connection_error_fake.arg0_history[0]);
  closed_connections.insert(mock_connection_error_fake.arg0_history[1]);

  ASSERT_EQ(closed_connections.count(connection), 1);
  ASSERT_EQ(closed_connections.count(connection2), 1);

  ASSERT_TRUE(ct_connection_is_closed(connection));
  ASSERT_TRUE(ct_connection_is_closed(connection2));
}

TEST_F(QuicCloseTest, StreamFinInvokedOnCanSendConnectionGroupDoesNotInvokeCloseCb) {
  ct_connection_group_add_connection(connection->connection_group, connection2);

  ct_quic_stream_state_t* stream_state = ct_connection_get_stream_state(connection2);
  // Simulate picoquic detecting remote close
  picoquic_callback(
      nullptr,
      stream_state->stream_id,
      nullptr,
      0,
      picoquic_callback_stream_fin,
      connection->connection_group,
      connection2
  );

  ASSERT_FALSE(ct_connection_can_receive(connection2));
  ASSERT_TRUE(ct_connection_can_send(connection2));
  ASSERT_EQ(faked_uv_close_fake.call_count, 0);
  ASSERT_EQ(ct_connection_group_get_num_active_connections(connection->connection_group), 2);
}

TEST_F(QuicCloseTest, StreamFinInvokedOnCantSendConnectionGroupOnDoesInvokeCloseCb) {
  ct_connection_group_add_connection(connection->connection_group, connection2);
  ct_connection_set_can_send(connection2, false);

  ct_quic_stream_state_t* stream_state = ct_connection_get_stream_state(connection2);

  picoquic_callback(
      nullptr,
      stream_state->stream_id,
      nullptr,
      0,
      picoquic_callback_stream_fin,
      connection->connection_group,
      connection2
  );

  // get connection ct_quic_group_state_s
  ct_quic_group_state_t* group_state = ct_connection_get_quic_group_state(connection);

  ASSERT_FALSE(ct_connection_can_receive(connection2));
  ASSERT_FALSE(ct_connection_can_send(connection2));
  ASSERT_EQ(mock_closed_cb_fake.call_count, 1);
  ASSERT_EQ(mock_closed_cb_fake.arg0_val, connection2);
  ASSERT_EQ(ct_connection_group_get_num_active_connections(connection->connection_group), 1);
}

TEST_F(QuicCloseTest, PicoquicStreamReset_ClosesAndInvokesErrorCb) {
  ct_connection_group_add_connection(connection->connection_group, connection2);

  ct_quic_stream_state_t* stream_state = ct_connection_get_stream_state(connection2);

  picoquic_callback(
      nullptr,
      stream_state->stream_id,
      nullptr,
      0,
      picoquic_callback_stream_reset,
      connection->connection_group,
      connection2
  );

  // get connection ct_quic_group_state_s
  ct_quic_group_state_t* group_state = ct_connection_get_quic_group_state(connection);

  ASSERT_TRUE(ct_connection_is_closed(connection2));
  ASSERT_EQ(mock_connection_error_fake.call_count, 1);
  ASSERT_EQ(mock_connection_error_fake.arg0_val, connection2);
  ASSERT_EQ(ct_connection_group_get_num_active_connections(connection->connection_group), 1);
}

TEST_F(QuicCloseTest, CloseCallsPicoquicCloseForConnection) {
  connection->protocol.close(connection);

  ASSERT_EQ(faked_picoquic_close_fake.call_count, 1);
  ASSERT_EQ(ct_connection_group_get_num_active_connections(connection->connection_group), 0);
}

TEST_F(QuicCloseTest, AbortCallsPicoquicCloseImmediateForLastConnection) {
  connection->protocol.abort(connection);

  ASSERT_EQ(faked_picoquic_close_immediate_fake.call_count, 1);
  ASSERT_EQ(ct_connection_group_get_num_active_connections(connection->connection_group), 0);
}

TEST_F(QuicCloseTest, CloseCallsPicoquicAddToStreamForConnectionGroup) {
  ct_connection_group_add_connection(connection->connection_group, connection2);

  connection->protocol.close(connection);

  ct_quic_group_state_t* group_state = ct_connection_get_quic_group_state(connection);

  ASSERT_EQ(faked_picoquic_add_to_stream_fake.call_count, 1);
  ASSERT_EQ(ct_connection_group_get_num_active_connections(connection->connection_group), 1);
  ASSERT_EQ(faked_picoquic_add_to_stream_fake.arg0_val, group_state->picoquic_connection);
  ASSERT_EQ(faked_picoquic_add_to_stream_fake.arg4_val, 1); // FIN set
}

TEST_F(QuicCloseTest, AbortCallsPicoquicResetStreamForConnectionGroup) {
  ct_connection_group_add_connection(connection->connection_group, connection2);

  connection->protocol.abort(connection);

  ct_quic_group_state_t* group_state = ct_connection_get_quic_group_state(connection);

  ASSERT_EQ(faked_picoquic_reset_stream_fake.call_count, 1);
  ASSERT_EQ(ct_connection_group_get_num_active_connections(connection->connection_group), 1);
  ASSERT_EQ(faked_picoquic_reset_stream_fake.arg0_val, group_state->picoquic_connection);
}

TEST_F(QuicCloseTest, StatelessResetInvokesErrorCb) {
  picoquic_callback(
      nullptr,
      0,
      nullptr,
      0,
      picoquic_callback_stateless_reset,
      connection->connection_group,
      nullptr
  );
  ASSERT_EQ(mock_connection_error_fake.call_count, 1);
  ASSERT_EQ(mock_connection_error_fake.arg0_val, connection);

  ct_quic_group_state_t* group_state = ct_connection_get_quic_group_state(connection);
  ASSERT_EQ(mock_connection_error_fake.call_count, 1);
  ASSERT_EQ(mock_connection_error_fake.arg0_val, connection);
  ASSERT_EQ(faked_uv_close_fake.call_count, 2);  // Timer + UDP
  ASSERT_TRUE(ct_connection_is_closed(connection));
}

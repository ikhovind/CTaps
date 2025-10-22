#include <gmock/gmock-matchers.h>

#include "gtest/gtest.h"
#include "fff.h"
extern "C" {
#include "connections/preconnection/preconnection.h"
#include "ctaps.h"
#include "endpoints/remote/remote_endpoint.h"
#include "transport_properties/transport_properties.h"
#include "util/util.h"
#include "fixtures/awaiting_fixture.cpp"
#include <logging/log.h>
}

#include <mutex>
#include <condition_variable>

#define QUIC_PING_PORT 4433

extern "C" {
  int quic_mark_connection_as_success_and_close(struct Connection* connection, void* udata) {
    log_info("Connection is ready");
    // close the connection
    bool* quic_connection_succeeded = (bool*)udata;
    *quic_connection_succeeded = true;
    connection_close(connection);
    return 0;
  }

  int send_message_on_connection_ready(struct Connection* connection, void* udata) {
    log_info("Connection is ready, sending message");
    // --- Action ---
    Message message;

    message_build_with_content(&message, "hello world", strlen("hello world") + 1);
    int rc = send_message(connection, &message);
    EXPECT_EQ(rc, 0);

    message_free_content(&message);

    return 0;
  }

  int quic_establishment_error(struct Connection* connection, void* udata) {
    log_error("Connection error occurred");
    bool* quic_connection_succeeded = (bool*)udata;
    *quic_connection_succeeded = false;
    return 0;
  }

  int on_msg_received(struct Connection* connection, Message** received_message, MessageContext* ctx, void* user_data) {
    log_info("Message received");
    // set user data to received message
    Message** output_addr = (Message**)user_data;
    *output_addr = *received_message;

    connection_close(connection);
    return 0;
  }
}

TEST(QuicGenericTests, successfullyConnectsToQuicServer) {
  // --- Setup ---
  ctaps_initialize();
  RemoteEndpoint remote_endpoint;
  remote_endpoint_build(&remote_endpoint);
  remote_endpoint_with_ipv4(&remote_endpoint, inet_addr("127.0.0.1"));
  remote_endpoint_with_port(&remote_endpoint, QUIC_PING_PORT);

  TransportProperties transport_properties;

  transport_properties_build(&transport_properties);

  tp_set_sel_prop_preference(&transport_properties, RELIABILITY, REQUIRE);
  tp_set_sel_prop_preference(&transport_properties, MULTISTREAMING, REQUIRE); // force QUIC

  Preconnection preconnection;
  preconnection_build(&preconnection, transport_properties, &remote_endpoint, 1);
  Connection connection;

  bool quic_connection_succeeded = false;
  ConnectionCallbacks connection_callbacks = {
    .establishment_error = quic_establishment_error,
    .ready = quic_mark_connection_as_success_and_close,
    .user_data = &quic_connection_succeeded,
  };

  int rc = preconnection_initiate(&preconnection, &connection, connection_callbacks);

  ASSERT_EQ(rc, 0);

  ctaps_start_event_loop();

  ASSERT_TRUE(quic_connection_succeeded);
}

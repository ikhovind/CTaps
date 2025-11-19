#include <gmock/gmock-matchers.h>

#include "gtest/gtest.h"
#include "fff.h"
extern "C" {
#include "ctaps.h"
#include "util/util.h"
#include <logging/log.h>
}
#include "fixtures/awaiting_fixture.cpp"

class QuicListenTests : public CTapsGenericFixture {};

TEST_F(QuicListenTests, QuicReceivesConnectionFromListenerAndExchangesMessages) {
    CallbackContext callback_context = {
        .messages = &received_messages,
        .server_connections = received_connections,
        .client_connections = client_connections,
    };

    ct_listener_t listener;
    ct_connection_t client_connection;

    ct_local_endpoint_t listener_endpoint;
    ct_local_endpoint_build(&listener_endpoint);

    ct_local_endpoint_with_interface(&listener_endpoint, "lo");
    ct_local_endpoint_with_port(&listener_endpoint, 1239);

    ct_remote_endpoint_t listener_remote;
    ct_remote_endpoint_build(&listener_remote);
    ct_remote_endpoint_with_hostname(&listener_remote, "127.0.0.1");

    ct_transport_properties_t listener_props;
    ct_transport_properties_build(&listener_props);
    ct_tp_set_sel_prop_preference(&listener_props, RELIABILITY, REQUIRE);
    ct_tp_set_sel_prop_preference(&listener_props, MULTISTREAMING, REQUIRE); // force QUIC

    ct_security_parameters_t server_security_parameters;
    ct_security_parameters_build(&server_security_parameters);
    char* alpn_strings = "simple-ping";
    ct_sec_param_set_property_string_array(&server_security_parameters, ALPN, &alpn_strings, 1);

    ct_preconnection_t listener_precon;
    ct_preconnection_build_with_local(&listener_precon, listener_props, &listener_remote, 1, &server_security_parameters, listener_endpoint);

    ct_listener_callbacks_t listener_callbacks = {
        .connection_received = receive_message_respond_and_close_listener_on_connection_received,
        .user_listener_context = &callback_context
    };

    int listen_res = ct_preconnection_listen(&listener_precon, &listener, listener_callbacks);

    ASSERT_EQ(listen_res, 0);

    // --- SETUP CLIENT ---
    ct_remote_endpoint_t client_remote;
    ct_remote_endpoint_build(&client_remote);
    ct_remote_endpoint_with_hostname(&client_remote, "127.0.0.1");
    ct_remote_endpoint_with_port(&client_remote, 1239);

    ct_transport_properties_t client_props;
    ct_transport_properties_build(&client_props);

    ct_tp_set_sel_prop_preference(&client_props, RELIABILITY, REQUIRE);
    ct_tp_set_sel_prop_preference(&client_props, MULTISTREAMING, REQUIRE);

    ct_security_parameters_t client_security_parameters;
    ct_security_parameters_build(&client_security_parameters);
    ct_sec_param_set_property_string_array(&client_security_parameters, ALPN, &alpn_strings, 1);


    ct_preconnection_t client_precon;
    ct_preconnection_build(&client_precon, client_props, &client_remote, 1, &client_security_parameters);

    ct_connection_callbacks_t client_callbacks {
        .ready = send_message_and_receive,
        .user_connection_context = &callback_context
    };

    ct_preconnection_initiate(&client_precon, &client_connection, client_callbacks);
  /*

    // --- RUN EVENT LOOP ---
    // This will block until the callbacks close the handles
    */
    ct_start_event_loop();

    // --- ASSERTIONS ---
    ASSERT_EQ(callback_context.messages->size(), 2);
    ASSERT_EQ(callback_context.messages->at(0)->length, 5);
    ASSERT_STREQ(callback_context.messages->at(0)->content, "ping");
    ASSERT_EQ(callback_context.messages->at(1)->length, 5);
    ASSERT_STREQ(callback_context.messages->at(1)->content, "pong");

    ct_free_security_parameter_content(&server_security_parameters);
    ct_free_security_parameter_content(&client_security_parameters);
    ct_preconnection_free(&client_precon);
    ct_preconnection_free(&listener_precon);
    ct_free_remote_endpoint_strings(&client_remote);
    ct_free_remote_endpoint_strings(&listener_remote);
    ct_close();
}

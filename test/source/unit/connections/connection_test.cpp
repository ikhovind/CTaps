#include <gmock/gmock-matchers.h>

#include "gtest/gtest.h"
#include "fff.h"
extern "C" {
#include "util/util.h"
#include "state/ctaps_state.h"
#include "endpoints/local/local_endpoint.h"
#include "transport_properties/transport_properties.h"
#include <connections/listener/socket_manager/socket_manager.h>
}

TEST(ConnectionUnitTests, TakesDeepCopyOfTransportProperties) {
    ct_remote_endpoint_t remote_endpoint;
    ct_remote_endpoint_build(&remote_endpoint);

    ct_remote_endpoint_with_ipv4(&remote_endpoint, inet_addr("127.0.0.1"));
    ct_remote_endpoint_with_port(&remote_endpoint, 5005);

    ct_transport_properties_t transport_properties;

    ct_transport_properties_build(&transport_properties);
    ct_tp_set_sel_prop_preference(&transport_properties, RELIABILITY, PROHIBIT);
    ct_tp_set_sel_prop_preference(&transport_properties, PRESERVE_ORDER, PROHIBIT);

    ct_connection_t connection;
    ct_local_endpoint_t local_endpoint;
    ct_socket_manager_t socket_manager = {
        .protocol_state = nullptr,
        .protocol_impl = nullptr
    };

    ct_listener_t mock_listener = {
        .transport_properties = transport_properties,
        .local_endpoint = local_endpoint,
        .socket_manager = &socket_manager,
    };

    ct_connection_build_multiplexed(&connection, &mock_listener,  &remote_endpoint);

    ASSERT_EQ(connection.transport_properties.selection_properties.selection_property[RELIABILITY].value.simple_preference, PROHIBIT);
    ASSERT_EQ(mock_listener.transport_properties.selection_properties.selection_property[RELIABILITY].value.simple_preference, PROHIBIT);

    ct_tp_set_sel_prop_preference(&connection.transport_properties, RELIABILITY, REQUIRE);

    ASSERT_EQ(connection.transport_properties.selection_properties.selection_property[RELIABILITY].value.simple_preference, REQUIRE);
    ASSERT_EQ(mock_listener.transport_properties.selection_properties.selection_property[RELIABILITY].value.simple_preference, PROHIBIT);
}

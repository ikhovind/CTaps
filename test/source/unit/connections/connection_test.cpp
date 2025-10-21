#include <gmock/gmock-matchers.h>

#include "gtest/gtest.h"
#include "fff.h"
extern "C" {
#include "util/util.h"
#include "ctaps.h"
#include "endpoints/local/local_endpoint.h"
#include "transport_properties/transport_properties.h"
#include <connections/listener/socket_manager/socket_manager.h>
}

TEST(ConnectionUnitTests, TakesDeepCopyOfTransportProperties) {
    RemoteEndpoint remote_endpoint;
    remote_endpoint_build(&remote_endpoint);

    remote_endpoint_with_ipv4(&remote_endpoint, inet_addr("127.0.0.1"));
    remote_endpoint_with_port(&remote_endpoint, 5005);

    TransportProperties transport_properties;

    transport_properties_build(&transport_properties);
    tp_set_sel_prop_preference(&transport_properties, RELIABILITY, PROHIBIT);

    Connection connection;
    LocalEndpoint local_endpoint;
    SocketManager socket_manager = {
        .protocol_uv_handle = nullptr,
        .protocol_impl = nullptr
    };

    Listener mock_listener = {
        .transport_properties = transport_properties,
        .local_endpoint = local_endpoint,
        .socket_manager = &socket_manager,
    };

    connection_build_multiplexed(&connection, &mock_listener,  &remote_endpoint);

    ASSERT_EQ(connection.transport_properties.selection_properties.selection_property[RELIABILITY].value.simple_preference, PROHIBIT);
    ASSERT_EQ(mock_listener.transport_properties.selection_properties.selection_property[RELIABILITY].value.simple_preference, PROHIBIT);

    tp_set_sel_prop_preference(&connection.transport_properties, RELIABILITY, REQUIRE);

    ASSERT_EQ(connection.transport_properties.selection_properties.selection_property[RELIABILITY].value.simple_preference, REQUIRE);
    ASSERT_EQ(mock_listener.transport_properties.selection_properties.selection_property[RELIABILITY].value.simple_preference, PROHIBIT);
}

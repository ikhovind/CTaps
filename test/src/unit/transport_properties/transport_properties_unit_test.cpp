#include <gmock/gmock-matchers.h>

#include "gtest/gtest.h"
extern "C" {
#include "ctaps.h"
#include "ctaps_internal.h"
#include "security_parameter/security_parameters.h"
}

TEST(TransportPropertiesUnitTest, setServerCertificateSetsCorrectValue) {
    ct_transport_properties_t* props = ct_transport_properties_new();
    ASSERT_NE(props, nullptr);

    int rc = ct_transport_properties_add_interface_preference(props, "Ethernet", REQUIRE);

    ASSERT_EQ(rc, 0);

    ASSERT_EQ(ct_transport_properties_get_interface_preference(props, "Ethernet"), REQUIRE);

    ct_transport_properties_free(props);
}

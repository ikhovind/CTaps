#include <gmock/gmock-matchers.h>

#include "gtest/gtest.h"
#include "fff.h"
extern "C" {
#include "ctaps.h"
#include "ctaps_internal.h"  // Needed to access selection_properties internals
}

TEST(SelectionPropertiesUnitTest, setsAdvertisesAltAddrCorrectly) {
  ct_transport_properties_t* props = ct_transport_properties_new();
  ASSERT_NE(props, nullptr);
  ASSERT_EQ(ct_transport_properties_get_advertises_alt_address(props), false);

  ct_transport_properties_set_advertises_alt_address(props, true);

  ASSERT_EQ(ct_transport_properties_get_advertises_alt_address(props), true);
  ASSERT_TRUE(props->selection_properties.list[ADVERTISES_ALT_ADDRESS].set_by_user);
  ct_transport_properties_free(props);
}

TEST(SelectionPropertiesUnitTest, setsDefaultValues) {
  // 1. Setup
  ct_transport_properties_t* props = ct_transport_properties_new();
  ASSERT_NE(props, nullptr);
  // Allocated with ct_transport_properties_new()

  for (int i = 0; i < SELECTION_PROPERTY_END; i++) {
    const ct_selection_property_t& current_prop = props->selection_properties.list[i];

    EXPECT_EQ(current_prop.set_by_user, false);
    switch (i) {
      case RELIABILITY:
        EXPECT_STREQ(current_prop.name, "reliability");
        EXPECT_EQ(current_prop.type, TYPE_PREFERENCE);
        EXPECT_EQ(current_prop.value.simple_preference, REQUIRE);
        break;
      case PRESERVE_MSG_BOUNDARIES:
        EXPECT_STREQ(current_prop.name, "preserveMsgBoundaries");
        EXPECT_EQ(current_prop.type, TYPE_PREFERENCE);
        EXPECT_EQ(current_prop.value.simple_preference, NO_PREFERENCE);
        break;
      case PER_MSG_RELIABILITY:
        EXPECT_STREQ(current_prop.name, "perMsgReliability");
        EXPECT_EQ(current_prop.type, TYPE_PREFERENCE);
        EXPECT_EQ(current_prop.value.simple_preference, NO_PREFERENCE);
        break;
      case PRESERVE_ORDER:
        EXPECT_STREQ(current_prop.name, "preserveOrder");
        EXPECT_EQ(current_prop.type, TYPE_PREFERENCE);
        EXPECT_EQ(current_prop.value.simple_preference, REQUIRE);
        break;
      case ZERO_RTT_MSG:
        EXPECT_STREQ(current_prop.name, "zeroRttMsg");
        EXPECT_EQ(current_prop.type, TYPE_PREFERENCE);
        EXPECT_EQ(current_prop.value.simple_preference, NO_PREFERENCE);
        break;
      case MULTISTREAMING:
        EXPECT_STREQ(current_prop.name, "multistreaming");
        EXPECT_EQ(current_prop.type, TYPE_PREFERENCE);
        EXPECT_EQ(current_prop.value.simple_preference, PREFER);
        break;
      case FULL_CHECKSUM_SEND:
        EXPECT_STREQ(current_prop.name, "fullChecksumSend");
        EXPECT_EQ(current_prop.type, TYPE_PREFERENCE);
        EXPECT_EQ(current_prop.value.simple_preference, REQUIRE);
        break;
      case FULL_CHECKSUM_RECV:
        EXPECT_STREQ(current_prop.name, "fullChecksumRecv");
        EXPECT_EQ(current_prop.type, TYPE_PREFERENCE);
        EXPECT_EQ(current_prop.value.simple_preference, REQUIRE);
        break;
      case CONGESTION_CONTROL:
        EXPECT_STREQ(current_prop.name, "congestionControl");
        EXPECT_EQ(current_prop.type, TYPE_PREFERENCE);
        EXPECT_EQ(current_prop.value.simple_preference, REQUIRE);
        break;
      case KEEP_ALIVE:
        EXPECT_STREQ(current_prop.name, "keepAlive");
        EXPECT_EQ(current_prop.type, TYPE_PREFERENCE);
        EXPECT_EQ(current_prop.value.simple_preference, NO_PREFERENCE);
        break;
      case INTERFACE:
        /*
        EXPECT_STREQ(current_prop.name, "interface");
        EXPECT_EQ(current_prop.type, TYPE_PREFERENCE_SET);
        // Check the default for the override case
        EXPECT_EQ(current_prop.value.preference_set.count, 0);
        EXPECT_EQ(current_prop.value., nullptr);
        */
        break;
      case PVD:
        EXPECT_STREQ(current_prop.name, "pvd");
        EXPECT_EQ(current_prop.type, TYPE_PREFERENCE_SET);
        EXPECT_EQ(current_prop.value.simple_preference, 0);
        break;
      case USE_TEMPORARY_LOCAL_ADDRESS:
        EXPECT_STREQ(current_prop.name, "useTemporaryLocalAddress");
        EXPECT_EQ(current_prop.type, TYPE_PREFERENCE);
        EXPECT_EQ(current_prop.value.simple_preference, 0);
        break;
      case MULTIPATH:
        EXPECT_STREQ(current_prop.name, "multipath");
        EXPECT_EQ(current_prop.type, TYPE_ENUM);
        EXPECT_EQ(current_prop.value.enum_val, 0);
        break;
      case ADVERTISES_ALT_ADDRESS:
        EXPECT_STREQ(current_prop.name, "advertisesAltAddr");
        EXPECT_EQ(current_prop.type, TYPE_BOOL);
        EXPECT_EQ(current_prop.value.bool_val, false);
        break;
      case DIRECTION:
        EXPECT_STREQ(current_prop.name, "direction");
        EXPECT_EQ(current_prop.type, TYPE_ENUM);
        EXPECT_EQ(current_prop.value.enum_val, CT_DIRECTION_BIDIRECTIONAL);
        break;
      case SOFT_ERROR_NOTIFY:
        EXPECT_STREQ(current_prop.name, "softErrorNotify");
        EXPECT_EQ(current_prop.type, TYPE_PREFERENCE);
        EXPECT_EQ(current_prop.value.simple_preference, NO_PREFERENCE);
        break;
      case ACTIVE_READ_BEFORE_SEND:
        EXPECT_STREQ(current_prop.name, "activeReadBeforeSend");
        EXPECT_EQ(current_prop.type, TYPE_PREFERENCE);
        EXPECT_EQ(current_prop.value.simple_preference, NO_PREFERENCE);
        break;
      default:
        // Fail the test if we hit a property that isn't checked
        FAIL() << "Unhandled selection property in test: " << i;
        break;
    }
  }
  ct_transport_properties_free(props);
}

TEST(SelectionPropertiesUnitTest, setsSetByUser) {
  // 1. Setup
  ct_transport_properties_t* props = ct_transport_properties_new();
  ASSERT_NE(props, nullptr);
  // Allocated with ct_transport_properties_new()

  ct_transport_properties_set_direction(props, CT_DIRECTION_UNIDIRECTIONAL_SEND);

  for (int i = 0; i < SELECTION_PROPERTY_END; i++) {
    const ct_selection_property_t& current_prop = props->selection_properties.list[i];

    if (i != DIRECTION) {
      EXPECT_EQ(current_prop.set_by_user, false);
    }
    switch (i) {
      case RELIABILITY:
        EXPECT_STREQ(current_prop.name, "reliability");
        EXPECT_EQ(current_prop.type, TYPE_PREFERENCE);
        EXPECT_EQ(current_prop.value.simple_preference, REQUIRE);
        break;
      case PRESERVE_MSG_BOUNDARIES:
        EXPECT_STREQ(current_prop.name, "preserveMsgBoundaries");
        EXPECT_EQ(current_prop.type, TYPE_PREFERENCE);
        EXPECT_EQ(current_prop.value.simple_preference, NO_PREFERENCE);
        break;
      case PER_MSG_RELIABILITY:
        EXPECT_STREQ(current_prop.name, "perMsgReliability");
        EXPECT_EQ(current_prop.type, TYPE_PREFERENCE);
        EXPECT_EQ(current_prop.value.simple_preference, NO_PREFERENCE);
        break;
      case PRESERVE_ORDER:
        EXPECT_STREQ(current_prop.name, "preserveOrder");
        EXPECT_EQ(current_prop.type, TYPE_PREFERENCE);
        EXPECT_EQ(current_prop.value.simple_preference, REQUIRE);
        break;
      case ZERO_RTT_MSG:
        EXPECT_STREQ(current_prop.name, "zeroRttMsg");
        EXPECT_EQ(current_prop.type, TYPE_PREFERENCE);
        EXPECT_EQ(current_prop.value.simple_preference, NO_PREFERENCE);
        break;
      case MULTISTREAMING:
        EXPECT_STREQ(current_prop.name, "multistreaming");
        EXPECT_EQ(current_prop.type, TYPE_PREFERENCE);
        EXPECT_EQ(current_prop.value.simple_preference, PREFER);
        break;
      case FULL_CHECKSUM_SEND:
        EXPECT_STREQ(current_prop.name, "fullChecksumSend");
        EXPECT_EQ(current_prop.type, TYPE_PREFERENCE);
        EXPECT_EQ(current_prop.value.simple_preference, REQUIRE);
        break;
      case FULL_CHECKSUM_RECV:
        EXPECT_STREQ(current_prop.name, "fullChecksumRecv");
        EXPECT_EQ(current_prop.type, TYPE_PREFERENCE);
        EXPECT_EQ(current_prop.value.simple_preference, REQUIRE);
        break;
      case CONGESTION_CONTROL:
        EXPECT_STREQ(current_prop.name, "congestionControl");
        EXPECT_EQ(current_prop.type, TYPE_PREFERENCE);
        EXPECT_EQ(current_prop.value.simple_preference, REQUIRE);
        break;
      case KEEP_ALIVE:
        EXPECT_STREQ(current_prop.name, "keepAlive");
        EXPECT_EQ(current_prop.type, TYPE_PREFERENCE);
        EXPECT_EQ(current_prop.value.simple_preference, NO_PREFERENCE);
        break;
      case INTERFACE:
        /*
        EXPECT_STREQ(current_prop.name, "interface");
        EXPECT_EQ(current_prop.type, TYPE_PREFERENCE_SET);
        EXPECT_EQ(current_prop.value.preference_set.count, 0);
        EXPECT_EQ(current_prop.value.preference_set.preferences, nullptr);
        */
        break;
      case PVD:
        EXPECT_STREQ(current_prop.name, "pvd");
        EXPECT_EQ(current_prop.type, TYPE_PREFERENCE_SET);
        EXPECT_EQ(current_prop.value.simple_preference, 0);
        break;
      case USE_TEMPORARY_LOCAL_ADDRESS:
        EXPECT_STREQ(current_prop.name, "useTemporaryLocalAddress");
        EXPECT_EQ(current_prop.type, TYPE_PREFERENCE);
        EXPECT_EQ(current_prop.value.simple_preference, 0);
        break;
      case MULTIPATH:
        EXPECT_STREQ(current_prop.name, "multipath");
        EXPECT_EQ(current_prop.type, TYPE_ENUM);
        EXPECT_EQ(current_prop.value.enum_val, 0);
        break;
      case ADVERTISES_ALT_ADDRESS:
        EXPECT_STREQ(current_prop.name, "advertisesAltAddr");
        EXPECT_EQ(current_prop.type, TYPE_BOOL);
        EXPECT_EQ(current_prop.value.bool_val, false);
        break;
      case DIRECTION:
        EXPECT_EQ(current_prop.set_by_user, true);
        EXPECT_STREQ(current_prop.name, "direction");
        EXPECT_EQ(current_prop.type, TYPE_ENUM);
        EXPECT_EQ(current_prop.value.enum_val, CT_DIRECTION_UNIDIRECTIONAL_SEND);
        break;
      case SOFT_ERROR_NOTIFY:
        EXPECT_STREQ(current_prop.name, "softErrorNotify");
        EXPECT_EQ(current_prop.type, TYPE_PREFERENCE);
        EXPECT_EQ(current_prop.value.simple_preference, NO_PREFERENCE);
        break;
      case ACTIVE_READ_BEFORE_SEND:
        EXPECT_STREQ(current_prop.name, "activeReadBeforeSend");
        EXPECT_EQ(current_prop.type, TYPE_PREFERENCE);
        EXPECT_EQ(current_prop.value.simple_preference, NO_PREFERENCE);
        break;
      default:
        // Fail the test if we hit a property that isn't checked
        FAIL() << "Unhandled selection property in test: " << i;
        break;
    }
  }
  ct_transport_properties_free(props);
}

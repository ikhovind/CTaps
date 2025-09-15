#include <gmock/gmock-matchers.h>

#include "gtest/gtest.h"
#include "fff.h"
extern "C" {
#include "transport_properties/transport_properties.h"

DEFINE_FFF_GLOBALS;
FAKE_VALUE_FUNC(const ProtocolImplementation**, get_supported_protocols);
}

TEST(TransportPropertiesUnitTest, TestGetCandidateStacks) {

  ProtocolImplementation proto1 = {
    .name = "p1", // Don't forget to initialize other members
    .selection_properties = DEFAULT_SELECTION_PROPERTIES
  };
  proto1.selection_properties.selection_property[RELIABILITY].value.simple_preference = NO_PREFERENCE;
  proto1.selection_properties.selection_property[CONGESTION_CONTROL].value.simple_preference = PROHIBIT;

  ProtocolImplementation proto2 = {
    .name = "p2", // Don't forget to initialize other members
    .selection_properties = DEFAULT_SELECTION_PROPERTIES
  };
  proto2.selection_properties.selection_property[RELIABILITY].value.simple_preference = REQUIRE;
  proto2.selection_properties.selection_property[CONGESTION_CONTROL].value.simple_preference = NO_PREFERENCE;

  ProtocolImplementation proto3 = {
    .name = "p3", // Don't forget to initialize other members
    .selection_properties = DEFAULT_SELECTION_PROPERTIES
  };
  proto3.selection_properties.selection_property[RELIABILITY].value.simple_preference = PROHIBIT;
  proto3.selection_properties.selection_property[CONGESTION_CONTROL].value.simple_preference = NO_PREFERENCE;

  // 2. Initialize your array of pointers with the addresses (&) of the objects.
  //    The array is often terminated with a nullptr for easier iteration.
  const ProtocolImplementation* dummy_protoco_stacks[] = {
    &proto1,
    &proto2,
    &proto3,
    nullptr
  };

  get_supported_protocols_fake.return_val = dummy_protoco_stacks;

  ProtocolImplementation* returned_protocols[256];

  TransportProperties props;
  transport_properties_build(&props);

  tp_set_sel_prop_preference(&props, RELIABILITY, PROHIBIT);
  tp_set_sel_prop_preference(&props, CONGESTION_CONTROL, PREFER);

  int num_found = 0;

  transport_properties_get_candidate_stacks(&props.selection_properties, returned_protocols, &num_found);

  // Only return the ones which actually fit all our preferences
  EXPECT_EQ(num_found, 1);
  EXPECT_STREQ(returned_protocols[0]->name, "p3");
}

TEST(TransportPropertiesUnitTest, GetsCandidateStacks_EvenWithoutPerfectPreferenceFit) {

  ProtocolImplementation proto1 = {
    .name = "p1", // Don't forget to initialize other members
    .selection_properties = DEFAULT_SELECTION_PROPERTIES
  };
  proto1.selection_properties.selection_property[RELIABILITY].value.simple_preference = NO_PREFERENCE;
  proto1.selection_properties.selection_property[CONGESTION_CONTROL].value.simple_preference = PROHIBIT;
  proto1.selection_properties.selection_property[PRESERVE_ORDER].value.simple_preference = PROHIBIT;

  ProtocolImplementation proto2 = {
    .name = "p2", // Don't forget to initialize other members
    .selection_properties = DEFAULT_SELECTION_PROPERTIES
  };
  proto2.selection_properties.selection_property[RELIABILITY].value.simple_preference = REQUIRE;
  proto2.selection_properties.selection_property[CONGESTION_CONTROL].value.simple_preference = NO_PREFERENCE;

  ProtocolImplementation proto3 = {
    .name = "p3", // Don't forget to initialize other members
    .selection_properties = DEFAULT_SELECTION_PROPERTIES
  };
  proto3.selection_properties.selection_property[RELIABILITY].value.simple_preference = PROHIBIT;
  proto3.selection_properties.selection_property[CONGESTION_CONTROL].value.simple_preference = NO_PREFERENCE;
  proto3.selection_properties.selection_property[PRESERVE_ORDER].value.simple_preference = REQUIRE;


  // 2. Initialize your array of pointers with the addresses (&) of the objects.
  //    The array is often terminated with a nullptr for easier iteration.
  const ProtocolImplementation* dummy_protoco_stacks[] = {
    &proto1,
    &proto2,
    &proto3,
    nullptr
  };

  get_supported_protocols_fake.return_val = dummy_protoco_stacks;

  ProtocolImplementation* returned_protocols[256];

  TransportProperties props;
  transport_properties_build(&props);

  tp_set_sel_prop_preference(&props, RELIABILITY, PROHIBIT);
  tp_set_sel_prop_preference(&props, CONGESTION_CONTROL, PREFER);
  tp_set_sel_prop_preference(&props, PRESERVE_ORDER, AVOID);

  int num_found = 0;

  transport_properties_get_candidate_stacks(&props.selection_properties, returned_protocols, &num_found);

  // We have two, each missing a single preference
  EXPECT_EQ(num_found, 2);
  EXPECT_STREQ(returned_protocols[0]->name, "p1");
  EXPECT_STREQ(returned_protocols[1]->name, "p3");
}

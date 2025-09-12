//
// Created by ikhovind on 12.08.25.
//

#include "transport_properties.h"

#include <protocols/registry/protocol_registry.h>

#include "selection_properties/selection_properties.h"

void transport_properties_build(TransportProperties* properties) {
  selection_properties_init(&properties->selection_properties);
}

// This is supposed to be invoked when initiating a preconnection,
// so it assumes that the selection properties have been set according to
// what kind of connection is desired.
bool protocol_implementation_supports_selection_properties(
  ProtocolImplementation* protocol,
  SelectionProperties* selection_properties) {
  for (int i = 0; i < SELECTION_PROPERTY_END; i++) {
    SelectionProperty desired_value = selection_properties->selection_property[i];
    SelectionProperty protocol_value = protocol->selection_properties.selection_property[i];

    if (desired_value.type == TYPE_PREFERENCE) {
      if (desired_value.value.simple_preference == REQUIRE && protocol_value.value.simple_preference == PROHIBIT) {
        printf("false 1 lol\n");
        printf("Desired value: %d, protocol value: %d\n", desired_value.value.simple_preference, protocol_value.value.simple_preference);
        return false;
      }
      if (desired_value.value.simple_preference == PROHIBIT && protocol_value.value.simple_preference == REQUIRE) {
        printf("false 2 lol\n");
        return false;
      }
    }
  }
  printf("Is supported\n");
  return true;
}

void transport_properties_sort_on_preferences(
  SelectionProperties *selection_properties,
  ProtocolImplementation** protocol_stacks,
  int num_found) {
  // Count the number of prefers and avoids that are satisfied by each protocol
  int protocol_scores[num_found];
  for (int i = 0; i < num_found; i++) {
    protocol_scores[i] = 0;
    for (int j = 0; j < SELECTION_PROPERTY_END; j++) {
      SelectionProperty desired_value = selection_properties->selection_property[j];
      SelectionProperty protocol_value = protocol_stacks[i]->selection_properties.selection_property[j];

      if (desired_value.type == TYPE_PREFERENCE) {
        // NO_PREFERENCE means that the protocol supports both, so it always satisfies the preference
        if (desired_value.value.simple_preference == PREFER && protocol_value.value.simple_preference != PROHIBIT) {
          protocol_scores[i]++;
        }
        if (desired_value.value.simple_preference == AVOID && protocol_value.value.simple_preference != REQUIRE) {
          protocol_scores[i]++;
        }
      }
    }
  }

  // stable sort protocol_stacks based on protocol_scores in descending order
  for (int i = 0; i < num_found - 1; i++) {
    for (int j = 0; j < num_found - i - 1; j++) {
      if (protocol_scores[j] < protocol_scores[j + 1]) {
        // Swap scores
        int temp_score = protocol_scores[j];
        protocol_scores[j] = protocol_scores[j + 1];
        protocol_scores[j + 1] = temp_score;

        // Swap protocols
        ProtocolImplementation* temp_protocol = protocol_stacks[j];
        protocol_stacks[j] = protocol_stacks[j + 1];
        protocol_stacks[j + 1] = temp_protocol;
      }
    }
  }
  printf("The top protocol is: %s, with score %d\n", protocol_stacks[0]->name, protocol_scores[0]);
}


int transport_properties_get_candidate_stacks(
  SelectionProperties *selection_properties,
  ProtocolImplementation** protocol_stacks,
  int* num_found) {
  ProtocolImplementation** supported_protocols = get_supported_protocols();
  *num_found = 0;
  for (int i = 0; i < MAX_PROTOCOLS && supported_protocols[i] != NULL; i++) {
    if (protocol_implementation_supports_selection_properties(supported_protocols[i], selection_properties)) {
      protocol_stacks[*num_found] = supported_protocols[i];
      (*num_found)++;
    }
  }

  transport_properties_sort_on_preferences(selection_properties, protocol_stacks, *num_found);

  memcpy(protocol_stacks, supported_protocols, sizeof(ProtocolImplementation*) * *num_found);
  return 0;
}


void tp_set_sel_prop_preference(TransportProperties* props, SelectionPropertyEnum prop_enum, SelectionPreference val) {
  set_sel_prop_preference(&props->selection_properties, prop_enum, val);  // Call the actual function to
}

void tp_set_sel_prop_multipath(TransportProperties* props, SelectionPropertyEnum prop_enum, MultipathEnum val) {
  set_sel_prop_multipath(&props->selection_properties, prop_enum, val);
}

void tp_set_sel_prop_direction(TransportProperties* props, SelectionPropertyEnum prop_enum, DirectionOfCommunicationEnum val) {
  set_sel_prop_direction(&props->selection_properties, prop_enum, val);
}

void tp_set_sel_prop_bool(TransportProperties* props, SelectionPropertyEnum prop_enum, bool val) {
  set_sel_prop_bool(&props->selection_properties, prop_enum, val);
}
void tp_set_sel_prop(TransportProperties* props, SelectionPropertyEnum prop_enum, SelectionPropertyValue val) {
  set_sel_prop(&props->selection_properties, prop_enum, val);
}

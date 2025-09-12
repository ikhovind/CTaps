//
// Created by ikhovind on 12.08.25.
//
#pragma once

#ifndef TRANSPORT_PROPERTIES_H
#define TRANSPORT_PROPERTIES_H
#include <protocols/protocol_interface.h>

#include "connection_properties/connection_properties.h"
#include "selection_properties/selection_properties.h"

#ifndef __cplusplus
#define selection_properties_set_selection_property(props, prop_enum, value) \
    _Generic((value), \
        SelectionPreference:            tp_set_sel_prop_preference, \
        MultipathEnum:                  tp_set_sel_prop_multipath, \
        bool:                           tp_set_sel_prop_bool, \
        SelectionPropertyValue:         tp_set_sel_prop, \
        DirectionOfCommunicationEnum:   tp_set_sel_prop_direction, \
        default:                        tp_set_sel_prop_preference \
    )(props, prop_enum, value)
#endif

typedef struct {
  SelectionProperties selection_properties;
  ConnectionProperties connection_properties;
} TransportProperties;

void transport_properties_build(TransportProperties* properties);

int transport_properties_get_candidate_stacks(
  SelectionProperties *selection_properties,
  ProtocolImplementation** protocol_stacks,
  int* num_found);

void tp_set_sel_prop_preference(TransportProperties* props, SelectionPropertyEnum prop_enum, SelectionPreference val);

void tp_set_sel_prop_multipath(TransportProperties* props, SelectionPropertyEnum prop_enum, MultipathEnum val);
void tp_set_sel_prop_direction(TransportProperties* props, SelectionPropertyEnum prop_enum, DirectionOfCommunicationEnum val);

void tp_set_sel_prop_bool(TransportProperties* props, SelectionPropertyEnum prop_enum, bool val);
void tp_set_sel_prop(TransportProperties* props, SelectionPropertyEnum prop_enum, SelectionPropertyValue);


#endif  // TRANSPORT_PROPERTIES_H

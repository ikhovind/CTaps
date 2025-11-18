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
        ct_selection_preference_t:            ct_tp_set_sel_prop_preference, \
        ct_multipath_enum_t:                  ct_tp_set_sel_prop_multipath, \
        bool:                           ct_tp_set_sel_prop_bool, \
        ct_direction_of_communication_enum_t:   ct_tp_set_sel_prop_direction, \
        default:                        ct_tp_set_sel_prop_preference \
    )(props, prop_enum, value)
#endif

typedef struct {
  ct_selection_properties_t selection_properties;
  ct_connection_properties_t connection_properties;
} ct_transport_properties_t;

void ct_transport_properties_build(ct_transport_properties_t* properties);

void ct_tp_set_sel_prop_preference(ct_transport_properties_t* props, ct_selection_property_enum_t prop_enum, ct_selection_preference_t val);
void ct_tp_set_sel_prop_multipath(ct_transport_properties_t* props, ct_selection_property_enum_t prop_enum, ct_multipath_enum_t val);
void ct_tp_set_sel_prop_direction(ct_transport_properties_t* props, ct_selection_property_enum_t prop_enum, ct_direction_of_communication_enum_t val);

void ct_tp_set_sel_prop_bool(ct_transport_properties_t* props, ct_selection_property_enum_t prop_enum, bool val);
void ct_tp_set_sel_prop(ct_transport_properties_t* props, ct_selection_property_enum_t prop_enum, ct_selection_property_value_t);
// TODO - this should be more generic, not only for interface
void ct_tp_set_sel_prop_interface(ct_transport_properties_t* props, char* interface_name, ct_selection_preference_t preference);


#endif  // TRANSPORT_PROPERTIES_H

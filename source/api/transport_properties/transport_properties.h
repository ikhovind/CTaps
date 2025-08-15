//
// Created by ikhovind on 12.08.25.
//
#pragma once

#ifndef TRANSPORT_PROPERTIES_H
#define TRANSPORT_PROPERTIES_H
#include "selection_properties/selection_properties.h"

typedef struct {
    SelectionProperties selection_properties;
} TransportProperties;

void selection_properties_set(SelectionProperties * selection_properties, const SelectionProperty selection_property, const SelectionPreference preference);

#endif //TRANSPORT_PROPERTIES_H

//
// Created by ikhovind on 12.08.25.
//

#include "transport_properties.h"
#include "selection_properties/selection_properties.h"

void transport_properties_build(TransportProperties *self) {
    return;
}

void selection_properties_set_selection_property(TransportProperties* transport_properties, SelectionProperty selection_property, SelectionPreference preference) {
    selection_properties_set(&transport_properties->selection_properties, selection_property, preference);
}

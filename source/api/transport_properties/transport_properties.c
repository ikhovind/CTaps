//
// Created by ikhovind on 12.08.25.
//

#include "transport_properties.h"
#include "selection_properties/selection_properties.h"

void selection_properties_set(SelectionProperties * selection_properties, const SelectionProperty selection_property, const SelectionPreference preference) {
    selection_properties->preference[selection_property] = preference;
}

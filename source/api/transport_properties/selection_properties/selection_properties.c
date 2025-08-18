//
// Created by ikhovind on 12.08.25.
//

#include "selection_properties.h"

struct {
    SelectionProperty selection_property;
    char* name;
} selection_property_name2[] = {
    mklist(f_arr)
};

void selection_properties_init(SelectionProperties* selection_properties) {
    selection_properties->preference[RELIABILITY] = REQUIRE;
}

void selection_properties_set(SelectionProperties * selection_properties, SelectionProperty selection_property, SelectionPreference preference) {
    selection_properties->preference[selection_property] = preference;
}

void selection_properties_require_prop(SelectionProperties *selection_properties,
    SelectionProperty selection_property) {
    printf("hello from require prop with arg: %d\n", selection_property);
}

void selection_properties_require_char(SelectionProperties *selection_properties, char *selection_property) {
    for (int i=0; i < SELECTION_PROPERTY_END; i++) {
        if (strcmp(selection_property, selection_property_name2[i].name) == 0) {
            printf("Found selection property with name: %s\n", selection_property_name2[i].name);
            selection_properties_require_prop(selection_properties, selection_property_name2[i].selection_property);
            return;
        }
    }
    printf("Did not find selection property with name: %s\n", selection_property);
}

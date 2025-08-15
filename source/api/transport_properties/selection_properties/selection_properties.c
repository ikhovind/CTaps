//
// Created by ikhovind on 12.08.25.
//

#include "selection_properties.h"

#include <stdio.h>

void selection_properties_init(SelectionProperties* selection_properties) {
    selection_properties->preference[RELIABILITY] = REQUIRE;
}

void selection_properties_require_prop(SelectionProperties * selection_properties, SelectionProperty selection_property) {
    printf("hello from require prop with arg: %d", selection_property);
}

void selection_properties_require_char(SelectionProperties * selection_properties, char* selection_property) {
    printf("Hello from require char with arg: %s\n", selection_property);
}

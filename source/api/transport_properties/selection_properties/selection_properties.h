//
// Created by ikhovind on 12.08.25.
//

#ifndef SELECTION_PROPERTIES_H
#define SELECTION_PROPERTIES_H

#define selection_properties_require(selection_properties, selection_property) \
    _Generic((selection_property),        \
        char*:   selection_properties_require_char,    \
        SelectionProperty:selection_properties_require_prop \
    )(selection_properties, selection_property)

typedef enum  {
    REQUIRE = 0,
    PREFER,
    NO_PREFERENCE,
    AVOID,
    PROHIBIT
} SelectionPreference;

typedef enum {
    RELIABILITY = 0,
    SELECTION_PROPERTY_END
} SelectionProperty;

typedef struct {
    SelectionPreference preference[SELECTION_PROPERTY_END];
} SelectionProperties;

void selection_properties_init(SelectionProperties* selection_properties);
void selection_properties_set(SelectionProperties * selection_properties, SelectionProperty selection_property, SelectionPreference preference);
void selection_properties_require_prop(SelectionProperties * selection_properties, SelectionProperty selection_property);
void selection_properties_require_char(SelectionProperties * selection_properties, char* selection_property);


#endif //SELECTION_PROPERTIES_H

#include "ctaps.h"
#include "ctaps_internal.h"
#include "selection_properties.h"
#include <logging/log.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#define create_sel_property_initializer(enum_name, string_name, property_type, function_name,      \
                                        default_value, type_enum)                                  \
    [enum_name] = {.name = (string_name),                                                          \
                   .type = type_enum,                                                              \
                   .set_by_user = false,                                                           \
                   .value = {(ct_selection_preference_t)(default_value)}},

const ct_selection_properties_t DEFAULT_SELECTION_PROPERTIES = {
    .list = {get_preference_set_selection_property_list(create_sel_property_initializer)
                 get_selection_property_list(create_sel_property_initializer)}};

void ct_selection_properties_build(ct_selection_properties_t* selection_properties) {
    memcpy(selection_properties, &DEFAULT_SELECTION_PROPERTIES, sizeof(ct_selection_properties_t));
}

void ct_selection_properties_cleanup(ct_selection_properties_t* selection_properties) {
    if (!selection_properties) {
        return;
    }
    for (size_t i = 0; i < SELECTION_PROPERTY_END; i++) {
        if (selection_properties->list[i].type == TYPE_PREFERENCE_SET) {
            for (size_t j = 0;
                 j < selection_properties->list[i].value.preference_set_val.num_combinations; j++) {
                free(selection_properties->list[i].value.preference_set_val.combinations[j].value);
            }
            free(selection_properties->list[i].value.preference_set_val.combinations);
        }
    }
}

void ct_selection_properties_deep_copy(ct_selection_properties_t* dest,
                                       const ct_selection_properties_t* src) {
    if (!src || !dest) {
        return;
    }
    memcpy(dest, src, sizeof(ct_selection_properties_t));
    dest->list[INTERFACE].value.preference_set_val.combinations = NULL;
    dest->list[INTERFACE].value.preference_set_val.num_combinations = 0;

    size_t interface_count = src->list[INTERFACE].value.preference_set_val.num_combinations;
    dest->list[INTERFACE].value.preference_set_val.combinations =
        malloc(interface_count * sizeof(ct_preference_combination_t));
    dest->list[INTERFACE].value.preference_set_val.num_combinations = interface_count;
    for (size_t i = 0; i < interface_count; i++) {
        dest->list[INTERFACE].value.preference_set_val.combinations[i].value =
            strdup(src->list[INTERFACE].value.preference_set_val.combinations[i].value);
        dest->list[INTERFACE].value.preference_set_val.combinations[i].preference =
            src->list[INTERFACE].value.preference_set_val.combinations[i].preference;
    }

    size_t pvd_count = src->list[PVD].value.preference_set_val.num_combinations;
    dest->list[PVD].value.preference_set_val.combinations =
        malloc(pvd_count * sizeof(ct_preference_combination_t));
    dest->list[PVD].value.preference_set_val.num_combinations = pvd_count;
    for (size_t i = 0; i < pvd_count; i++) {
        dest->list[PVD].value.preference_set_val.combinations[i].value =
            strdup(src->list[PVD].value.preference_set_val.combinations[i].value);
        dest->list[PVD].value.preference_set_val.combinations[i].preference =
            src->list[PVD].value.preference_set_val.combinations[i].preference;
    }
}

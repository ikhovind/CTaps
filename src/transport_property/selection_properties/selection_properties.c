#include "ctaps.h"
#include "ctaps_internal.h"
#include "selection_properties.h"

#include <logging/log.h>
#include <stddef.h>
#include <string.h>

#define create_sel_property_initializer(enum_name, string_name, property_type, function_name, default_value, type_enum) \
  [enum_name] = {                                                          \
    .name = (string_name),                                                   \
    .type = type_enum,                                                        \
    .set_by_user = false,                                                  \
    .value = { (ct_selection_preference_t)(default_value) }                     \
},

const ct_selection_properties_t DEFAULT_SELECTION_PROPERTIES = {
  .list = {
    get_preference_set_selection_property_list(create_sel_property_initializer)
    get_selection_property_list(create_sel_property_initializer)
  }
};

void ct_selection_properties_build(ct_selection_properties_t* selection_properties) {
  memcpy(selection_properties, &DEFAULT_SELECTION_PROPERTIES, sizeof(ct_selection_properties_t));
}

void ct_selection_properties_cleanup(ct_selection_properties_t* selection_properties) {
  if (!selection_properties) {
    return;
  }
  for (size_t i = 0; i < SELECTION_PROPERTY_END; i++) {
    if (selection_properties->list[i].type == TYPE_PREFERENCE_SET) {
      for (size_t j = 0; j < selection_properties->list[i].value.preference_set_val.num_combinations; j++) {
        free(selection_properties->list[i].value.preference_set_val.combinations[j].value);
      }
    }
  }
}

void ct_selection_properties_deep_copy(ct_selection_properties_t* dest, const ct_selection_properties_t* src) {
  if (!src || !dest) {
    return;
  }
  memcpy(dest, src, sizeof(ct_selection_properties_t));

  for (size_t i = 0; i < src->list[INTERFACE].value.preference_set_val.num_combinations; i++) {
    dest->list[INTERFACE].value.preference_set_val.combinations[i].value = strdup(src->list[INTERFACE].value.preference_set_val.combinations[i].value);
    dest->list[INTERFACE].value.preference_set_val.combinations[i].preference = src->list[INTERFACE].value.preference_set_val.combinations[i].preference;
    dest->list[INTERFACE].value.preference_set_val.num_combinations++;
  }
  for (size_t i = 0; i < src->list[PVD].value.preference_set_val.num_combinations; i++) {
    dest->list[PVD].value.preference_set_val.combinations[i].value = strdup(src->list[PVD].value.preference_set_val.combinations[i].value);
    dest->list[PVD].value.preference_set_val.combinations[i].preference = src->list[PVD].value.preference_set_val.combinations[i].preference;
    dest->list[PVD].value.preference_set_val.num_combinations++;
  }
}

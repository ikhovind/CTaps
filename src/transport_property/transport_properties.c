#include "ctaps.h"
#include "ctaps_internal.h"
#include "logging/log.h"
#include "transport_property/selection_properties/selection_properties.h"
#include "transport_property/transport_properties.h"
#include <stdbool.h>
#include <stdlib.h>

ct_transport_properties_t* ct_transport_properties_new(void) {
  ct_transport_properties_t* props = malloc(sizeof(ct_transport_properties_t));
  if (!props) {
    return NULL;
  }
  memset(props, 0, sizeof(ct_transport_properties_t));

  // Initialize with default values (inlined from removed _build function)
  memcpy(&props->selection_properties, &DEFAULT_SELECTION_PROPERTIES, sizeof(ct_selection_properties_t));
  memcpy(&props->connection_properties, &DEFAULT_CONNECTION_PROPERTIES, sizeof(ct_connection_properties_t));

  return props;
}

void ct_transport_properties_free(ct_transport_properties_t* props) {
  if (!props) {
    log_warn("Attempted to free NULL transport properties");
    return;
  }

  // Clean up selection properties (frees GHashTable if created)
  ct_selection_properties_cleanup(&props->selection_properties);
  // connection_properties has no dynamic allocations, no cleanup needed
  free(props);
}

ct_transport_properties_t* ct_transport_properties_deep_copy(const ct_transport_properties_t* src) {
  if (!src) {
    return NULL;
  }

  ct_transport_properties_t* dest = ct_transport_properties_new();
  if (!dest) {
    return NULL;
  }

  ct_selection_properties_deep_copy(&dest->selection_properties, &src->selection_properties);

  // Shallow copy connection properties (no dynamic allocations)
  dest->connection_properties = src->connection_properties;

  return dest;
}

void ct_transport_properties_add_interface_preference(ct_transport_properties_t* transport_props, const char* value, ct_selection_preference_t preference) {
  if (!transport_props) {
    log_warn("Null pointer passed to add_interface_preference");
    return;
  }
  ct_preference_set_t* set = &transport_props->selection_properties.list[INTERFACE].value.preference_set_val;
  log_debug("Before adding num_combinations: %zu", set->num_combinations);
  log_debug("Adding interface preference: %s with preference %d", value, preference);
  for (size_t i = 0; i < set->num_combinations; i++) {
    if (strcmp(value, set->combinations[i].value) == 0) {
      set->combinations[i].preference = preference;
      return;
    }
  }
  
  ct_preference_combination_t* tmp = realloc(set->combinations, (set->num_combinations + 1) * sizeof(ct_preference_combination_t));
  if (!tmp) {
    log_error("Failed to allocate memory for new interface preference combination");
    return;
  }
  set->combinations = tmp;
  set->combinations[set->num_combinations].value = strdup(value);
  set->combinations[set->num_combinations].preference = preference;
  set->num_combinations++;
  log_debug("After adding num_combinations: %zu", set->num_combinations);
}

void ct_transport_properties_add_pvd_preference(ct_transport_properties_t* transport_props, const char* value, ct_selection_preference_t preference) {
  if (!transport_props) {
    log_warn("Null pointer passed to add_pvd_preference");
    return;
  }
  ct_preference_set_t* set = &transport_props->selection_properties.list[PVD].value.preference_set_val;
  for (size_t i = 0; i < set->num_combinations; i++) {
    if (strcmp(value, set->combinations[i].value) == 0) {
      set->combinations[i].preference = preference;
      return;
    }
  }
  ct_preference_combination_t* tmp = realloc(set->combinations, (set->num_combinations + 1) * sizeof(ct_preference_combination_t));
  if (!tmp) {
    log_error("Failed to allocate memory for new interface preference combination");
    return;
  }
  set->combinations = tmp;
  set->combinations[set->num_combinations].value = strdup(value);
  set->combinations[set->num_combinations].preference = preference;
  set->num_combinations++;
}

ct_selection_preference_t ct_transport_properties_get_interface_preference(const ct_transport_properties_t *transport_props, const char *value) {
  if (!transport_props) {
    return NO_PREFERENCE;
  }
  for (size_t i = 0; i < transport_props->selection_properties.list[INTERFACE].value.preference_set_val.num_combinations; i++) {
    if (strcmp(value, transport_props->selection_properties.list[INTERFACE].value.preference_set_val.combinations[i].value) == 0) {
      return transport_props->selection_properties.list[INTERFACE].value.preference_set_val.combinations[i].preference;
    }
  }
  return NO_PREFERENCE;
}

ct_selection_preference_t ct_transport_properties_get_pvd_preference(const ct_transport_properties_t *transport_props, const char *value) {
  if (!transport_props) {
    return NO_PREFERENCE;
  }
  for (size_t i = 0; i < transport_props->selection_properties.list[PVD].value.preference_set_val.num_combinations; i++) {
    if (strcmp(value, transport_props->selection_properties.list[PVD].value.preference_set_val.combinations[i].value) == 0) {
      return transport_props->selection_properties.list[PVD].value.preference_set_val.combinations[i].preference;
    }
  }
  return NO_PREFERENCE;
}

// Map type tags to union members
#define UNION_MEMBER_TYPE_UINT32  uint32_val
#define UNION_MEMBER_TYPE_UINT64  uint64_val
#define UNION_MEMBER_TYPE_BOOL    bool_val
#define UNION_MEMBER_TYPE_ENUM    enum_val
#define UNION_MEMBER_TYPE_PREFERENCE      enum_val
#define UNION_MEMBER_TYPE_ENUM_VAL  enum_val
#define UNION_MEMBER_TYPE_PREFERENCE_SET  preference_set_val

#define DEFINE_CONNECTION_PROPERTY_GETTER(ENUM, STRING, TYPE, FIELD, DEFAULT, TYPE_TAG)    \
  TYPE ct_transport_properties_get_##FIELD(const ct_transport_properties_t* tp) {          \
    if (!tp) {                                                                              \
      log_warn("Null pointer passed to get_" #FIELD);                                      \
      return (TYPE)(DEFAULT);                                                               \
    }                                                                                       \
    return tp->connection_properties.list[ENUM].value.UNION_MEMBER_##TYPE_TAG;             \
  }

#define DEFINE_CONNECTION_PROPERTY_SETTER(ENUM, STRING, TYPE, FIELD, DEFAULT, TYPE_TAG)    \
  void ct_transport_properties_set_##FIELD(ct_transport_properties_t* tp, TYPE val) {      \
    if (!tp) {                                                                              \
      log_warn("Null pointer passed to set_" #FIELD);                                      \
      return;                                                                               \
    }                                                                                       \
    tp->connection_properties.list[ENUM].value.UNION_MEMBER_##TYPE_TAG = val;              \
  }


#define DEFINE_SELECTION_PROPERTY_GETTER(ENUM, STRING, TYPE, FIELD, DEFAULT, TYPE_TAG)  \
  TYPE ct_transport_properties_get_##FIELD(const ct_transport_properties_t* tp) {        \
    if (!tp) {                                                                            \
      log_warn("Null pointer passed to get_" #FIELD);                                    \
      return (TYPE)(DEFAULT);                                                             \
    }                                                                                     \
    return tp->selection_properties.list[ENUM].value.UNION_MEMBER_##TYPE_TAG;            \
  }

#define DEFINE_SELECTION_PROPERTY_SETTER(ENUM, STRING, TYPE, FIELD, DEFAULT, TYPE_TAG)  \
  void ct_transport_properties_set_##FIELD(ct_transport_properties_t* tp, TYPE val) {   \
    if (!tp) {                                                                            \
      log_warn("Null pointer passed to set_" #FIELD);                                    \
      return;                                                                             \
    }                                                                                     \
    tp->selection_properties.list[ENUM].value.UNION_MEMBER_##TYPE_TAG = val;             \
    tp->selection_properties.list[ENUM].set_by_user = true;                             \
  }


get_writable_connection_property_list(DEFINE_CONNECTION_PROPERTY_GETTER)
get_read_only_connection_properties(DEFINE_CONNECTION_PROPERTY_GETTER)
get_tcp_connection_properties(DEFINE_CONNECTION_PROPERTY_GETTER)
get_writable_connection_property_list(DEFINE_CONNECTION_PROPERTY_SETTER)
get_tcp_connection_properties(DEFINE_CONNECTION_PROPERTY_SETTER)
get_selection_property_list(DEFINE_SELECTION_PROPERTY_GETTER)
get_selection_property_list(DEFINE_SELECTION_PROPERTY_SETTER)
get_preference_set_selection_property_list(DEFINE_SELECTION_PROPERTY_SETTER)


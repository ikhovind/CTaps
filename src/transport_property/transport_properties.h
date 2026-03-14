#ifndef CT_TRANSPORT_PROPERTIES_H
#define CT_TRANSPORT_PROPERTIES_H
#include <ctaps.h>

// Used in our macro generators to refer to union member names, see 
//  create_con_property_initializer for an example
#define UNION_MEMBER_TYPE_UINT8  uint8_val
#define UNION_MEMBER_TYPE_UINT32 uint32_val
#define UNION_MEMBER_TYPE_UINT64 uint64_val
#define UNION_MEMBER_TYPE_BOOL bool_val
#define UNION_MEMBER_TYPE_ENUM enum_val
#define UNION_MEMBER_TYPE_PREFERENCE simple_preference
#define UNION_MEMBER_TYPE_ENUM_VAL enum_val
#define UNION_MEMBER_TYPE_PREFERENCE_SET preference_set_val

/**
 * @brief Deep copy transport properties.
 *
 * Creates a new transport properties object with copies of all data.
 *
 * @param src Source transport properties to copy
 * @return Newly allocated copy, or NULL on failure
 */
ct_transport_properties_t* ct_transport_properties_deep_copy(const ct_transport_properties_t* src);

#endif // CT_TRANSPORT_PROPERTIES_H

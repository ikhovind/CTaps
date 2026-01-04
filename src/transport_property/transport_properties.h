#ifndef CT_TRANSPORT_PROPERTIES_H
#define CT_TRANSPORT_PROPERTIES_H
#include <ctaps.h>

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

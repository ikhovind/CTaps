#ifndef CT_CONNECTION_PROPERTIES_H
#define CT_CONNECTION_PROPERTIES_H
#include <ctaps.h>

/**
 * @brief Initialize connection properties with default values.
 * @param[out] connection_properties structure to initialize
 */
void ct_connection_properties_build(ct_connection_properties_t* connection_properties);

/**
 * @brief Free resources in connection properties.
 * @param[in] connection_properties structure to free
 */
void ct_connection_properties_free(ct_connection_properties_t* connection_properties);
#endif // CT_CONNECTION_PROPERTIES_H

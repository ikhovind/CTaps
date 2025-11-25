#ifndef CT_CONNECTION_GROUP_H
#define CT_CONNECTION_GROUP_H
#include "ctaps.h"

/**
  * @brief Add a connection to a connection group.
  *
  * @param[in,out] group Connection group to add to
  * @param[in] connection Connection to add
  *
  * @return 0 on success, -EINVAL if connection or group is NULL, -EEXIST if connection already in group
  */
int ct_connection_group_add_connection(ct_connection_group_t* group, ct_connection_t* connection);

/**
  * @brief Get the first connection from a connection group.
  *
  * @param[in] group Connection group to get connection from
  *
  * @return Pointer to first connection, or NULL if group is NULL or empty
  */
ct_connection_t* ct_connection_group_get_first(ct_connection_group_t* group);

/**
 * @brief Decrement the active connection counter in a connection group.
 *
 * @param[in,out] group The connection group
 */
void ct_connection_group_decrement_active(ct_connection_group_t* group);

uint64_t connection_group_get_num_active_connections(ct_connection_group_t* group);


#endif // CT_CONNECTION_GROUP_H

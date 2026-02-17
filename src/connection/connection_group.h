#ifndef CT_CONNECTION_GROUP_H
#define CT_CONNECTION_GROUP_H
#include "ctaps.h"
#include "ctaps_internal.h"


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
 * @brief Get the number of active connections in a connection group.
 *
 * @param[in] group Connection group to query
 *
 * @return Number of active connections in the group
 */
uint64_t ct_connection_group_get_num_active_connections(ct_connection_group_t* group);

/**
 * @brief Remove a connection from a connection group.
 *
 * @param[in,out] group Connection group to remove from
 * @param[in] connection Connection to remove
 *
 * @return 0 on success, -EINVAL if group or connection is NULL, -ENOENT if connection not in group
 */
int ct_connection_group_remove_connection(ct_connection_group_t* group, ct_connection_t* connection);

/**
 * @brief Check if a connection group is empty.
 *
 * @param[in] group Connection group to check
 *
 * @return true if the group has no connections, false otherwise
 */
bool ct_connection_group_is_empty(ct_connection_group_t* group);

/**
 * @brief Free a connection group and its internal resources.
 *
 * @param[in] group Connection group to free
 */
void ct_connection_group_free(ct_connection_group_t* group);

/**
 * @brief Close all connections in a connection group gracefully (internal).
 *
 * @param[in] connection_group The connection group to close
 *
 * @note Internal function - public API uses ct_connection_close_group()
 */
void ct_connection_group_close_all(ct_connection_group_t* connection_group);

/**
 * @brief Abort all connections in a connection group (internal).
 *
 * @param[in] connection_group The connection group to abort
 *
 * @note Internal function - public API uses ct_connection_abort_group()
 */
void ct_connection_group_abort_all(ct_connection_group_t* connection_group);

/**
 * @brief Get the number of active connections in a connection group (internal).
 *
 * @param[in] group Connection group to query
 * @return Number of active connections in the group
 *
 * @note Internal function - used by protocol implementations
 */
uint64_t ct_connection_group_get_num_active_connections(ct_connection_group_t* group);

ct_connection_group_t* ct_connection_group_ref(ct_connection_group_t* group);

void ct_connection_group_unref(ct_connection_group_t* group);

ct_connection_group_t* ct_connection_group_new();


#endif // CT_CONNECTION_GROUP_H

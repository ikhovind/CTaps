#ifndef CONNECTION_H
#define CONNECTION_H

#include "ctaps.h"

/**
 * @brief Initialize a connection with zeroed memory and generate a UUID.
 *
 * This helper function should be called at the start of any connection
 * building function to ensure the connection starts in a clean state with
 * a unique identifier.
 *
 * @param[out] connection Connection to initialize
 */
void ct_connection_build_with_connection_group(ct_connection_t* connection);

/**
  * @brief Allocate 0-initialized connection with only UUID.
  *
  * @return Pointer to newly created empty connection, or NULL on error
  */
ct_connection_t* create_empty_connection_with_uuid();

/**
 * @brief Mark a connection as closed.
 *
 * @param[in,out] connection The connection to mark as closed
 */
void ct_connection_mark_as_closed(ct_connection_t* connection);


#endif // CONNECTION_H

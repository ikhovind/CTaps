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
int ct_connection_build_with_new_connection_group(ct_connection_t* connection);

/**
  * @brief Allocate 0-initialized connection with only UUID.
  *
  * @return Pointer to newly created empty connection, or NULL on error
  */
ct_connection_t* create_empty_connection_with_uuid();

/**
 * @brief Mark a connection as established.
 *
 * @param[in,out] connection The connection to mark as established
 */
void ct_connection_mark_as_established(ct_connection_t* connection);

/**
 * @brief Mark a connection as closed.
 *
 * @param[in,out] connection The connection to mark as closed
 */
void ct_connection_mark_as_closed(ct_connection_t* connection);

/**
 * @brief Mark a connection as closing.
 *
 * @param[in,out] connection The connection to mark as closing
 */
void ct_connection_mark_as_closing(ct_connection_t* connection);

/**
 * @brief Free the internal content of a connection without freeing the connection pointer itself.
 *
 * @param[in,out] connection The connection whose content should be freed
 */
void ct_connection_free_content(ct_connection_t* connection);

/**
 * @brief Create a new connection by cloning from an existing connection.
 *
 * Allocates and initializes a new connection in the same connection group as the source,
 * copying all relevant properties (endpoints, transport properties, security parameters, etc.).
 * This is used for creating additional streams in QUIC or cloning UDP connections.
 *
 * @param[in] src_clone Source connection to clone from
 * @return Pointer to newly created connection, or NULL on error
 */
ct_connection_t* ct_connection_create_clone(const ct_connection_t* src_clone);

/**
 * @brief Set the can receive connection property
 *
 * @param[in,out] connection The connection to modify
 * @param[in] can_receive New value for the canReceive property
 */
void ct_connection_set_can_receive(ct_connection_t* connection, bool can_receive);

/**
 * @brief Create a connection from an accepted handle (internal helper).
 * @param[in] listener Parent listener
 * @param[in] received_handle libuv stream handle for the accepted connection
 * @return Pointer to newly created connection, or NULL on error
 */
ct_connection_t* ct_connection_build_from_received_handle(const struct ct_listener_s* listener, uv_stream_t* received_handle);

/**
 * @brief Initialize a multiplexed connection (internal helper).
 * @param[out] connection Connection to initialize
 * @param[in] listener Parent listener
 * @param[in] remote_endpoint Remote endpoint of the peer
 *
 * @return 0 on success, non-zero on error
 */
int ct_connection_build_multiplexed(ct_connection_t* connection, const struct ct_listener_s* listener, const ct_remote_endpoint_t* remote_endpoint);

#endif // CONNECTION_H

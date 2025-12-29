#ifndef CT_CTAPS_INTERNAL_H
#define CT_CTAPS_INTERNAL_H
#include <ctaps.h>

typedef struct ct_listener_s {
  ct_transport_properties_t transport_properties;     ///< Transport properties for accepted connections
  ct_local_endpoint_t local_endpoint;                 ///< Local endpoint (listening address/port)
  size_t num_local_endpoints;                         ///< Number of local endpoints
  ct_listener_callbacks_t listener_callbacks;         ///< User-provided callbacks for listener events
  const ct_security_parameters_t* security_parameters; ///< Security configuration for accepted connections
  struct ct_socket_manager_s* socket_manager;         ///< Socket manager handling listening sockets
} ct_listener_t;

/**
 * @brief Connection group for managing related connections.
 */
typedef struct ct_connection_group_s {
  char connection_group_id[37];           ///< Unique identifier for this group
  GHashTable* connections;                ///< Map of UUID string → ct_connection_t*
  void* connection_group_state;           ///< Protocol-specific shared state
  uint64_t num_active_connections;        ///< Number of active connections in this group
} ct_connection_group_t;

/**
 * Definition of data structures used in ctaps.h
 */
typedef struct ct_connection_s {
  char uuid[37];                                       ///< Unique identifier for this connection (UUID string)
  ct_connection_group_t* connection_group;             ///< Connection group (never NULL)
  ct_transport_properties_t transport_properties;      ///< Transport and connection properties
  const ct_security_parameters_t* security_parameters; ///< Security configuration (TLS/QUIC)
  ct_local_endpoint_t local_endpoint;                  ///< Local endpoint (bound address/port)
  ct_remote_endpoint_t remote_endpoint;                ///< Remote endpoint (peer address/port)
  ct_protocol_impl_t protocol;                         ///< Protocol implementation in use
  void* internal_connection_state;                     ///< Protocol-specific per-connection state (opaque)
  ct_framer_impl_t* framer_impl;                       ///< Optional message framer (NULL = no framing)
  ct_connection_socket_type_t socket_type;             ///< Socket type (standalone vs multiplexed)
  ct_connection_role_t role;                           ///< Connection role (client vs server)
  ct_connection_callbacks_t connection_callbacks;      ///< User-provided callbacks for events
  struct ct_socket_manager_s* socket_manager;          ///< Socket manager (for listeners/mux)
  GQueue* received_callbacks;                          ///< Queue of pending receive callbacks
  GQueue* received_messages;                           ///< Queue of received messages
} ct_connection_t;


#endif

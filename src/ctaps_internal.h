#ifndef CT_CTAPS_INTERNAL_H
#define CT_CTAPS_INTERNAL_H
#include <ctaps.h>

/**
 * @brief Collection of all transport selection properties.
 *
 * This structure contains all selection properties that influence protocol selection
 * during connection establishment. Properties are indexed by ct_selection_property_enum_t.
 */
typedef struct ct_selection_properties_s {
  ct_selection_property_t selection_property[SELECTION_PROPERTY_END];  ///< Array of selection properties
} ct_selection_properties_t;

// The value cast is a hack to please the c++ compiler for our tests
#define create_sel_property_initializer(enum_name, string_name, property_type, default_value) \
  [enum_name] = {                                                          \
    .name = string_name,                                                   \
    .type = property_type,                                                 \
    .set_by_user = false,                                                  \
    .value = { (ct_selection_preference_t)default_value }                     \
},

static const ct_selection_properties_t DEFAULT_SELECTION_PROPERTIES = {
  .selection_property = {
    get_selection_property_list(create_sel_property_initializer)
  }
};

static const ct_message_properties_t DEFAULT_MESSAGE_PROPERTIES = {
  .message_property = {
    get_message_property_list(create_sel_property_initializer)
  }
};

/**
 * @brief Transport properties for protocol selection and connection configuration.
 *
 * This structure contains both selection properties (for choosing protocols) and
 * connection properties (for configuring active connections).
 */
typedef struct ct_transport_properties_s {
  ct_selection_properties_t selection_properties;      ///< Properties for protocol selection
  ct_connection_properties_t connection_properties;    ///< Properties for connection configuration
} ct_transport_properties_t;

/**
 * @brief Protocol implementation interface.
 *
 * This interface defines the contract that all transport protocol implementations
 * (TCP, UDP, QUIC, or custom protocols) must implement.
 */
typedef struct ct_protocol_impl_s {
  const char* name;                              ///< Protocol name (e.g., "TCP", "UDP", "QUIC")
  ct_selection_properties_t selection_properties; ///< Properties supported by this protocol

  /** @brief Initialize a new connection using this protocol. */
  int (*init)(struct ct_connection_s* connection, const ct_connection_callbacks_t* connection_callbacks);

  /** @brief Send a message over the protocol. */
  int (*send)(struct ct_connection_s*, ct_message_t*, ct_message_context_t*);

  /** @brief Start listening for incoming connections. */
  int (*listen)(struct ct_socket_manager_s* socket_manager);

  /** @brief Stop listening for incoming connections. */
  int (*stop_listen)(struct ct_socket_manager_s*);

  /** @brief Close a connection. */
  int (*close)(struct ct_connection_s*);

  /** @brief Forcefully abort a connection without graceful shutdown. */
  void (*abort)(struct ct_connection_s* connection);

  /** @brief Clone a connection's protocol specific state. */
  int (*clone_connection)(const struct ct_connection_s* source_connection,
                          struct ct_connection_s* target_connection);

  /** @brief Extract remote endpoint information from a connected peer handle. */
  int (*remote_endpoint_from_peer)(uv_handle_t* peer, ct_remote_endpoint_t* resolved_peer);

  /** @brief Retarget protocol-specific connection state during racing. */
  void (*retarget_protocol_connection)(struct ct_connection_s* from_connection, struct ct_connection_s* to_connection);
} ct_protocol_impl_t;

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
/**
 * @brief Preconnection configuration object.
 *
 * Created before establishing a connection, this object holds all configuration
 * (endpoints, properties, security) needed to initiate a connection or start a listener.
 */
typedef struct ct_preconnection_s {
  ct_transport_properties_t transport_properties;     ///< Transport property preferences
  const ct_security_parameters_t* security_parameters; ///< Security configuration
  ct_local_endpoint_t local;                          ///< Local endpoint specification
  size_t num_local_endpoints;                         ///< Number of local endpoints
  ct_remote_endpoint_t* remote_endpoints;             ///< Array of remote endpoints
  size_t num_remote_endpoints;                        ///< Number of remote endpoints
  ct_framer_impl_t* framer_impl;                      ///< Optional message framer
} ct_preconnection_t;

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

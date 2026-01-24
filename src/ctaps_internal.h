#ifndef CT_CTAPS_INTERNAL_H
#define CT_CTAPS_INTERNAL_H
#include <ctaps.h>

struct ct_socket_manager_s;
// =============================================================================
// Endpoint Internal Definitions
// =============================================================================
/**
 * @brief Local endpoint specification for binding connections/listeners.
 */
typedef struct ct_local_endpoint_s {
  uint16_t port;            ///< Port number (0 = any port)
  char* interface_name;     ///< Network interface name (e.g., "eth0") or NULL for any
  char* service;            ///< Service name (e.g., "http") or NULL
  union {
    struct sockaddr_storage resolved_address;  ///< Resolved socket address
  } data;
} ct_local_endpoint_t;

/**
 * @brief Wrapper for queued messages with their context.
 *
 * Used to store messages along with their context when no receive callback
 * is ready. The context contains endpoint pointers that remain valid as
 * long as the connection exists.
 */
typedef struct ct_queued_message_s {
  ct_message_t* message;           ///< The queued message
  ct_message_context_t* context;   ///< Message context with endpoint info
} ct_queued_message_t;

/**
 * @brief Remote endpoint specification for connection targets.
 *
 * Specifies the remote address, hostname, port, or service name to connect to.
 */
typedef struct ct_remote_endpoint_s {
  uint16_t port;            ///< Port number
  char* service;            ///< Service name (e.g., "https") or NULL
  char* hostname;           ///< Hostname for DNS resolution or NULL
  union {
    struct sockaddr_storage resolved_address;  ///< Resolved socket address
  } data;
} ct_remote_endpoint_t;

// =============================================================================
// Message Internal Definitions
// =============================================================================
/**
 * @brief A message containing data to send or received data.
 */
typedef struct ct_message_s {
  char* content;         ///< Message data buffer
  size_t length;   ///< Length of message data in bytes
} ct_message_t;



// =============================================================================
// Security Parameters Internal Definitions
// =============================================================================

/**
 * @brief Type of value stored in a security parameter.
 */
typedef enum ct_sec_property_type_e {
  TYPE_STRING_ARRAY,  ///< Array of strings (e.g., ALPN protocols, cipher suites)
  TYPE_CERTIFICATE_BUNDLES,  ///< ct_certificate_bundles_t for certificate configuration
  TYPE_STRING  ///< Single string value
} ct_sec_property_type_t;

typedef struct ct_certificate_bundle_s {
  char* certificate_file_name;
  char* private_key_file_name;
} ct_certificate_bundle_t;

typedef struct ct_certificate_bundles_s {
  ct_certificate_bundle_t* certificate_bundles;
  size_t num_bundles;
} ct_certificate_bundles_t;

/**
 * @brief String array value for security parameters.
 */
typedef struct {
  char** strings;       ///< Array of string pointers
  size_t num_strings;   ///< Number of strings in the array
} ct_string_array_value_t;

ct_string_array_value_t* ct_string_array_value_new(char** strings, size_t num_strings);


/**
 * @brief Union holding security parameter values.
 */
typedef union ct_sec_property_value_u {
  ct_string_array_value_t* array_of_strings;  ///< For TYPE_STRING_ARRAY properties
  ct_certificate_bundles_t* certificate_bundles; ///< For TYPE_CERTIFICATE_BUNDLES properties
  char* string;                         ///< For TYPE_STRING properties
} ct_sec_property_value_t;

/**
 * @brief A single security parameter.
 */
typedef struct ct_sec_property_s {
  char* name;                        ///< Parameter name string
  ct_sec_property_type_t type;       ///< Type of value stored
  bool set_by_user;                  ///< True if user explicitly set this parameter
  ct_sec_property_value_t value;     ///< Parameter value
} ct_security_parameter_t;

/**
 * @brief Collection of all security parameters.
 */
typedef struct ct_security_parameters_s {
  ct_security_parameter_t security_parameters[SEC_PROPERTY_END];  ///< Array of security parameters
} ct_security_parameters_t;

#define create_sec_property_initializer(enum_name, string_name, property_type) \
{                                                                              \
    .name = string_name,                                                       \
    .type = property_type,                                                     \
    .set_by_user = false,                                                      \
    .value = {0}                                                               \
},

static const ct_security_parameters_t DEFAULT_SECURITY_PARAMETERS = {
  .security_parameters = {
    get_security_parameter_list(create_sec_property_initializer)
  }
};

// =============================================================================
// Transport Properties Internal Definitions
// =============================================================================
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

// =============================================================================
// Message Properties - Properties for individual messages
// =============================================================================

/**
 * @brief Type of value stored in a message property.
 */
typedef enum ct_message_property_type_e {
  TYPE_UINT32_MSG,
  TYPE_BOOLEAN_MSG,
  TYPE_UINT64_MSG,
  TYPE_ENUM_MSG
} ct_message_property_type_t;

/**
 * @brief Union holding message property values.
 */
typedef union {
  uint32_t uint32_value;
  bool boolean_value;
  uint64_t uint64_value;
  ct_capacity_profile_enum_t capacity_profile_enum_value;
} ct_message_property_value_t;

/**
 * @brief A single message property.
 */
typedef struct ct_message_property_s {
  char* name;                              ///< Property name string
  ct_message_property_type_t type;         ///< Type of value stored
  bool set_by_user;                        ///< True if user explicitly set this property
  ct_message_property_value_t value;       ///< Property value
} ct_message_property_t;

/**
 * @brief Collection of all message properties.
 *
 * Contains properties that can be set on a per-message basis to control transmission
 * characteristics. Properties are indexed by ct_message_property_enum_t.
 */
typedef struct ct_message_properties_s {
  ct_message_property_t message_property[MESSAGE_PROPERTY_END];  ///< Array of message properties
} ct_message_properties_t;

// The value cast is a hack to please the c++ compiler for our tests
#define create_message_property_initializer(enum_name, string_name, property_type, default_value) \
  [enum_name] = {                                                          \
    .name = string_name,                                                   \
    .type = property_type,                                                 \
    .set_by_user = false,                                                  \
    .value = { (uint32_t)default_value }                     \
},

static const ct_message_properties_t DEFAULT_MESSAGE_PROPERTIES = {
  .message_property = {
    get_message_property_list(create_message_property_initializer)
  }
};



/**
 * @brief Union holding connection property values.
 */
typedef union {
  uint32_t uint32_val;
  uint64_t uint64_val;
  bool bool_val;
  int enum_val;
} ct_connection_property_value_t;

/**
 * @brief A single connection property.
 */
typedef struct ct_connection_property_s {
  char* name;                              ///< Property name string
  bool read_only;                          ///< True if property cannot be modified by user
  ct_connection_property_value_t value;    ///< Property value
} ct_connection_property_t;


typedef struct ct_connection_properties_s {
  ct_connection_property_t list[CONNECTION_PROPERTY_END];  ///< Array of connection properties
} ct_connection_properties_t;

static const ct_connection_property_t DEFAULT_CONNECTION_PROPERTIES[] = {
    get_writable_connection_property_list(create_con_property_initializer)
    get_read_only_connection_properties(create_con_property_initializer)
    get_tcp_connection_properties(create_con_property_initializer)
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

typedef struct ct_message_context_s {
  ct_message_properties_t message_properties;  ///< Per-message transmission properties
  const ct_local_endpoint_t* local_endpoint;         ///< Local endpoint for this message (optional)
  const ct_remote_endpoint_t* remote_endpoint;       ///< Remote endpoint for this message (optional)
  void* user_receive_context;                  ///< User context from ct_receive_callbacks_t
} ct_message_context_t;

/**
 * @brief Protocol implementation interface.
 *
 * This interface defines the contract that all transport protocol implementations
 * (TCP, UDP, QUIC, or custom protocols) must implement.
 */
typedef struct ct_protocol_impl_s {
  const char* name;                              ///< Protocol name (e.g., "TCP", "UDP", "QUIC")
  ct_protocol_enum_t protocol_enum;              ///< Protocol enumeration value
  bool supports_alpn;                        ///< True if protocol supports ALPN negotiation
  ct_selection_properties_t selection_properties; ///< Properties supported by this protocol

  /** @brief Initialize a new connection using this protocol. */
  int (*init)(ct_connection_t* connection, const ct_connection_callbacks_t* connection_callbacks, ct_message_t* initial_message, ct_message_context_t* initial_message_context);

  /** @brief Send a message over the protocol. */
  int (*send)(ct_connection_t*, ct_message_t*, ct_message_context_t*);

  /** @brief Start listening for incoming connections. */
  int (*listen)(struct ct_socket_manager_s* socket_manager);

  /** @brief Stop listening for incoming connections. */
  int (*stop_listen)(struct ct_socket_manager_s*);

  /** @brief Close a connection. */
  int (*close)(ct_connection_t*);

  /** @brief Forcefully abort a connection without graceful shutdown. */
  void (*abort)(ct_connection_t* connection);

  /** @brief Clone a connection's protocol specific state. */
  int (*clone_connection)(const ct_connection_t* source_connection,
                          ct_connection_t* target_connection);

  /** @brief Extract remote endpoint information from a connected peer handle. */
  int (*remote_endpoint_from_peer)(uv_handle_t* peer, ct_remote_endpoint_t* resolved_peer);

  /** @brief Retarget protocol-specific connection state during racing. */
  void (*retarget_protocol_connection)(ct_connection_t* from_connection, ct_connection_t* to_connection);
} ct_protocol_impl_t;

bool ct_protocol_supports_alpn(const ct_protocol_impl_t* protocol_impl);

typedef struct ct_listener_s {
  ct_transport_properties_t transport_properties;     ///< Transport properties for accepted connections
  ct_local_endpoint_t local_endpoint;                 ///< Local endpoint (listening address/port)
  size_t num_local_endpoints;                         ///< Number of local endpoints
  ct_listener_callbacks_t listener_callbacks;         ///< User-provided callbacks for listener events
  ct_security_parameters_t* security_parameters;       ///< Security configuration for accepted connections (owned copy)
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
  ct_security_parameters_t* security_parameters;       ///< Security configuration (owned copy)
  ct_local_endpoint_t local;                          ///< Local endpoint specification
  size_t num_local_endpoints;                         ///< Number of local endpoints
  ct_remote_endpoint_t* remote_endpoints;             ///< Array of remote endpoints
  size_t num_remote_endpoints;                        ///< Number of remote endpoints
  ct_framer_impl_t* framer_impl;                      ///< Optional message framer
} ct_preconnection_t;


// =============================================================================
// Connections
// =============================================================================

/**
 * @brief Connection socket type classification.
 */
typedef enum {
  CONNECTION_SOCKET_TYPE_STANDALONE = 0,    ///< Independent connection
  CONNECTION_SOCKET_TYPE_MULTIPLEXED,       ///< Multiplexed connection (e.g., QUIC stream)
} ct_connection_socket_type_t;

/**
 * @brief Connection role classification.
 */
typedef enum {
  CONNECTION_ROLE_CLIENT = 0,           ///< Connection initiated by local endpoint
  CONNECTION_ROLE_SERVER,               ///< Connection accepted from remote endpoint
} ct_connection_role_t;

typedef struct ct_connection_s {
  char uuid[37];                                       ///< Unique identifier for this connection (UUID string)
  ct_connection_group_t* connection_group;             ///< Connection group (never NULL)
  ct_transport_properties_t transport_properties;      ///< Transport and connection properties
  ct_security_parameters_t* security_parameters;       ///< Security configuration (TLS/QUIC, owned copy)
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

  bool used_0rtt;                                      ///< True if 0-RTT was used for this connection
} ct_connection_t;


#endif

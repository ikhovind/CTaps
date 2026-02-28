#ifndef CT_CTAPS_INTERNAL_H
#define CT_CTAPS_INTERNAL_H
#include "ctaps.h"
#include <glib.h>
#include <uv.h>

extern uv_loop_t* event_loop;

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
//

/**
 * @brief Union holding security parameter values.
 * Opaque type - full definition is in ctaps_internal.h.
 */
typedef union ct_sec_property_value_u ct_sec_property_value_t;

/**
 * @brief A single security parameter.
 * Opaque type - full definition is in ctaps_internal.h.
 */
typedef struct ct_sec_property_s ct_security_parameter_t;

// clang-format off
#define get_security_parameter_scalar_list(f)                                                                                                           \
f(TICKET_STORE_PATH,          "ticketStorePath",          char*,                     ticket_store_path,          NULL, TYPE_STRING)              \
f(SERVER_NAME_IDENTIFICATION, "serverNameIdentification", char*,                     server_name_identification, NULL, TYPE_STRING)

#define get_security_parameter_array_list(f)                                                                                                            \
f(SUPPORTED_GROUP,               "supportedGroup",             ct_string_array_t*,        supported_group,               NULL, TYPE_STRING_ARRAY)        \
f(CIPHERSUITE,                   "ciphersuite",                ct_string_array_t*,        ciphersuite,                   NULL, TYPE_STRING_ARRAY)        \
f(SERVER_CERTIFICATE,            "serverCertificate",          ct_certificate_bundles_t*, server_certificate,            NULL, TYPE_CERTIFICATE_BUNDLES) \
f(CLIENT_CERTIFICATE,            "clientCertificate",          ct_certificate_bundles_t*, client_certificate,            NULL, TYPE_CERTIFICATE_BUNDLES) \
f(SIGNATURE_ALGORITHM,           "signatureAlgorithm",         ct_string_array_t*,        signature_algorithm,           NULL, TYPE_STRING_ARRAY)        \
f(ALPN,                          "alpn",                       ct_string_array_t*,        alpn,                          NULL, TYPE_STRING_ARRAY)        \
f(SESSION_TICKET_ENCRYPTION_KEY, "sessionTicketEncryptionKey", ct_byte_array_t*,          session_ticket_encryption_key, NULL, TYPE_BYTE_ARRAY)
// clang-format on

/**
 * @brief Enumeration of all available security parameters.
 */
typedef enum { 
  get_security_parameter_array_list(output_enum) 
  get_security_parameter_scalar_list(output_enum)
  SEC_PROPERTY_END 
} ct_security_property_enum_t;




typedef struct ct_certificate_bundle_s {
  char* certificate_file_name;
  char* private_key_file_name;
} ct_certificate_bundle_t;

typedef struct ct_certificate_bundles_s {
  ct_certificate_bundle_t* certificate_bundles;
  size_t num_bundles;
} ct_certificate_bundles_t;

typedef struct ct_byte_array_s {
  uint8_t* bytes;
  size_t length;
} ct_byte_array_t;

/**
 * @brief String array value for security parameters.
 */
typedef struct {
  char** strings;       ///< Array of string pointers
  size_t num_strings;   ///< Number of strings in the array
} ct_string_array_t;

ct_string_array_t* ct_string_array_value_new(char** strings, size_t num_strings);


/**
 * @brief Union holding security parameter values.
 */
typedef union ct_sec_property_value_u {
  ct_string_array_t array_of_strings;  ///< For TYPE_STRING_ARRAY properties
  ct_certificate_bundles_t certificate_bundles; ///< For TYPE_CERTIFICATE_BUNDLES properties
  char* string;                         ///< For TYPE_STRING properties
  ct_byte_array_t byte_array;          ///< For TYPE_BYTE_ARRAY properties
} ct_sec_property_value_t;

/**
 * @brief A single security parameter.
 */
typedef struct ct_sec_property_s {
  char* name;                        ///< Parameter name string
  ct_property_type_t type;       ///< Type of value stored
  bool set_by_user;                  ///< True if user explicitly set this parameter
  ct_sec_property_value_t value;     ///< Parameter value
} ct_security_parameter_t;

/**
 * @brief Collection of all security parameters.
 */
typedef struct ct_security_parameters_s {
  ct_security_parameter_t list[SEC_PROPERTY_END];  ///< Array of security parameters
} ct_security_parameters_t;

#define create_sec_property_initializer(enum_name, string_name, property_type, token_name, default_value, type_tag) \
  [enum_name] = {                                                          \
    .name = (string_name),                                                       \
    .type = (type_tag),                                                     \
    .set_by_user = false,                                                      \
    .value = {{0}}                                                               \
},

static const ct_security_parameters_t DEFAULT_SECURITY_PARAMETERS = {
  .list = {
    get_security_parameter_array_list(create_sec_property_initializer)
    get_security_parameter_scalar_list(create_sec_property_initializer)
  }
};


// =============================================================================
// Transport Properties Internal Definitions
// =============================================================================

typedef struct ct_preference_combination_s {
  char* value;
  ct_selection_preference_t preference;
} ct_preference_combination_t;

typedef struct ct_preference_set_s {
  ct_preference_combination_t* combinations;
  size_t num_combinations;
} ct_preference_set_t;

/**
 * @brief Enumeration of all available selection properties.
 *
 * These properties control protocol selection by expressing preferences and requirements
 * for transport characteristics like reliability, ordering, and multistreaming.
 */
typedef enum { 
  get_selection_property_list(output_enum) 
  get_preference_set_selection_property_list(output_enum)
  SELECTION_PROPERTY_END
} ct_selection_property_enum_t;

/**
 * @brief Union holding the value of a selection property.
 */
typedef union {
  ct_selection_preference_t simple_preference;              ///< For TYPE_PREFERENCE properties
  ct_preference_set_t preference_set_val;                                     ///< For TYPE_PREFERENCE_SET properties
  uint32_t enum_val;
  bool bool_val;                                             ///< For TYPE_BOOLEAN properties
} ct_selection_property_value_t;

/**
 * @brief A single transport selection property.
 */
typedef struct ct_selection_property_s {
  char* name;                              ///< Property name string
  bool set_by_user;                        ///< True if user explicitly set this property
  ct_property_type_t type;
  ct_selection_property_value_t value;     ///< Property value
} ct_selection_property_t;


/**
 * @brief Collection of all transport selection properties.
 *
 * This structure contains all selection properties that influence protocol selection
 * during connection establishment. Properties are indexed by ct_selection_property_enum_t.
 */
typedef struct ct_selection_properties_s {
  ct_selection_property_t list[SELECTION_PROPERTY_END];  ///< Array of selection properties
} ct_selection_properties_t;


extern const ct_selection_properties_t DEFAULT_SELECTION_PROPERTIES;


/**
 * @brief Enumeration of all available message properties.
 */
typedef enum { get_message_property_list(output_enum) MESSAGE_PROPERTY_END } ct_message_properties_enum_t;



/**
 * @brief Union holding message property values.
 */
typedef union {
  uint32_t uint32_val;
  bool bool_val;
  uint64_t uint64_val;
  uint32_t enum_val;
} ct_message_property_value_t;

/**
 * @brief A single message property.
 */
typedef struct ct_message_property_s {
  char* name;                              ///< Property name string
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
  ct_message_property_t list[MESSAGE_PROPERTY_END];  ///< Array of message properties
} ct_message_properties_t;

// The value cast is a hack to please the c++ compiler for our tests
#define create_message_property_initializer(enum_name, string_name, property_type, token_name, default_value, type_name) \
  [enum_name] = {                                                          \
    .name = (string_name),                                                   \
    .set_by_user = false,                                                  \
    .value = { (uint32_t)(default_value) }                     \
},

static const ct_message_properties_t DEFAULT_MESSAGE_PROPERTIES = {
  .list = {
    get_message_property_list(create_message_property_initializer)
  }
};



/**
 * @brief Enumeration of all available connection properties.
 *
 * Includes writable properties (configurable), read-only properties (status),
 * and TCP-specific properties.
 */
typedef enum {
  get_writable_connection_property_list(output_enum)
  get_read_only_connection_properties(output_enum)
  get_tcp_connection_properties(output_enum)
  CONNECTION_PROPERTY_END
} ct_connection_property_enum_t;

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
  ct_property_type_t type;
  ct_connection_property_value_t value;    ///< Property value
} ct_connection_property_t;


typedef struct ct_connection_properties_s {
  ct_connection_property_t list[CONNECTION_PROPERTY_END];  ///< Array of connection properties
} ct_connection_properties_t;

extern const ct_connection_property_t DEFAULT_CONNECTION_PROPERTIES[];

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
 * @brief Connection group for managing related connections.
 */
typedef struct ct_connection_group_s {
  char connection_group_id[37];           ///< Unique identifier for this group
  GHashTable* connections;                ///< Map of UUID string → ct_connection_t*
  void* connection_group_state;           ///< Protocol-specific shared state
  size_t ref_count;                       ///< Reference count for this connection group
  ct_transport_properties_t* transport_properties;      ///< Transport and connection properties
} ct_connection_group_t;


typedef void (*ct_on_connection_close_cb)(ct_connection_t*);
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
  int (*init)(ct_connection_t* connection, const ct_connection_callbacks_t* connection_callbacks);

  /** @brief Initialize a new connection using this protocol and attempt 0-rtt. */
  int (*init_with_send)(ct_connection_t*, const ct_connection_callbacks_t*, ct_message_t*, ct_message_context_t*);

  /** @brief Send a message over the protocol. 
   *
   * @note The caller is responsible for freeing on sync errors, the protocol implementation is responsible for freeing on async errors or success.
   * @return 0 on successful send initiation, non-zero on error.
   */
  int (*send)(ct_connection_t*, ct_message_t*, ct_message_context_t*);

  /** @brief Start listening for incoming connections. */
  int (*listen)(struct ct_socket_manager_s* socket_manager);

  /** @brief Stop listening for incoming connections. */
  int (*stop_listen)(struct ct_socket_manager_s*);

  /** @brief Close a connection. */
  int (*close)(ct_connection_t*);

  int(*close_socket)(struct ct_socket_manager_s*);

  /** @brief Forcefully abort a connection without graceful shutdown. */
  void (*abort)(ct_connection_t* connection);

  /** @brief Clone a connection's protocol specific state. */
  int (*clone_connection)(const ct_connection_t* source_connection,
                          ct_connection_t* target_connection);

  /** @brief Extract remote endpoint information from a connected peer handle. */
  int (*remote_endpoint_from_peer)(uv_handle_t* peer, ct_remote_endpoint_t* resolved_peer);

  /** @brief Free protocol-specific state in a connection. */
  int (*free_connection_state)(ct_connection_t* connection);

  /** @brief Free socket-specific state in a connection. */
  int (*free_socket_state)(struct ct_socket_manager_s* socket_manager);

  int (*close_connection_group)(ct_connection_group_t* connection_group);

  int (*set_connection_priority)(ct_connection_t* connection, uint8_t priority);

  /** @brief Free protocol-specific shared state in a connection group, useful for multiplexing */
  int (*free_connection_group_state)(ct_connection_group_t* connection_group);

} ct_protocol_impl_t;

bool ct_protocol_supports_alpn(const ct_protocol_impl_t* protocol_impl);

extern const ct_protocol_impl_t* const ct_supported_protocols[];

extern const size_t ct_num_protocols;

typedef struct ct_listener_s {
  ct_transport_properties_t transport_properties;     ///< Transport properties for accepted connections
  ct_local_endpoint_t local_endpoint;                 ///< Local endpoint (listening address/port)
  size_t num_local_endpoints;                         ///< Number of local endpoints
  ct_listener_callbacks_t listener_callbacks;         ///< User-provided callbacks for listener events
  ct_listener_state_enum_t state;                     ///< Current state of the listener
  ct_security_parameters_t* security_parameters;      ///< Security configuration for accepted connections (owned copy)
  struct ct_socket_manager_s* socket_manager;         ///< Socket manager handling listening sockets
} ct_listener_t;


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

// ===================================
// Socket manager
// ===================================

typedef struct ct_socket_manager_callbacks_s {
  void (*closed_connection)(ct_connection_t* connection);
  void (*aborted_connection)(ct_connection_t* connection);
  void (*establishment_error)(ct_connection_t* connection);
  void (*connection_ready)(ct_connection_t* connection);
  void (*message_sent)(ct_connection_t* connection, ct_message_context_t* message_context);
  void (*message_send_error)(ct_connection_t* connection, ct_message_context_t* message_context, int reason_code);
  void (*socket_closed)(struct ct_socket_manager_s* socket_manager);
} ct_socket_manager_callbacks_t;


typedef struct ct_socket_manager_s {
  void* internal_socket_manager_state;
  int ref_count;                 // Number of objects using this socket (ct_listener_t + Connections)
  GSList* all_connections; // List of all ct_connection_t* using this socket manager
  GHashTable* demux_table; // remote_endpoint → ct_connection_t* (Only used for UDP where demultiplexing is needed)
  const ct_protocol_impl_t* protocol_impl;
  struct ct_listener_s* listener;
  ct_socket_manager_callbacks_t callbacks;
} ct_socket_manager_t;


// =============================================================================
// Connections
// =============================================================================

/**
 * @brief Connection role classification.
 */
typedef enum {
  CONNECTION_ROLE_CLIENT = 0,           ///< Connection initiated by local endpoint
  CONNECTION_ROLE_SERVER,               ///< Connection accepted from remote endpoint
} ct_connection_role_t;

typedef struct ct_per_connection_properties_s {
  ct_connection_state_enum_t state;
  // 8-bit because that is the resolution used by picoquic
  uint8_t priority;             ///< Relative priority of this compared to others in the same connection group, 0 is highest.
  bool can_receive;
  bool can_send;
} ct_per_connection_properties_t;

typedef struct ct_connection_s {
  char uuid[37];                                       ///< Unique identifier for this connection (UUID string)
  ct_connection_group_t* connection_group;             ///< Connection group (never NULL)
  ct_security_parameters_t* security_parameters;       ///< Security configuration (TLS/QUIC, owned copy)

  size_t num_local_endpoints;
  size_t active_local_endpoint;                        ///< index into all_local_endpoints for currently active local endpoint
  ct_local_endpoint_t* all_local_endpoints;            ///< Local endpoint (bound address/port)

  size_t num_remote_endpoints;
  size_t active_remote_endpoint;                       ///< index into all_remote_endpoints for currently active remote endpoint
  ct_remote_endpoint_t* all_remote_endpoints;
  
  ct_per_connection_properties_t properties;

  void* internal_connection_state;                     ///< Protocol-specific per-connection state (opaque)
  ct_framer_impl_t* framer_impl;                       ///< Optional message framer (NULL = no framing)
  ct_connection_role_t role;                           ///< Connection role (client/server)

  ct_connection_callbacks_t connection_callbacks;      ///< User-provided callbacks for events

  struct ct_socket_manager_s* socket_manager;          ///< Socket manager

  GQueue* received_callbacks;                          ///< Queue of pending receive callbacks
  GQueue* received_messages;                           ///< Queue of received messages

  bool sent_early_data;                                ///< True if 0-RTT was used for this connection and we sent early data
} ct_connection_t;


#endif

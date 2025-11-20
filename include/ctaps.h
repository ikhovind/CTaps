//
// CTaps - C implementation of RFC 9622 Transport Services API
//
// This is the main header file for the CTaps library.
// Include this file to access the complete Transport Services API.
//

#ifndef CTAPS_H
#define CTAPS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <glib.h>
#include <uv.h>

// Symbol visibility control - only export public API functions
#if defined(__GNUC__) || defined(__clang__)
#  define CT_EXTERN __attribute__((visibility("default")))
#else
#  define CT_EXTERN
#endif

// =============================================================================
// Library State and Configuration
// =============================================================================

typedef struct ct_config_s {
  char* cert_file_name;
  char* key_file_name;
} ct_config_t;

extern ct_config_t global_config;
extern uv_loop_t* event_loop;

CT_EXTERN int ct_initialize(const char *cert_file_name, const char *key_file_name);
CT_EXTERN void ct_start_event_loop();
CT_EXTERN int ct_close();

// =============================================================================
// Selection Properties - Transport property preferences for protocol selection
// =============================================================================

typedef enum {
  PROHIBIT = -2,
  AVOID,
  NO_PREFERENCE,
  PREFER,
  REQUIRE,
} ct_selection_preference_t;

typedef enum ct_property_type_t {
  TYPE_PREFERENCE,
  TYPE_PREFERENCE_SET,
  TYPE_MULTIPATH_ENUM,
  TYPE_BOOLEAN,
  TYPE_DIRECTION_ENUM
} ct_property_type_t;

typedef enum ct_direction_of_communication_t {
  DIRECTION_BIDIRECTIONAL,
  DIRECTION_UNIDIRECTIONAL_SEND,
  DIRECTION_UNIDIRECTIONAL_RECV
} ct_direction_of_communication_enum_t;

typedef enum ct_multipath_enum_t {
  MULTIPATH_DISABLED,
  MULTIPATH_ACTIVE,
  MULTIPATH_PASSIVE
} ct_multipath_enum_t;

typedef union {
  ct_selection_preference_t simple_preference;
  void* preference_map;
  ct_multipath_enum_t multipath_enum;
  bool boolean;
  ct_direction_of_communication_enum_t direction_enum;
} ct_selection_property_value_t;

typedef struct ct_selection_property_s {
  char* name;
  ct_property_type_t type;
  bool set_by_user;
  ct_selection_property_value_t value;
} ct_selection_property_t;

#define EMPTY_PREFERENCE_SET_DEFAULT NO_PREFERENCE
#define RUNTIME_DEPENDENT_DEFAULT NO_PREFERENCE

// clang-format off
#define get_selection_property_list(f)                                                                    \
  f(RELIABILITY,                 "reliability",                TYPE_PREFERENCE,           REQUIRE)        \
  f(PRESERVE_MSG_BOUNDARIES,     "preserveMsgBoundaries",      TYPE_PREFERENCE,           NO_PREFERENCE)  \
  f(PER_MSG_RELIABILITY,         "perMsgReliability",          TYPE_PREFERENCE,           NO_PREFERENCE)  \
  f(PRESERVE_ORDER,              "preserveOrder",              TYPE_PREFERENCE,           REQUIRE)        \
  f(ZERO_RTT_MSG,                "zeroRttMsg",                 TYPE_PREFERENCE,           NO_PREFERENCE)  \
  f(MULTISTREAMING,              "multistreaming",             TYPE_PREFERENCE,           PREFER)         \
  f(FULL_CHECKSUM_SEND,          "fullChecksumSend",           TYPE_PREFERENCE,           REQUIRE)        \
  f(FULL_CHECKSUM_RECV,          "fullChecksumRecv",           TYPE_PREFERENCE,           REQUIRE)        \
  f(CONGESTION_CONTROL,          "congestionControl",          TYPE_PREFERENCE,           REQUIRE)        \
  f(KEEP_ALIVE,                  "keepAlive",                  TYPE_PREFERENCE,           NO_PREFERENCE)  \
  f(INTERFACE,                   "interface",                  TYPE_PREFERENCE_SET,       EMPTY_PREFERENCE_SET_DEFAULT)  \
  f(PVD,                         "pvd",                        TYPE_PREFERENCE_SET,       EMPTY_PREFERENCE_SET_DEFAULT)  \
  f(USE_TEMPORARY_LOCAL_ADDRESS, "useTemporaryLocalAddress",   TYPE_PREFERENCE,           RUNTIME_DEPENDENT_DEFAULT)         \
  f(MULTIPATH,                   "multipath",                  TYPE_MULTIPATH_ENUM,       RUNTIME_DEPENDENT_DEFAULT)  \
  f(ADVERTISES_ALT_ADDRES,       "advertisesAltAddr",          TYPE_BOOLEAN,              NO_PREFERENCE)  \
  f(DIRECTION,                   "direction",                  TYPE_DIRECTION_ENUM,       DIRECTION_BIDIRECTIONAL)  \
  f(SOFT_ERROR_NOTIFY,           "softErrorNotify",            TYPE_PREFERENCE,           NO_PREFERENCE)  \
  f(ACTIVE_READ_BEFORE_SEND,     "activeReadBeforeSend",       TYPE_PREFERENCE,           NO_PREFERENCE)
// clang-format on

#define output_enum(enum_name, string_name, property_type, default_value) enum_name,

typedef enum { get_selection_property_list(output_enum) SELECTION_PROPERTY_END } ct_selection_property_enum_t;

typedef struct {
  ct_selection_property_t selection_property[SELECTION_PROPERTY_END];
} ct_selection_properties_t;

// The value cast is a hack to please the c++ compiler for our tests
#define create_sel_property_initializer(enum_name, string_name, property_type, default_value) \
  [enum_name] = {                                                          \
    .name = string_name,                                                   \
    .type = property_type,                                                 \
    .set_by_user = false,                                                  \
    .value = { (ct_selection_preference_t)default_value }                     \
},

const static ct_selection_properties_t DEFAULT_SELECTION_PROPERTIES = {
  .selection_property = {
    get_selection_property_list(create_sel_property_initializer)
  }
};

CT_EXTERN void ct_selection_properties_build(ct_selection_properties_t* selection_properties);

CT_EXTERN void ct_set_sel_prop_preference(ct_selection_properties_t* props, ct_selection_property_enum_t prop_enum, ct_selection_preference_t val);

CT_EXTERN void ct_set_sel_prop_direction(ct_selection_properties_t* props, ct_selection_property_enum_t prop_enum, ct_direction_of_communication_enum_t val);

CT_EXTERN void ct_set_sel_prop_multipath(ct_selection_properties_t* props, ct_selection_property_enum_t prop_enum, ct_multipath_enum_t val);

CT_EXTERN void ct_set_sel_prop_bool(ct_selection_properties_t* props, ct_selection_property_enum_t prop_enum, bool val);

CT_EXTERN void ct_set_sel_prop_interface(ct_selection_properties_t* props, const char* interface_name, ct_selection_preference_t preference);

// =============================================================================
// Connection Properties - Properties of active connections
// =============================================================================

#define CONN_TIMEOUT_DISABLED UINT32_MAX
#define CONN_RATE_UNLIMITED UINT64_MAX
#define CONN_CHECKSUM_FULL_COVERAGE UINT32_MAX
#define CONN_MSG_MAX_LEN_NOT_APPLICABLE 0

#define output_con_enum(enum_name, string_name, property_type, default_value) enum_name,

typedef enum {
  CONN_STATE_ESTABLISHING = 0,
  CONN_STATE_ESTABLISHED,
  CONN_STATE_CLOSING,
  CONN_STATE_CLOSED
} ct_connection_state_enum_t;

typedef enum {
  CONN_SCHEDULER_WEIGHTED_FAIR_QUEUEING = 0,
} ct_connection_scheduler_enum_t;

typedef enum {
  CAPACITY_PROFILE_BEST_EFFORT = 0,
  CAPACITY_PROFILE_SCAVENGER,
  CAPACITY_PROFILE_LOW_LATENCY_INTERACTIVE,
  CAPACITY_PROFILE_LOW_LATENCY_NON_INTERACTIVE,
  CAPACITY_PROFILE_CONSTANT_RATE_STREAMING,
  CAPACITY_PROFILE_CAPACITY_SEEKING
} ct_capacity_profile_enum_t;

typedef enum {
  MULTIPATH_POLICY_HANDOVER = 0,
  MULTIPATH_POLICY_INTERACTIVE,
  MULTIPATH_POLICY_AGGREGATE
} ct_multipath_policy_enum_t;

typedef union {
  uint32_t uint32_val;
  uint64_t uint64_val;
  bool bool_val;
  int enum_val;
} ct_connection_property_value_t;

typedef struct ct_connection_property_s {
  char* name;
  bool read_only;
  ct_connection_property_value_t value;
} ct_connection_property_t;

// clang-format off
#define get_writable_connection_property_list(f)                                                                    \
f(RECV_CHECKSUM_LEN,          "recvChecksumLen",          uint32_t,                CONN_CHECKSUM_FULL_COVERAGE)        \
f(CONN_PRIORITY,              "connPriority",             uint32_t,                100)                                \
f(CONN_TIMEOUT,               "connTimeout",              uint32_t,                CONN_TIMEOUT_DISABLED)              \
f(KEEP_ALIVE_TIMEOUT,         "keepAliveTimeout",         uint32_t,                CONN_TIMEOUT_DISABLED)              \
f(CONN_SCHEDULER,             "connScheduler",            ct_connection_scheduler_enum_t, CONN_SCHEDULER_WEIGHTED_FAIR_QUEUEING) \
f(CONN_CAPACITY_PROFILE,      "connCapacityProfile",      ct_capacity_profile_enum_t,     CAPACITY_PROFILE_BEST_EFFORT)           \
f(MULTIPATH_POLICY,           "multipathPolicy",          ct_multipath_policy_enum_t,     MULTIPATH_POLICY_HANDOVER)          \
f(MIN_SEND_RATE,              "minSendRate",              uint64_t,                CONN_RATE_UNLIMITED)                \
f(MIN_RECV_RATE,              "minRecvRate",              uint64_t,                CONN_RATE_UNLIMITED)                \
f(MAX_SEND_RATE,              "maxSendRate",              uint64_t,                CONN_RATE_UNLIMITED)                \
f(MAX_RECV_RATE,              "maxRecvRate",              uint64_t,                CONN_RATE_UNLIMITED)                \
f(GROUP_CONN_LIMIT,           "groupConnLimit",           uint64_t,                CONN_RATE_UNLIMITED)                \
f(ISOLATE_SESSION,            "isolateSession",           bool,                    false)

#define get_read_only_connection_properties(f)                                                                \
f(STATE,                               "state",                               ct_connection_state_enum_t, 0)        \
f(CAN_SEND,                            "canSend",                             bool,                0)        \
f(CAN_RECEIVE,                         "canReceive",                          bool,                0)        \
f(SINGULAR_TRANSMISSION_MSG_MAX_LEN,   "singularTransmissionMsgMaxLen",       uint64_t,            0)        \
f(SEND_MESSAGE_MAX_LEN,                "sendMsgMaxLen",                       uint64_t,            0)        \
f(RECV_MESSAGE_MAX_LEN,                "recvMessageMaxLen",                   uint64_t,            0)

#define get_tcp_connection_properties(f)                                                                \
f(USER_TIMEOUT_VALUE_MS,        "userTimeoutValueMs",      uint32_t, TCP_USER_TIMEOUT)        \
f(USER_TIMEOUT_ENABLED,         "userTimeoutEnabled",      bool,     false)        \
f(USER_TIMEOUT_CHANGEABLE,      "userTimeoutChangeable",   bool,     true)
// clang-format on

typedef enum {
  get_writable_connection_property_list(output_con_enum)
  get_read_only_connection_properties(output_con_enum)
  get_tcp_connection_properties(output_con_enum)
  CONNECTION_PROPERTY_END
} ct_connection_property_enum_t;

typedef struct {
  ct_connection_property_t list[CONNECTION_PROPERTY_END];
} ct_connection_properties_t;

#define create_con_property_initializer(enum_name, string_name, property_type, default_value) \
  [enum_name] = {                                                          \
    .name = string_name,                                                   \
    .value = { (uint32_t)default_value }                     \
},

static ct_connection_property_t DEFAULT_CONNECTION_PROPERTIES[] = {
    get_writable_connection_property_list(create_con_property_initializer)
    get_read_only_connection_properties(create_con_property_initializer)
    get_tcp_connection_properties(create_con_property_initializer)
};


// =============================================================================
// Message Properties - Properties for individual messages
// =============================================================================

typedef enum ct_message_property_type_t {
  TYPE_INTEGER_MSG,
  TYPE_BOOLEAN_MSG,
  TYPE_UINT64_MSG,
  TYPE_ENUM_MSG
} ct_message_property_type_t;

typedef union {
  uint64_t uint64_value;
  uint32_t integer_value;
  bool boolean_value;
  ct_capacity_profile_enum_t enum_value;
} ct_message_property_value_t;

typedef struct ct_message_property_s {
  char* name;
  ct_message_property_type_t type;
  bool set_by_user;
  ct_message_property_value_t value;
} ct_message_property_t;

// clang-format off
#define get_message_property_list(f)                                                                    \
  f(MSG_LIFETIME,           "msgLifetime",          TYPE_UINT64_MSG,   0)                  \
  f(MSG_PRIORITY,           "msgPriority",          TYPE_INTEGER_MSG,  100)                \
  f(MSG_ORDERED,            "msgOrdered",           TYPE_BOOLEAN_MSG,  true)               \
  f(MSG_SAFELY_REPLAYABLE,  "msgSafelyReplayable",  TYPE_BOOLEAN_MSG,  false)              \
  f(FINAL,                  "final",                TYPE_BOOLEAN_MSG,  false)              \
  f(MSG_CHECKSUM_LEN,       "msgChecksumLen",       TYPE_INTEGER_MSG,  0)                  \
  f(MSG_RELIABLE,           "msgReliable",          TYPE_BOOLEAN_MSG,  true)               \
  f(MSG_CAPACITY_PROFILE,   "msgCapacityProfile",   TYPE_ENUM_MSG,     CAPACITY_PROFILE_BEST_EFFORT)    \
  f(NO_FRAGMENTATION,       "noFragmentation",      TYPE_BOOLEAN_MSG,  false)              \
  f(NO_SEGMENTATION,        "noSegmentation",       TYPE_BOOLEAN_MSG,  false)
// clang-format on

typedef enum { get_message_property_list(output_enum) MESSAGE_PROPERTY_END } ct_message_property_enum_t;

typedef struct {
  ct_message_property_t message_property[MESSAGE_PROPERTY_END];
} ct_message_properties_t;

// =============================================================================
// Transport Properties - Combination of selection and connection properties
// =============================================================================

typedef struct {
  ct_selection_properties_t selection_properties;
  ct_connection_properties_t connection_properties;
} ct_transport_properties_t;

// =============================================================================
// Security Parameters
// =============================================================================

typedef enum ct_sec_property_type_t {
  TYPE_STRING_ARRAY,
} ct_sec_property_type_t;

typedef struct {
  char** strings;
  size_t num_strings;
} ct_string_array_value_t;

typedef union {
  ct_string_array_value_t array_of_strings;
} ct_sec_property_value_t;

typedef struct ct_sec_property_s {
  char* name;
  ct_sec_property_type_t type;
  bool set_by_user;
  ct_sec_property_value_t value;
} ct_security_parameter_t;

// clang-format off
#define get_security_parameter_list(f)                                                                    \
  f(SUPPORTED_GROUP,    "supportedGroup",    TYPE_STRING_ARRAY)                  \
  f(CIPHERSUITE,        "ciphersuite",       TYPE_STRING_ARRAY)                  \
  f(SIGNATURE_ALGORITHM,"signatureAlgorithm",TYPE_STRING_ARRAY)                  \
  f(ALPN,               "alpn",              TYPE_STRING_ARRAY)
// clang-format on

#define output_sec_enum(enum_name, string_name, property_type) enum_name,

typedef enum { get_security_parameter_list(output_sec_enum) SEC_PROPERTY_END } ct_security_property_enum_t;

typedef struct {
  ct_security_parameter_t security_parameters[SEC_PROPERTY_END];
} ct_security_parameters_t;

CT_EXTERN void ct_transport_properties_build(ct_transport_properties_t* properties);

CT_EXTERN void ct_tp_set_sel_prop_preference(ct_transport_properties_t* props, ct_selection_property_enum_t prop_enum, ct_selection_preference_t val);

CT_EXTERN void ct_tp_set_sel_prop_multipath(ct_transport_properties_t* props, ct_selection_property_enum_t prop_enum, ct_multipath_enum_t val);

CT_EXTERN void ct_tp_set_sel_prop_direction(ct_transport_properties_t* props, ct_selection_property_enum_t prop_enum, ct_direction_of_communication_enum_t val);

CT_EXTERN void ct_tp_set_sel_prop_bool(ct_transport_properties_t* props, ct_selection_property_enum_t prop_enum, bool val);

CT_EXTERN void ct_tp_set_sel_prop_interface(ct_transport_properties_t* props, char* interface_name, ct_selection_preference_t preference);


// =============================================================================
// Endpoints - Local and Remote endpoint definitions
// =============================================================================

typedef struct {
  uint16_t port;
  char* interface_name;
  char* service;
  union {
    struct sockaddr_storage address;
  } data;
} ct_local_endpoint_t;

typedef struct ct_remote_endpoint_s{
  uint16_t port;
  char* service;
  char* hostname;
  union {
    struct sockaddr_storage resolved_address;
  } data;
} ct_remote_endpoint_t;

// =============================================================================
// Messages - Message and message context structures
// =============================================================================

typedef struct {
  char* content;
  unsigned int length;
} ct_message_t;

typedef struct ct_message_context_s {
  ct_message_properties_t message_properties;
  ct_local_endpoint_t* local_endpoint;
  ct_remote_endpoint_t* remote_endpoint;
  void* user_receive_context;  // From ct_receive_callbacks_t - per-receive-request context
} ct_message_context_t;

// =============================================================================
// Forward Declarations
// =============================================================================

struct ct_connection_s;
struct ct_listener_s;
struct ct_socket_manager_s;

// =============================================================================
// Callbacks - Connection and Listener callback structures
// =============================================================================

typedef struct ct_receive_callbacks_s {
  int (*receive_callback)(struct ct_connection_s* connection, ct_message_t** received_message, ct_message_context_t* ctx);
  int (*receive_error)(struct ct_connection_s* connection, ct_message_context_t* ctx, const char* reason);
  int (*receive_partial)(struct ct_connection_s* connection, ct_message_t** received_message, ct_message_context_t* ctx, bool end_of_message);
  void* user_receive_context;  // Per-receive-request context
} ct_receive_callbacks_t;

typedef struct ct_connection_callbacks_s {
  int (*connection_error)(struct ct_connection_s* connection);
  int (*establishment_error)(struct ct_connection_s* connection);
  int (*expired)(struct ct_connection_s* connection);
  int (*path_change)(struct ct_connection_s* connection);
  int (*ready)(struct ct_connection_s* connection);
  int (*send_error)(struct ct_connection_s* connection);
  int (*sent)(struct ct_connection_s* connection);
  int (*soft_error)(struct ct_connection_s* connection);
  void* user_connection_context;  // Connection lifetime context
} ct_connection_callbacks_t;

typedef struct ct_listener_callbacks_s {
  int (*connection_received)(struct ct_listener_s* listener, struct ct_connection_s* new_conn);
  int (*establishment_error)(struct ct_listener_s* listener, const char* reason);
  int (*stopped)(struct ct_listener_s* listener);
  void* user_listener_context;  // Listener lifetime context
} ct_listener_callbacks_t;

// =============================================================================
// Message Framer - Optional message framing/parsing layer
// =============================================================================

// Forward declaration
typedef struct ct_framer_impl_s ct_framer_impl_t;

// Callback invoked by framer when encoding is complete
typedef void (*ct_framer_done_encoding_callback)(struct ct_connection_s* connection,
                                                  ct_message_t* encoded_message,
                                                  ct_message_context_t* context);

typedef void (*ct_framer_done_decoding_callback)(struct ct_connection_s* connection,
                                                  ct_message_t* encoded_message,
                                                  ct_message_context_t* context);

// Message Framer Implementation Interface
typedef struct ct_framer_impl_s {
  // Encode outbound message
  // Implementation should call the callback when encoding is complete
  void (*encode_message)(struct ct_connection_s* connection,
                        ct_message_t* message,
                        ct_message_context_t* context,
                        ct_framer_done_encoding_callback callback);

  // Decode inbound data into messages
  // Implementation should call ct_connection_deliver_to_app() for each complete message
  void (*decode_data)(struct ct_connection_s* connection,
                     const void* data,
                     size_t len,
                     ct_framer_done_decoding_callback callback);
} ct_framer_impl_t;

// =============================================================================
// Protocol Interface - Protocol implementation abstraction
// =============================================================================

typedef struct ct_protocol_impl_s {
  const char* name;
  ct_selection_properties_t selection_properties;
  int (*init)(struct ct_connection_s* connection, const ct_connection_callbacks_t* connection_callbacks);
  int (*send)(struct ct_connection_s*, ct_message_t*, ct_message_context_t*);
  int (*listen)(struct ct_socket_manager_s* socket_manager);
  int (*stop_listen)(struct ct_socket_manager_s*);
  int (*close)(const struct ct_connection_s*);
  int (*remote_endpoint_from_peer)(uv_handle_t* peer, ct_remote_endpoint_t* resolved_peer);
  void (*retarget_protocol_connection)(struct ct_connection_s* from_connection, struct ct_connection_s* to_connection);
} ct_protocol_impl_t;

// =============================================================================
// Connections - Main connection structures
// =============================================================================

typedef enum {
  CONNECTION_TYPE_STANDALONE = 0,
  CONNECTION_OPEN_TYPE_MULTIPLEXED,
} ct_connection_type_t;

typedef struct ct_connection_s {
  ct_transport_properties_t transport_properties;
  const ct_security_parameters_t* security_parameters;
  ct_local_endpoint_t local_endpoint;
  ct_remote_endpoint_t remote_endpoint;
  ct_protocol_impl_t protocol;
  void* protocol_state;
  ct_framer_impl_t* framer_impl;  // NULL = no framing (passthrough)
  ct_connection_type_t open_type;
  ct_connection_callbacks_t connection_callbacks;
  struct ct_socket_manager_s* socket_manager;
  GQueue* received_callbacks;
  GQueue* received_messages;
} ct_connection_t;

typedef struct ct_preconnection_s {
  ct_transport_properties_t transport_properties;
  const ct_security_parameters_t* security_parameters;
  ct_local_endpoint_t local;
  size_t num_local_endpoints;
  ct_remote_endpoint_t* remote_endpoints;
  size_t num_remote_endpoints;
  ct_framer_impl_t* framer_impl;  // Optional message framer
} ct_preconnection_t;

typedef struct ct_listener_s {
  ct_transport_properties_t transport_properties;
  ct_local_endpoint_t local_endpoint;
  size_t num_local_endpoints;
  ct_listener_callbacks_t listener_callbacks;
  const ct_security_parameters_t* security_parameters;
  struct ct_socket_manager_s* socket_manager;
} ct_listener_t;

// =============================================================================
// Public API Functions
// =============================================================================

// Selection Properties
CT_EXTERN void ct_selection_properties_build(ct_selection_properties_t* selection_properties);
CT_EXTERN void ct_selection_properties_set(ct_selection_properties_t* selection_properties, ct_selection_property_enum_t property, ct_selection_property_value_t value);
CT_EXTERN void ct_selection_properties_free(ct_selection_properties_t* selection_properties);

// Connection Properties
CT_EXTERN void ct_connection_properties_build(ct_connection_properties_t* connection_properties);
CT_EXTERN void ct_connection_properties_free(ct_connection_properties_t* connection_properties);

// Message Properties
CT_EXTERN void ct_message_properties_build(ct_message_properties_t* message_properties);
CT_EXTERN void ct_message_properties_free(ct_message_properties_t* message_properties);

// Transport Properties
CT_EXTERN void ct_transport_properties_build(ct_transport_properties_t* transport_properties);
CT_EXTERN void ct_transport_properties_free(ct_transport_properties_t* transport_properties);

// Security Parameters
CT_EXTERN void ct_security_parameters_build(ct_security_parameters_t* security_parameters);
CT_EXTERN void ct_security_parameters_free(ct_security_parameters_t* security_parameters);

CT_EXTERN int ct_sec_param_set_property_string_array(ct_security_parameters_t* security_parameters, ct_security_property_enum_t property, char** strings, size_t num_strings);

CT_EXTERN void ct_free_security_parameter_content(ct_security_parameters_t* security_parameters);

// Local Endpoint
CT_EXTERN void ct_local_endpoint_build(ct_local_endpoint_t* local_endpoint);
CT_EXTERN int ct_local_endpoint_with_interface(ct_local_endpoint_t* local_endpoint, const char* interface_name);
CT_EXTERN void ct_local_endpoint_with_port(ct_local_endpoint_t* local_endpoint, int port);
CT_EXTERN int ct_local_endpoint_with_service(ct_local_endpoint_t* local_endpoint, char* service);
CT_EXTERN void ct_free_local_endpoint(ct_local_endpoint_t* local_endpoint);
CT_EXTERN void ct_free_local_endpoint_strings(ct_local_endpoint_t* local_endpoint);
ct_local_endpoint_t* local_endpoint_copy(const ct_local_endpoint_t* local_endpoint);
ct_local_endpoint_t ct_local_endpoint_copy_content(const ct_local_endpoint_t* local_endpoint);
CT_EXTERN int ct_local_endpoint_resolve(const ct_local_endpoint_t* local_endpoint, ct_local_endpoint_t** out_list, size_t* out_count);


// Remote Endpoint
CT_EXTERN void ct_remote_endpoint_build(ct_remote_endpoint_t* remote_endpoint);
CT_EXTERN int ct_remote_endpoint_with_hostname(ct_remote_endpoint_t* remote_endpoint, const char* hostname);
CT_EXTERN void ct_remote_endpoint_with_port(ct_remote_endpoint_t* remote_endpoint, uint16_t port);
CT_EXTERN int ct_remote_endpoint_with_service(ct_remote_endpoint_t* remote_endpoint, const char* service);
CT_EXTERN void ct_free_remote_endpoint_strings(ct_remote_endpoint_t* remote_endpoint);
CT_EXTERN void ct_free_remote_endpoint(ct_remote_endpoint_t* remote_endpoint);
CT_EXTERN int ct_remote_endpoint_from_sockaddr(ct_remote_endpoint_t* remote_endpoint, const struct sockaddr_storage* addr);
CT_EXTERN int ct_remote_endpoint_resolve(const ct_remote_endpoint_t* remote_endpoint, ct_remote_endpoint_t** out_list, size_t* out_count);
ct_remote_endpoint_t* remote_endpoint_copy(const ct_remote_endpoint_t* remote_endpoint);
ct_remote_endpoint_t ct_remote_endpoint_copy_content(const ct_remote_endpoint_t* remote_endpoint);
CT_EXTERN int ct_remote_endpoint_with_ipv4(ct_remote_endpoint_t* remote_endpoint, in_addr_t ipv4_addr);
CT_EXTERN int ct_remote_endpoint_with_ipv6(ct_remote_endpoint_t* remote_endpoint, struct in6_addr ipv6_addr);

// Message
CT_EXTERN void ct_message_build_with_content(ct_message_t* message, const char* content, size_t length);
CT_EXTERN void ct_message_free_content(const ct_message_t* message);

CT_EXTERN void ct_message_free_all(ct_message_t* message);
CT_EXTERN void ct_message_build_without_content(ct_message_t* message);

// Message Context
CT_EXTERN void ct_message_context_build(ct_message_context_t* message_context);
CT_EXTERN void ct_message_context_free(ct_message_context_t* message_context);

// Preconnection
CT_EXTERN void ct_preconnection_build_user_connection(ct_connection_t* connection, const ct_preconnection_t* preconnection, ct_connection_callbacks_t connection_callbacks);
CT_EXTERN int ct_preconnection_build(ct_preconnection_t* preconnection,
                           const ct_transport_properties_t transport_properties,
                           const ct_remote_endpoint_t* remote_endpoints,
                           const size_t num_remote_endpoints,
                           const ct_security_parameters_t* security_parameters);
CT_EXTERN int ct_preconnection_build_ex(ct_preconnection_t* preconnection,
                           const ct_transport_properties_t transport_properties,
                           const ct_remote_endpoint_t* remote_endpoints,
                           const size_t num_remote_endpoints,
                           const ct_security_parameters_t* security_parameters,
                           ct_framer_impl_t* framer_impl);
CT_EXTERN int ct_preconnection_build_with_local(ct_preconnection_t* preconnection,
                                      ct_transport_properties_t transport_properties,
                                      ct_remote_endpoint_t remote_endpoints[],
                                      size_t num_remote_endpoints,
                                      const ct_security_parameters_t* security_parameters,
                                      ct_local_endpoint_t local_endpoint);
CT_EXTERN void ct_preconnection_add_remote_endpoint(ct_preconnection_t* preconnection, const ct_remote_endpoint_t* remote_endpoint);
CT_EXTERN void ct_preconnection_set_local_endpoint(ct_preconnection_t* preconnection, const ct_local_endpoint_t* local_endpoint);
CT_EXTERN void ct_preconnection_free(ct_preconnection_t* preconnection);
CT_EXTERN int ct_preconnection_initiate(ct_preconnection_t* preconnection, ct_connection_t* connection, ct_connection_callbacks_t connection_callbacks);
CT_EXTERN int ct_preconnection_listen(ct_preconnection_t* preconnection, ct_listener_t* listener, ct_listener_callbacks_t listener_callbacks);

// Connection
CT_EXTERN int ct_send_message(ct_connection_t* connection, ct_message_t* message);
CT_EXTERN int ct_send_message_full(ct_connection_t* connection, ct_message_t* message, ct_message_context_t* message_context);
CT_EXTERN int ct_receive_message(ct_connection_t* connection, ct_receive_callbacks_t receive_callbacks);
CT_EXTERN void ct_connection_build_multiplexed(ct_connection_t* connection, const struct ct_listener_s* listener, const ct_remote_endpoint_t* remote_endpoint);
CT_EXTERN ct_connection_t* ct_connection_build_from_received_handle(const struct ct_listener_s* listener, uv_stream_t* received_handle);
CT_EXTERN void ct_connection_build(ct_connection_t* connection);
CT_EXTERN void ct_connection_free(ct_connection_t* connection);
CT_EXTERN void ct_connection_close(ct_connection_t* connection);


// External because the user may want to implement their own protocol
CT_EXTERN void ct_connection_on_protocol_receive(ct_connection_t* connection,
                                       const void* data,
                                       size_t len);

// Listener
CT_EXTERN void ct_listener_stop(ct_listener_t* listener);
CT_EXTERN void ct_listener_close(const ct_listener_t* listener);
CT_EXTERN void ct_listener_free(ct_listener_t* listener);
ct_local_endpoint_t ct_listener_get_local_endpoint(const ct_listener_t* listener);

// Protocol Registry
CT_EXTERN void ct_protocol_registry_build();
CT_EXTERN void ct_protocol_registry_free();
#define MAX_PROTOCOLS 256

// A dynamic list to hold registered protocols
static const ct_protocol_impl_t* ct_supported_protocols[MAX_PROTOCOLS] = {0};

CT_EXTERN void ct_register_protocol(ct_protocol_impl_t* proto);

const ct_protocol_impl_t** ct_get_supported_protocols();

size_t ct_get_num_protocols();



#endif  // CTAPS_H

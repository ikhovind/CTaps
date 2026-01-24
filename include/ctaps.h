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

typedef struct ct_config_s ct_config_t;

extern uv_loop_t* event_loop;

/**
 * @brief Active connection object.
 *
 * Represents an established or establishing connection. Created via ct_preconnection_initiate()
 * for client connections or via listener callbacks for server connections.
 */
typedef struct ct_connection_s ct_connection_t;

/**
 * @brief Preconnection configuration object.
 *
 * Created before establishing a connection, this object holds all configuration
 * (endpoints, properties, security) needed to initiate a connection or start a listener.
 * This is the RFC 9622 "Preconnection" abstraction.
 *
 * This is an opaque type. Use ct_preconnection_new() to create instances.
 */
typedef struct ct_preconnection_s ct_preconnection_t;

/**
 * @brief Listener object for accepting incoming connections.
 *
 * Created via ct_preconnection_listen(). Accepts incoming connections and
 * invokes callbacks when new connections arrive.
 */
typedef struct ct_listener_s ct_listener_t;

/**
 * @brief Initialize the CTaps library
 *
 * This function must be called before any other CTaps functions. It initializes
 * the library state, and sets up the protocol registry.
 *
 * @return 0 on success
 * @return Non-zero error code on failure
 *
 * @note Must be called before ct_start_event_loop()
 * @note This function initializes global state and is not thread-safe
 *
 * @see ct_start_event_loop() for starting the event loop after initialization
 * @see ct_close() for cleanup and shutdown
 */
CT_EXTERN int ct_initialize(void);

/**
 * @brief Start the CTaps event loop (blocking operation).
 *
 * @note This is a blocking function - it will not return until the event loop is stopped
 * @note Must be called after ct_initialize()
 * @note All CTaps callbacks are invoked from within this event loop's thread context
 *
 * @see ct_initialize() for library initialization
 */
CT_EXTERN void ct_start_event_loop();

/**
 * @brief Close and cleanup the CTaps library.
 *
 * @return 0 on success
 * @return Non-zero error code on failure
 *
 * @see ct_initialize() for re-initializing the library
 */
CT_EXTERN int ct_close();

// =============================================================================
// Logging Configuration
// =============================================================================

/**
 * @brief Log level enumeration for filtering log output.
 *
 * Log levels range from TRACE (most verbose) to FATAL (critical errors only).
 * Setting a log level filters out all messages below that level.
 */
typedef enum {
  CT_LOG_TRACE = 0,  ///< Trace-level debugging (most verbose)
  CT_LOG_DEBUG = 1,  ///< Debug-level information
  CT_LOG_INFO = 2,   ///< Informational messages (default)
  CT_LOG_WARN = 3,   ///< Warning messages
  CT_LOG_ERROR = 4,  ///< Error messages
  CT_LOG_FATAL = 5   ///< Fatal errors (least verbose)
} ct_log_level_t;

/**
 * @brief Set the minimum logging level for CTaps.
 *
 * Only log messages at or above this level will be output. By default,
 * CTaps logs at CT_LOG_INFO level and above.
 *
 * @param[in] level Minimum log level (CT_LOG_TRACE through CT_LOG_FATAL)
 *
 * @note This can be called before ct_initialize() or at any time during execution
 * @note Lower numeric values are more verbose (TRACE=0, FATAL=5)
 *
 * @see ct_log_level_t for available log levels
 */
CT_EXTERN void ct_set_log_level(ct_log_level_t level);

/**
 * @brief Add a file output destination for CTaps logs.
 *
 * Logs will be written to the specified file in addition to stderr.
 * Multiple files can be added, each with their own minimum log level.
 *
 * @param[in] file_path Path to the log file (will be created/appended)
 * @param[in] min_level Minimum log level to write to this file
 *
 * @return 0 on success
 * @return Non-zero error code if file cannot be opened
 *
 * @note The file will be opened in append mode
 * @note File handle remains open for the lifetime of the library
 * @note This should be called after ct_initialize()
 */
CT_EXTERN int ct_add_log_file(const char* file_path, ct_log_level_t min_level);

// =============================================================================
// Selection Properties - Transport property preferences for protocol selection
// =============================================================================

/**
 * @brief Preference levels for transport selection properties.
 *
 * These values express how strongly a particular transport property is desired,
 * ranging from PROHIBIT (must not have) to REQUIRE (must have).
 *
 * If a candidate cannot fulfill a PROHIBIT or REQUIRE it is pruned completely.
 *
 * If a candidate cannot fulfill an AVOID or PREFER it is placed later in
 * the order of possible candidates to race.
 *
 * @note A missing PREFER is placed later than any missing avoids. even if
 * a candidate cannot fulfill 10 AVOIDs, it will be placed before a candidate
 * which is missing only a single PREFER
 */
typedef enum {
  PROHIBIT = -2,      ///< Protocol MUST NOT have this property (eliminates candidates)
  AVOID,              ///< Prefer protocols without this property if possible
  NO_PREFERENCE,      ///< No preference - property does not affect selection
  PREFER,             ///< Prefer protocols with this property if available
  REQUIRE,            ///< Protocol MUST have this property (eliminates candidates)
} ct_selection_preference_t;

/**
 * @brief Type of value stored in a selection property.
 */
typedef enum ct_property_type_t {
  TYPE_PREFERENCE,       ///< Simple preference value (PROHIBIT through REQUIRE)
  TYPE_PREFERENCE_SET,   ///< Set of preferences (e.g., interface preferences)
  TYPE_MULTIPATH_ENUM,   ///< Multipath mode enumeration
  TYPE_BOOLEAN,          ///< Boolean flag
  TYPE_DIRECTION_ENUM    ///< Communication direction enumeration
} ct_property_type_t;

/**
 * @brief Direction of communication for a connection.
 */
typedef enum ct_direction_of_communication_t {
  DIRECTION_BIDIRECTIONAL,          ///< Two-way communication (send and receive)
  DIRECTION_UNIDIRECTIONAL_SEND,    ///< One-way, send only
  DIRECTION_UNIDIRECTIONAL_RECV     ///< One-way, receive only
} ct_direction_of_communication_enum_t;

/**
 * @brief Multipath transport modes.
 */
typedef enum ct_multipath_enum_t {
  MULTIPATH_DISABLED,  ///< Do not use multipath
  MULTIPATH_ACTIVE,    ///< Actively use multiple paths simultaneously
  MULTIPATH_PASSIVE    ///< TBD
} ct_multipath_enum_t;

/**
 * @brief Union holding the value of a selection property.
 */
typedef union {
  ct_selection_preference_t simple_preference;              ///< For TYPE_PREFERENCE properties
  void* preference_map;                                     ///< For TYPE_PREFERENCE_SET properties
  ct_multipath_enum_t multipath_enum;                       ///< For TYPE_MULTIPATH_ENUM properties
  bool boolean;                                             ///< For TYPE_BOOLEAN properties
  ct_direction_of_communication_enum_t direction_enum;      ///< For TYPE_DIRECTION_ENUM properties
} ct_selection_property_value_t;

/**
 * @brief A single transport selection property.
 */
typedef struct ct_selection_property_s {
  char* name;                              ///< Property name string
  ct_property_type_t type;                 ///< Type of value stored
  bool set_by_user;                        ///< True if user explicitly set this property
  ct_selection_property_value_t value;     ///< Property value
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

/**
 * @brief Enumeration of all available selection properties.
 *
 * These properties control protocol selection by expressing preferences and requirements
 * for transport characteristics like reliability, ordering, and multistreaming.
 */
typedef enum { get_selection_property_list(output_enum) SELECTION_PROPERTY_END } ct_selection_property_enum_t;

/**
 * @brief Collection of all transport selection properties.
 *
 * This structure contains all selection properties that influence protocol selection
 * during connection establishment. Properties are indexed by ct_selection_property_enum_t.
 *
 * This is an opaque type. Use the setter functions to configure selection properties.
 */
typedef struct ct_selection_properties_s ct_selection_properties_t;

/**
 * @brief Set a selection property preference value.
 *
 * @param[in,out] props Pointer to selection properties structure
 * @param[in] prop_enum Which property to set
 * @param[in] val Preference level (PROHIBIT through REQUIRE)
 */
CT_EXTERN void ct_set_sel_prop_preference(ct_selection_properties_t* props, ct_selection_property_enum_t prop_enum, ct_selection_preference_t val);

/**
 * @brief Set the communication direction property.
 *
 * @param[in,out] props Pointer to selection properties structure
 * @param[in] prop_enum Must be DIRECTION property
 * @param[in] val Direction value (bidirectional, send-only, or receive-only)
 */
CT_EXTERN void ct_set_sel_prop_direction(ct_selection_properties_t* props, ct_selection_property_enum_t prop_enum, ct_direction_of_communication_enum_t val);

/**
 * @brief Set the multipath mode property.
 *
 * @param[in,out] props Pointer to selection properties structure
 * @param[in] prop_enum Must be MULTIPATH property
 * @param[in] val Multipath mode (disabled, active, or passive)
 */
CT_EXTERN void ct_set_sel_prop_multipath(ct_selection_properties_t* props, ct_selection_property_enum_t prop_enum, ct_multipath_enum_t val);

/**
 * @brief Set a boolean selection property.
 *
 * @param[in,out] props Pointer to selection properties structure
 * @param[in] prop_enum Which boolean property to set
 * @param[in] val Boolean value
 */
CT_EXTERN void ct_set_sel_prop_bool(ct_selection_properties_t* props, ct_selection_property_enum_t prop_enum, bool val);

/**
 * @brief Set interface preference for protocol selection.
 *
 * @param[in,out] props Pointer to selection properties structure
 * @param[in] interface_name Name of the network interface (e.g., "eth0", "wlan0")
 * @param[in] preference Preference level for using this interface
 */
CT_EXTERN void ct_set_sel_prop_interface(ct_selection_properties_t* props, const char* interface_name, ct_selection_preference_t preference);

// =============================================================================
// Connection Properties
// =============================================================================

#define CONN_TIMEOUT_DISABLED UINT32_MAX            ///< Special value: no timeout
#define CONN_RATE_UNLIMITED UINT64_MAX              ///< Special value: no rate limit
#define CONN_CHECKSUM_FULL_COVERAGE UINT32_MAX      ///< Special value: checksum entire message
#define CONN_MSG_MAX_LEN_NOT_APPLICABLE 0           ///< Special value: no maximum length

#define output_con_enum(enum_name, string_name, property_type, default_value) enum_name,

/**
 * @brief Connection lifecycle states.
 */
typedef enum {
  CONN_STATE_ESTABLISHING = 0,  ///< Connection is being established
  CONN_STATE_ESTABLISHED,       ///< Connection is ready for data transfer
  CONN_STATE_CLOSING,           ///< Connection is closing gracefully
  CONN_STATE_CLOSED             ///< Connection is fully closed
} ct_connection_state_enum_t;

/**
 * @brief Connection scheduling algorithms for multipath.
 */
typedef enum {
  CONN_SCHEDULER_WEIGHTED_FAIR_QUEUEING = 0,  ///< Weighted fair queueing across paths
} ct_connection_scheduler_enum_t;

/**
 * @brief QoS capacity profiles for traffic classification.
 */
typedef enum {
  CAPACITY_PROFILE_BEST_EFFORT = 0,                 ///< Default best-effort traffic
  CAPACITY_PROFILE_SCAVENGER,                       ///< Background/bulk traffic
  CAPACITY_PROFILE_LOW_LATENCY_INTERACTIVE,         ///< Interactive low-latency (e.g., gaming, VoIP)
  CAPACITY_PROFILE_LOW_LATENCY_NON_INTERACTIVE,     ///< Non-interactive low-latency (e.g., streaming)
  CAPACITY_PROFILE_CONSTANT_RATE_STREAMING,         ///< Constant bitrate streaming
  CAPACITY_PROFILE_CAPACITY_SEEKING                 ///< Throughput-seeking traffic
} ct_capacity_profile_enum_t;

/**
 * @brief Policies for multipath traffic distribution.
 */
typedef enum {
  MULTIPATH_POLICY_HANDOVER = 0,  ///< Use paths sequentially (failover only)
  MULTIPATH_POLICY_INTERACTIVE,   ///< Optimize for low latency
  MULTIPATH_POLICY_AGGREGATE      ///< Use all paths for maximum throughput
} ct_multipath_policy_enum_t;

// clang-format off
#define get_writable_connection_property_list(f)                                                                    \
f(RECV_CHECKSUM_LEN,          "recvChecksumLen",          uint32_t,                       CONN_CHECKSUM_FULL_COVERAGE)        \
f(CONN_PRIORITY,              "connPriority",             uint32_t,                       100)                                \
f(CONN_TIMEOUT,               "connTimeout",              uint32_t,                       CONN_TIMEOUT_DISABLED)              \
f(KEEP_ALIVE_TIMEOUT,         "keepAliveTimeout",         uint32_t,                       CONN_TIMEOUT_DISABLED)              \
f(CONN_SCHEDULER,             "connScheduler",            ct_connection_scheduler_enum_t, CONN_SCHEDULER_WEIGHTED_FAIR_QUEUEING) \
f(CONN_CAPACITY_PROFILE,      "connCapacityProfile",      ct_capacity_profile_enum_t,     CAPACITY_PROFILE_BEST_EFFORT)           \
f(MULTIPATH_POLICY,           "multipathPolicy",          ct_multipath_policy_enum_t,     MULTIPATH_POLICY_HANDOVER)          \
f(MIN_SEND_RATE,              "minSendRate",              uint64_t,                       CONN_RATE_UNLIMITED)                \
f(MIN_RECV_RATE,              "minRecvRate",              uint64_t,                       CONN_RATE_UNLIMITED)                \
f(MAX_SEND_RATE,              "maxSendRate",              uint64_t,                       CONN_RATE_UNLIMITED)                \
f(MAX_RECV_RATE,              "maxRecvRate",              uint64_t,                       CONN_RATE_UNLIMITED)                \
f(GROUP_CONN_LIMIT,           "groupConnLimit",           uint64_t,                       CONN_RATE_UNLIMITED)                \
f(ISOLATE_SESSION,            "isolateSession",           bool,                           false)

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

/**
 * @brief Enumeration of all available connection properties.
 *
 * Includes writable properties (configurable), read-only properties (status),
 * and TCP-specific properties.
 */
typedef enum {
  get_writable_connection_property_list(output_con_enum)
  get_read_only_connection_properties(output_con_enum)
  get_tcp_connection_properties(output_con_enum)
  CONNECTION_PROPERTY_END
} ct_connection_property_enum_t;

/**
 * @brief Collection of all connection properties.
 *
 * Contains both configurable and read-only properties for an active connection.
 * Properties are indexed by ct_connection_property_enum_t.
 */
typedef struct ct_connection_properties_s ct_connection_properties_t;

// Connection property getters

CT_EXTERN uint64_t ct_connection_properties_get_recv_checksum_len(ct_connection_properties_t* conn_props);

CT_EXTERN uint32_t ct_connection_properties_get_conn_priority(ct_connection_properties_t* conn_props);

CT_EXTERN uint32_t ct_connection_properties_get_conn_timeout(ct_connection_properties_t* conn_props);

CT_EXTERN uint32_t ct_connection_properties_get_keep_alive_timeout(ct_connection_properties_t* conn_props);

CT_EXTERN ct_connection_scheduler_enum_t ct_connection_properties_get_conn_scheduler(ct_connection_properties_t* conn_props);

CT_EXTERN ct_capacity_profile_enum_t ct_connection_properties_get_conn_capacity_profile(ct_connection_properties_t* conn_props);

CT_EXTERN ct_multipath_policy_enum_t ct_connection_properties_get_multipath_policy(ct_connection_properties_t* conn_props);

CT_EXTERN uint64_t ct_connection_properties_get_min_send_rate(ct_connection_properties_t* conn_props);

CT_EXTERN uint64_t ct_connection_properties_get_min_recv_rate(ct_connection_properties_t* conn_props);

CT_EXTERN uint64_t ct_connection_properties_get_max_send_rate(ct_connection_properties_t* conn_props);

CT_EXTERN uint64_t ct_connection_properties_get_max_recv_rate(ct_connection_properties_t* conn_props);

CT_EXTERN uint64_t ct_connection_properties_get_group_conn_limit(ct_connection_properties_t* conn_props);

CT_EXTERN bool ct_connection_properties_get_isolate_session(ct_connection_properties_t* conn_props);

CT_EXTERN ct_connection_state_enum_t ct_connection_properties_get_state(ct_connection_properties_t* conn_props);

CT_EXTERN bool ct_connection_properties_get_can_send(ct_connection_properties_t* conn_props);

CT_EXTERN bool ct_connection_properties_get_can_receive(ct_connection_properties_t* conn_props);

CT_EXTERN uint64_t ct_connection_properties_get_singular_transmission_msg_max_len(ct_connection_properties_t* conn_props);

CT_EXTERN uint64_t ct_connection_properties_get_send_message_max_len(ct_connection_properties_t* conn_props);

CT_EXTERN uint64_t ct_connection_properties_get_recv_message_max_len(ct_connection_properties_t* conn_props);

CT_EXTERN uint32_t ct_connection_properties_get_user_timeout_value_ms(ct_connection_properties_t* conn_props);

CT_EXTERN bool ct_connection_properties_get_user_timeout_enabled(ct_connection_properties_t* conn_props);

CT_EXTERN bool ct_connection_properties_get_user_timeout_changeable(ct_connection_properties_t* conn_props);

// Writable connection property setters

CT_EXTERN void ct_connection_properties_set_recv_checksum_len(ct_connection_properties_t* conn_props, uint32_t recv_checksum_len);

CT_EXTERN void ct_connection_properties_set_conn_priority(ct_connection_properties_t* conn_props, uint32_t conn_priority);

CT_EXTERN void ct_connection_properties_set_conn_timeout(ct_connection_properties_t* conn_props, uint32_t conn_timeout);

CT_EXTERN void ct_connection_properties_set_keep_alive_timeout(ct_connection_properties_t* conn_props, uint32_t keep_alive_timeout);

CT_EXTERN void ct_connection_properties_set_conn_scheduler(ct_connection_properties_t* conn_props, ct_connection_scheduler_enum_t conn_scheduler);

CT_EXTERN void ct_connection_properties_set_conn_capacity_profile(ct_connection_properties_t* conn_props, ct_capacity_profile_enum_t conn_capacity_profile);

CT_EXTERN void ct_connection_properties_set_multipath_policy(ct_connection_properties_t* conn_props, ct_multipath_policy_enum_t multipath_policy);

CT_EXTERN void ct_connection_properties_set_min_send_rate(ct_connection_properties_t* conn_props, uint64_t min_send_rate);


CT_EXTERN void ct_connection_properties_set_min_recv_rate(ct_connection_properties_t* conn_props, uint64_t min_recv_rate);

CT_EXTERN void ct_connection_properties_set_max_send_rate(ct_connection_properties_t* conn_props, uint64_t max_send_rate);

CT_EXTERN void ct_connection_properties_set_max_recv_rate(ct_connection_properties_t* conn_props, uint64_t max_recv_rate);

CT_EXTERN void ct_connection_properties_set_group_conn_limit(ct_connection_properties_t* conn_props, uint64_t group_conn_limit);

CT_EXTERN void ct_connection_properties_set_isolate_session(ct_connection_properties_t* conn_props, bool isolate_session);

CT_EXTERN void ct_connection_properties_set_user_timeout_value_ms(ct_connection_properties_t* conn_props, uint32_t user_timeout_value_ms);

CT_EXTERN void ct_connection_properties_set_user_timeout_enabled(ct_connection_properties_t* conn_props, bool user_timeout_enabled);

// TODO - is user_timeout_changeable settable?



#define create_con_property_initializer(enum_name, string_name, property_type, default_value) \
  [enum_name] = {                                                          \
    .name = string_name,                                                   \
    .value = { (uint32_t)default_value }                     \
},

#define MESSAGE_CHECKSUM_FULL_COVERAGE UINT32_MAX  ///< Special value: checksum entire message

// clang-format off
#define get_message_property_list(f)                                                                    \
  f(MSG_LIFETIME,           "msgLifetime",          TYPE_UINT64_MSG,   0)                  \
  f(MSG_PRIORITY,           "msgPriority",          TYPE_UINT32_MSG,   100)                \
  f(MSG_ORDERED,            "msgOrdered",           TYPE_BOOLEAN_MSG,  true)               \
  f(MSG_SAFELY_REPLAYABLE,  "msgSafelyReplayable",  TYPE_BOOLEAN_MSG,  false)              \
  f(FINAL,                  "final",                TYPE_BOOLEAN_MSG,  false)              \
  f(MSG_CHECKSUM_LEN,       "msgChecksumLen",       TYPE_UINT32_MSG,   MESSAGE_CHECKSUM_FULL_COVERAGE)                  \
  f(MSG_RELIABLE,           "msgReliable",          TYPE_BOOLEAN_MSG,  true)               \
  f(MSG_CAPACITY_PROFILE,   "msgCapacityProfile",   TYPE_ENUM_MSG,     CAPACITY_PROFILE_BEST_EFFORT)    \
  f(NO_FRAGMENTATION,       "noFragmentation",      TYPE_BOOLEAN_MSG,  false)              \
  f(NO_SEGMENTATION,        "noSegmentation",       TYPE_BOOLEAN_MSG,  false)
// clang-format on

/**
 * @brief Enumeration of all available message properties.
 */
typedef enum { get_message_property_list(output_enum) MESSAGE_PROPERTY_END } ct_message_properties_enum_t;

// =============================================================================
// Transport Properties - Combination of selection and connection properties
// =============================================================================

/**
 * @brief Transport properties for protocol selection and connection configuration.
 *
 * This structure contains both selection properties (for choosing protocols) and
 * connection properties (for configuring active connections).
 *
 * This is an opaque type. Use ct_transport_properties_new() to create instances
 * and the setter functions to configure properties.
 */
typedef struct ct_transport_properties_s ct_transport_properties_t;

// =============================================================================
// Security Parameters
// =============================================================================

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
#define get_security_parameter_list(f)                                        \
  f(SUPPORTED_GROUP,    "supportedGroup",    TYPE_STRING_ARRAY)               \
  f(CIPHERSUITE,        "ciphersuite",       TYPE_STRING_ARRAY)               \
  f(SERVER_CERTIFICATE, "serverCertificate", TYPE_CERTIFICATE_BUNDLES)        \
  f(CLIENT_CERTIFICATE, "clientCertificate", TYPE_CERTIFICATE_BUNDLES)        \
  f(SIGNATURE_ALGORITHM,"signatureAlgorithm",TYPE_STRING_ARRAY)               \
  f(ALPN,               "alpn",              TYPE_STRING_ARRAY)               \
  f(TICKET_STORE_PATH,  "ticketStorePath",   TYPE_STRING)
// clang-format on

#define output_sec_enum(enum_name, string_name, property_type) enum_name,

/**
 * @brief Enumeration of all available security parameters.
 */
typedef enum { get_security_parameter_list(output_sec_enum) SEC_PROPERTY_END } ct_security_property_enum_t;

/**
 * @brief Collection of all security parameters.
 *
 * Opaque type - use security parameter functions to work with this structure.
 * Full definition is in ctaps_internal.h.
 *
 * ## Security Parameters Ownership Model
 *
 * ### Passing to Preconnections and Connections
 * When you pass security parameters to ct_preconnection_new() or similar functions:
 * - **You retain ownership** of your original security_parameters
 * - CTaps makes a **deep copy** internally
 * - **You can free your security_parameters** after the function returns
 * - Multiple preconnections can share the same source security_parameters safely
 *
 * ### Lifecycle
 * - Create with ct_security_parameters_new()
 * - Configure with ct_sec_param_set_property_string_array()
 * - Pass to preconnection/connection functions (CTaps deep copies internally)
 * - Free your copy with ct_security_parameters_free() when done
 * - CTaps-owned copies are freed automatically when preconnections/connections are freed
 */
typedef struct ct_security_parameters_s ct_security_parameters_t;


/**
 * @brief Create a new transport properties object with default values.
 *
 * Allocates and initializes a new transport properties object on the heap.
 * The returned object must be freed with ct_transport_properties_free().
 *
 * @return Pointer to newly allocated transport properties, or NULL on allocation failure.
 */
CT_EXTERN ct_transport_properties_t* ct_transport_properties_new(void);

/**
 * @brief Free a transport properties object.
 *
 * Releases all resources associated with the transport properties object,
 * including any dynamically allocated internal state (e.g., interface preference maps).
 * After calling this function, the pointer is invalid and must not be used.
 *
 * @param[in] props Pointer to transport properties to free. Does nothing if NULL.
 */
CT_EXTERN void ct_transport_properties_free(ct_transport_properties_t* props);

/**
 * @brief Set a selection property preference in transport properties.
 *
 * @param[in,out] props Pointer to transport properties structure
 * @param[in] prop_enum Which selection property to set
 * @param[in] val Preference level (PROHIBIT through REQUIRE)
 */
CT_EXTERN void ct_tp_set_sel_prop_preference(ct_transport_properties_t* props, ct_selection_property_enum_t prop_enum, ct_selection_preference_t val);

/**
 * @brief Set the multipath mode in transport properties.
 *
 * @param[in,out] props Pointer to transport properties structure
 * @param[in] prop_enum Must be MULTIPATH property
 * @param[in] val Multipath mode (disabled, active, or passive)
 */
CT_EXTERN void ct_tp_set_sel_prop_multipath(ct_transport_properties_t* props, ct_selection_property_enum_t prop_enum, ct_multipath_enum_t val);

/**
 * @brief Set the communication direction in transport properties.
 *
 * @param[in,out] props Pointer to transport properties structure
 * @param[in] prop_enum Must be DIRECTION property
 * @param[in] val Direction value (bidirectional, send-only, or receive-only)
 */
CT_EXTERN void ct_tp_set_sel_prop_direction(ct_transport_properties_t* props, ct_selection_property_enum_t prop_enum, ct_direction_of_communication_enum_t val);

/**
 * @brief Set a boolean selection property in transport properties.
 *
 * @param[in,out] props Pointer to transport properties structure
 * @param[in] prop_enum Which boolean property to set
 * @param[in] val Boolean value
 */
CT_EXTERN void ct_tp_set_sel_prop_bool(ct_transport_properties_t* props, ct_selection_property_enum_t prop_enum, bool val);

/**
 * @brief Set interface preference in transport properties.
 *
 * @param[in,out] props Pointer to transport properties structure
 * @param[in] interface_name Name of the network interface (e.g., "eth0", "wlan0")
 * @param[in] preference Preference level for using this interface
 */
CT_EXTERN void ct_tp_set_sel_prop_interface(ct_transport_properties_t* props, char* interface_name, ct_selection_preference_t preference);


// =============================================================================
// Endpoints - Opaque types for local and remote endpoints
// =============================================================================

/**
 * @brief Local endpoint specification for binding connections/listeners.
 *
 * This is an opaque type - the internal structure is hidden from users.
 * Use ct_local_endpoint_new() to create and ct_local_endpoint_with_*() to configure.
 */
typedef struct ct_local_endpoint_s ct_local_endpoint_t;

/**
 * @brief Remote endpoint specification for connection targets.
 *
 * This is an opaque type - the internal structure is hidden from users.
 * Use ct_remote_endpoint_new() to create and ct_remote_endpoint_with_*() to configure.
 */
typedef struct ct_remote_endpoint_s ct_remote_endpoint_t;


// =============================================================================
// Messages - Message and message context structures
// =============================================================================

/**
 * @brief A message containing data to send or received data.
 *
 * Opaque type - use message accessor functions to work with messages.
 * Full definition is in ctaps_internal.h.
 *
 * ## Message Ownership Model
 *
 * CTaps uses a clear ownership model for messages:
 *
 * ### Sending Messages
 *
 * When you send a message using ct_send_message() or ct_send_message_full():
 * - **You retain ownership** of your original message
 * - CTaps makes a **deep copy** internally for transmission
 * - **You can free your message immediately** after the send function returns
 * - CTaps manages the lifecycle of its internal copy
 *
 * Example:
 * ```c
 * ct_message_t* msg = ct_message_new_with_content("Hello", 5);
 * ct_send_message(connection, msg);
 * ct_message_free_all(msg);  // Safe to free immediately after send
 * ```
 *
 * ### Receiving Messages
 *
 * When you receive a message in a receive callback:
 * - The client takes ownership of the received message and must free it when appropriate.
 *
 * Example:
 * ```c
 * int on_receive(ct_connection_t* conn, ct_message_t** msg, ct_message_context_t* ctx) {
 *     const char* content = ct_message_get_content(*msg);
 *     printf("Received message: %s\n", content);
 *     ct_message_free_all(*msg);  // Free when done
 *     return 0;
 * }
 * ```
 */
typedef struct ct_message_s ct_message_t;

/**
 * @brief Context information associated with a message.
 *
 * Opaque type containing message properties, endpoint information, and user context.
 * Full definition is in ctaps_internal.h.
 *
 * ## Message Context Ownership Model
 *
 * ### Passing to Sending Functions
 * When you pass a message context to ct_send_message_full() or similar functions:
 * - **You retain ownership** of your original message_context
 * - CTaps makes a **deep copy** internally if it needs to store the context
 * - **You can free your message_context** after the function returns
 * - It is safe to reuse or modify your message_context for subsequent sends
 *
 * ### Receiving in Callbacks
 * When a message context is passed to your receive callback:
 * - **CTaps owns the message_context**
 * - The context is valid only during the callback execution
 * - **Do not free** the context - CTaps will free it after the callback returns
 * - If you need the context data after the callback, make a deep copy
 *
 * ### Lifecycle
 * - Create with ct_message_context_new()
 * - Access properties with ct_message_context_get_message_properties()
 * - Optionally set local/remote endpoints
 * - Pass to send functions (CTaps deep copies internally)
 * - Free your copy with ct_message_context_free() when done
 */
typedef struct ct_message_context_s ct_message_context_t;

// =============================================================================
// Callbacks - Connection and Listener callback structures
// =============================================================================

/**
 * @brief Callback functions for receiving messages on a connection.
 *
 * Set these callbacks via ct_receive_message() to handle incoming data.
 * All callbacks are invoked from the event loop thread.
 */
typedef struct ct_receive_callbacks_s {
  /** @brief Called when a complete message is received.
   * @param[in] connection The connection that received the message
   * @param[in,out] received_message Pointer to received message. Caller takes ownership.
   * @param[in] ctx Message context with properties and endpoints
   * @return 0 on success, non-zero on error
   */
  int (*receive_callback)(ct_connection_t* connection, ct_message_t** received_message, ct_message_context_t* ctx);

  /** @brief Called when a receive error occurs.
   * @param[in] connection The connection that experienced the error
   * @param[in] ctx Message context
   * @param[in] reason Error description string
   * @return 0 on success, non-zero on error
   */
  int (*receive_error)(ct_connection_t* connection, ct_message_context_t* ctx, const char* reason);

  /** @brief Called when a partial message is received (for streaming).
   * @param[in] connection The connection that received the partial message
   * @param[in,out] received_message Pointer to partial message data
   * @param[in] ctx Message context
   * @param[in] end_of_message True if this is the final fragment
   * @return 0 on success, non-zero on error
   */
  int (*receive_partial)(ct_connection_t* connection, ct_message_t** received_message, ct_message_context_t* ctx, bool end_of_message);

  void* user_receive_context;  ///< User-provided context passed to receive callbacks
} ct_receive_callbacks_t;

/**
 * @brief Callback functions for connection lifecycle events.
 *
 * Set these callbacks via ct_preconnection_initiate() or ct_preconnection_listen().
 * All callbacks are invoked from the event loop thread.
 */
typedef struct ct_connection_callbacks_s {
  /** @brief Called when a connection error occurs after establishment. */
  int (*connection_error)(ct_connection_t* connection);

  /** @brief Called when connection establishment fails. */
  int (*establishment_error)(ct_connection_t* connection);

  /** @brief Called when a connection expires (e.g., idle timeout). */
  int (*expired)(ct_connection_t* connection);

  /** @brief Called when the connection's network path changes (multipath). */
  int (*path_change)(ct_connection_t* connection);

  /** @brief Called when connection is established and ready for data transfer. */
  int (*ready)(ct_connection_t* connection);

  /** @brief Called when a message send operation fails. */
  int (*send_error)(ct_connection_t* connection);

  /** @brief Called when a sent message is acknowledged by the transport. */
  int (*sent)(ct_connection_t* connection);

  /** @brief Called when a non-fatal error occurs (e.g., congestion). */
  int (*soft_error)(ct_connection_t* connection);

  void* user_connection_context;  ///< User-provided context for the connection lifetime
} ct_connection_callbacks_t;

/**
 * @brief Callback functions for listener events.
 *
 * Set these callbacks via ct_preconnection_listen().
 * All callbacks are invoked from the event loop thread.
 */
typedef struct ct_listener_callbacks_s {
  /** @brief Called when a new connection is received.
   * @param[in] listener The listener that accepted the connection
   * @param[in] new_conn The new connection object (caller must handle)
   * @return 0 to accept connection, non-zero to reject
   */
  int (*connection_received)(ct_listener_t* listener, ct_connection_t* new_conn);

  /** @brief Called when connection establishment fails for an incoming connection.
   * @param[in] listener The listener
   * @param[in] reason Error description string
   * @return 0 on success, non-zero on error
   */
  int (*establishment_error)(ct_listener_t* listener, const char* reason);

  /** @brief Called when the listener has stopped and will accept no more connections. */
  int (*stopped)(ct_listener_t* listener);

  void* user_listener_context;  ///< User-provided context for the listener lifetime
} ct_listener_callbacks_t;

// =============================================================================
// Message Framer - Optional message framing/parsing layer
// =============================================================================

/**
 * @brief Message framer implementation interface.
 *
 * Framers provide an optional layer between the application and the transport protocol
 * to handle message boundaries, encoding, and decoding. Examples include length-prefix
 * framing, HTTP/2 framing, or custom application protocols.
 */
typedef struct ct_framer_impl_s ct_framer_impl_t;

/**
 * @brief Callback invoked by framer when message encoding is complete.
 *
 * @param[in] connection The connection
 * @param[in] encoded_message The encoded message ready for transmission
 * @param[in] context Message context
 * @return 0 on success, negative error code on failure
 */
typedef int (*ct_framer_done_encoding_callback)(ct_connection_t* connection,
                                                 ct_message_t* encoded_message,
                                                 ct_message_context_t* context);

/**
 * @brief Callback invoked by framer when message decoding is complete.
 *
 * @param[in] connection The connection
 * @param[in] encoded_message The decoded message
 * @param[in] context Message context
 */
typedef void (*ct_framer_done_decoding_callback)(ct_connection_t* connection,
                                                  ct_message_t* encoded_message,
                                                  ct_message_context_t* context);

/**
 * @brief Message framer implementation interface.
 *
 * Implement this interface to provide custom message framing/deframing logic.
 */
struct ct_framer_impl_s {
  /**
   * @brief Encode an outbound message before transmission.
   *
   * Implementation should call the provided callback when encoding is complete.
   *
   * @param[in] connection The connection
   * @param[in] message The message to encode
   * @param[in] context Message context
   * @param[in] callback Callback to invoke when encoding is complete
   * @return 0 on success, negative error code on failure
   */
  int (*encode_message)(ct_connection_t* connection,
                        ct_message_t* message,
                        ct_message_context_t* context,
                        ct_framer_done_encoding_callback callback);

  /**
   * @brief Decode inbound data into application messages.
   *
   * @param[in] connection The connection
   * @param[in] message received from transport layer
   * @param[in] context Message context containing endpoint info
   * @param[in] callback Callback to invoke when decoding is complete
   */
  void (*decode_data)(ct_connection_t* connection,
                     ct_message_t* message,
                     ct_message_context_t* context,
                     ct_framer_done_decoding_callback callback);
};

// =============================================================================
// Protocol Interface - Protocol implementation abstraction
// =============================================================================

/**
 * @brief Protocol implementation interface.
 *
 * This interface defines the contract that all transport protocol implementations
 * (TCP, UDP, QUIC, or custom protocols) must implement. The CTaps library uses
 * this interface to abstract over different transport protocols.
 *
 * This is an opaque type used only via pointers. Protocol implementations are registered
 * using ct_register_protocol().
 */
typedef struct ct_protocol_impl_s ct_protocol_impl_t;

// =============================================================================
// Public API Functions
// =============================================================================

/**
 * @brief Set a selection property value directly.
 * @param[in,out] selection_properties structure to modify
 * @param[in] property Which property to set
 * @param[in] value Property value
 */
CT_EXTERN void ct_selection_properties_set(ct_selection_properties_t* selection_properties, ct_selection_property_enum_t property, ct_selection_property_value_t value);

/**
 * @brief Free resources in selection properties.
 * @param[in] selection_properties structure to free
 */
CT_EXTERN void ct_selection_properties_free(ct_selection_properties_t* selection_properties);

// =============================================================================
// Message Properties
// =============================================================================

/**
 * @brief Collection of message properties for per-message transmission control.
 *
 * Opaque type - use message property functions to work with this structure.
 * Full definition is in ctaps_internal.h.
 *
 * ## Message Properties Ownership Model
 *
 * ### Passing to Functions
 * Message properties are typically embedded within a ct_message_context_t.
 * When you pass a message context to sending functions:
 * - **You retain ownership** of your original message_context
 * - CTaps makes a **deep copy** internally when needed
 * - **You can free your message_context** after the function returns
 *
 * ### Lifecycle
 * - Create with ct_message_properties_new()
 * - Embed in a message context with ct_message_context_new()
 * - Free your copy with ct_message_properties_free() when done
 * - CTaps-owned copies are freed automatically
 */
typedef struct ct_message_properties_s ct_message_properties_t;

/**
 * @brief Create a new message properties object with default values.
 *
 * Allocates and initializes a new message properties object on the heap.
 * The returned object must be freed with ct_message_properties_free().
 *
 * @return Pointer to newly allocated message properties, or NULL on allocation failure.
 */
CT_EXTERN ct_message_properties_t* ct_message_properties_new(void);

/**
 * @brief Check if the FINAL property is set in message properties.
 * @param[in] message_properties structure to check
 *
 * @return true if FINAL property is set, false otherwise or null
 */
CT_EXTERN bool ct_message_properties_is_final(const ct_message_properties_t* message_properties);

CT_EXTERN void ct_message_properties_set_uint64(ct_message_properties_t* message_properties, ct_message_properties_enum_t property, uint64_t value);

CT_EXTERN void ct_message_properties_set_uint32(ct_message_properties_t* message_properties, ct_message_properties_enum_t property, uint32_t value);

CT_EXTERN void ct_message_properties_set_boolean(ct_message_properties_t* message_properties, ct_message_properties_enum_t property, bool value);

CT_EXTERN void ct_message_properties_set_capacity_profile(ct_message_properties_t* message_properties, ct_message_properties_enum_t property, ct_capacity_profile_enum_t value);

CT_EXTERN uint64_t ct_message_properties_get_uint64(const ct_message_properties_t* message_properties,  ct_message_properties_enum_t property);

CT_EXTERN uint32_t ct_message_properties_get_uint32(const ct_message_properties_t* message_properties,  ct_message_properties_enum_t property);

CT_EXTERN bool ct_message_properties_get_boolean(const ct_message_properties_t* message_properties,  ct_message_properties_enum_t property);

CT_EXTERN ct_capacity_profile_enum_t ct_message_properties_get_capacity_profile(const ct_message_properties_t* message_properties);

CT_EXTERN bool ct_message_properties_get_safely_replayable(const ct_message_properties_t* message_properties);

CT_EXTERN void ct_message_properties_set_safely_replayable(ct_message_properties_t* message_properties, bool value);

/**
 * @brief Free resources in message properties.
 * @param[in] message_properties structure to free
 */
CT_EXTERN void ct_message_properties_free(ct_message_properties_t* message_properties);

/**
 * @brief Free resources in transport properties.
 * @param[in] transport_properties structure to free
 */
CT_EXTERN void ct_transport_properties_free(ct_transport_properties_t* transport_properties);

// Certificate bundles
typedef struct ct_certificate_bundles_s ct_certificate_bundles_t;

CT_EXTERN ct_certificate_bundles_t* ct_certificate_bundles_new(void);

CT_EXTERN int ct_certificate_bundles_add_cert(ct_certificate_bundles_t* bundles, const char* cert_file_path, const char* key_file_path);

CT_EXTERN void ct_certificate_bundles_free(ct_certificate_bundles_t* bundles);


// Security Parameters
/**
 * @brief Allocate a new security parameters object on the heap.
 * @return Pointer to newly allocated security parameters, or NULL on failure
 */
CT_EXTERN ct_security_parameters_t* ct_security_parameters_new(void);

/**
 * @brief Free resources in security parameters including the structure itself.
 * @param[in] security_parameters structure to free
 */
CT_EXTERN void ct_sec_param_free(ct_security_parameters_t* security_parameters);


// TODO - change these to be per-value not per-type
/**
 * @brief Set a string array security parameter (e.g., ALPN, ciphersuites).
 * @param[in,out] security_parameters structure to modify
 * @param[in] property Which security parameter to set
 * @param[in] strings Array of string values
 * @param[in] num_strings Number of strings in the array
 * @return 0 on success, non-zero on error
 */
CT_EXTERN int ct_sec_param_set_property_string_array(ct_security_parameters_t* security_parameters, ct_security_property_enum_t property, char** strings, size_t num_strings);

CT_EXTERN int ct_sec_param_set_property_certificate_bundles(ct_security_parameters_t* security_parameters, ct_security_property_enum_t property, ct_certificate_bundles_t* bundles);

CT_EXTERN int ct_sec_param_set_ticket_store_path(ct_security_parameters_t* security_parameters, const char* ticket_store_path);

CT_EXTERN const char* ct_sec_param_get_ticket_store_path(const ct_security_parameters_t* security_parameters);

CT_EXTERN const char** ct_sec_param_get_alpn_strings(const ct_security_parameters_t* security_parameters, size_t* out_num_strings);

// ==============================================================================
// ENDPOINT OWNERSHIP MODEL
// ==============================================================================
/**
 * @section endpoint_ownership Endpoint Ownership and Memory Management
 *
 * CTaps uses a clear ownership model for endpoints:
 *
 * **User Ownership:**
 * - Users create endpoints with ct_local_endpoint_new() / ct_remote_endpoint_new()
 * - Users OWN these endpoints and are responsible for freeing them
 * - Endpoints can be reused with multiple preconnections
 * - Endpoints can be freed immediately after passing to ct_preconnection_new()
 *
 * **CTaps Ownership:**
 * - ct_preconnection_new() makes deep copies of all endpoints passed to it
 * - Each preconnection owns its own independent copies
 * - ct_preconnection_free() automatically frees the preconnection's endpoint copies
 * - When a connection is initiated, it gets its own deep copies of endpoints
 * - Connection cleanup automatically frees its endpoint copies
 *
 * **Example Usage:**
 * @code
 *   // Create endpoint (user owns this)
 *   ct_remote_endpoint_t* endpoint = ct_remote_endpoint_new();
 *   ct_remote_endpoint_with_hostname(endpoint, "example.com");
 *   ct_remote_endpoint_with_port(endpoint, 443);
 *
 *   // Use endpoint with multiple preconnections (each gets a deep copy)
 *   ct_preconnection_t* precon1 = ct_preconnection_new(endpoint, 1, props1, NULL);
 *   ct_preconnection_t* precon2 = ct_preconnection_new(endpoint, 1, props2, NULL);
 *
 *   // User can free endpoint after last use (preconnections have their own copies)
 *   ct_remote_endpoint_free(endpoint);
 *
 *   // Preconnections free their own endpoint copies when freed
 *   ct_preconnection_free(precon1);  // Frees precon1's endpoint copy
 *   ct_preconnection_free(precon2);  // Frees precon2's endpoint copy
 * @endcode
 *
 * This model ensures:
 * - Clear ownership boundaries (user owns originals, CTaps owns copies)
 * - No use-after-free bugs (each object owns its own endpoint data)
 * - Easy resource management (free endpoints when you're done with them)
 * - Endpoint reusability (use same endpoint for multiple preconnections)
 */

// Local Endpoint
/**
 * @brief Create a new heap-allocated local endpoint.
 *
 * The caller owns the returned endpoint and must free it with ct_local_endpoint_free()
 * when done. The endpoint can be safely freed after passing to ct_preconnection_new()
 * or ct_preconnection_set_local_endpoint(), as CTaps makes internal copies.
 *
 * @return Pointer to newly allocated endpoint, or NULL on error
 * @see endpoint_ownership
 */
CT_EXTERN ct_local_endpoint_t* ct_local_endpoint_new(void);

/**
 * @brief Set the network interface for a local endpoint.
 * @param[in,out] local_endpoint Endpoint to modify
 * @param[in] interface_name Interface name (e.g., "eth0", "wlan0")
 * @return 0 on success, non-zero on error
 */
CT_EXTERN int ct_local_endpoint_with_interface(ct_local_endpoint_t* local_endpoint, const char* interface_name);

/**
 * @brief Set the port number for a local endpoint.
 * @param[in,out] local_endpoint Endpoint to modify
 * @param[in] port Port number (0 = any available port)
 */
CT_EXTERN void ct_local_endpoint_with_port(ct_local_endpoint_t* local_endpoint, int port);

/**
 * @brief Set the service name for a local endpoint.
 * @param[in,out] local_endpoint Endpoint to modify
 * @param[in] service Service name (e.g., "http", "https")
 * @return 0 on success, non-zero on error
 */
CT_EXTERN int ct_local_endpoint_with_service(ct_local_endpoint_t* local_endpoint, char* service);

/**
 * @brief Free all resources in a local endpoint.
 * @param[in] local_endpoint Endpoint to free
 */
CT_EXTERN void ct_local_endpoint_free(ct_local_endpoint_t* local_endpoint);

/**
 * @brief Free string fields in a local endpoint without freeing the structure.
 * @param[in] local_endpoint Endpoint whose strings to free
 */
CT_EXTERN void ct_local_endpoint_free_strings(ct_local_endpoint_t* local_endpoint);

/**
 * @brief Create a heap-allocated copy of a local endpoint.
 * @param[in] local_endpoint Source endpoint
 * @return Pointer to newly allocated copy, or NULL on error
 */
ct_local_endpoint_t* local_endpoint_copy(const ct_local_endpoint_t* local_endpoint);

/**
 * @brief Create a stack copy of a local endpoint's content.
 * @param[in] local_endpoint Source endpoint
 * @return Copy of the endpoint structure
 */
ct_local_endpoint_t ct_local_endpoint_copy_content(const ct_local_endpoint_t* local_endpoint);

/**
 * @brief Resolve a local endpoint to concrete addresses.
 * @param[in] local_endpoint Endpoint to resolve
 * @param[out] out_list Output array of resolved endpoints (caller must free)
 * @param[out] out_count Number of endpoints in output array
 * @return 0 on success, non-zero on error
 *
 * TODO - this shouldn't be public?
 */
CT_EXTERN int ct_local_endpoint_resolve(const ct_local_endpoint_t* local_endpoint, ct_local_endpoint_t** out_list, size_t* out_count);

/**
 * @brief Get the service for a local endpoint
 * @param[in] remote_endpoint endpoint to get service for
 *
 * @return char* pointer to service name, NULL if endpoint is null, or if service is not set
 */
CT_EXTERN const char* ct_local_endpoint_get_service(const ct_local_endpoint_t* local_endpoint);


// Remote Endpoint
/**
 * @brief Create a new heap-allocated remote endpoint.
 *
 * The caller owns the returned endpoint and must free it with ct_remote_endpoint_free()
 * when done. The endpoint can be safely freed after passing to ct_preconnection_new(),
 * as CTaps makes internal copies.
 *
 * @return Pointer to newly allocated endpoint, or NULL on error
 * @see endpoint_ownership
 */
CT_EXTERN ct_remote_endpoint_t* ct_remote_endpoint_new(void);

/**
 * @brief Set the hostname for a remote endpoint.
 * @param[in,out] remote_endpoint Endpoint to modify
 * @param[in] hostname Hostname or IP address string
 * @return 0 on success, non-zero on error
 */
CT_EXTERN int ct_remote_endpoint_with_hostname(ct_remote_endpoint_t* remote_endpoint, const char* hostname);

/**
 * @brief Set the port number for a remote endpoint.
 * @param[in,out] remote_endpoint Endpoint to modify
 * @param[in] port Port number
 */
CT_EXTERN void ct_remote_endpoint_with_port(ct_remote_endpoint_t* remote_endpoint, uint16_t port);

/**
 * @brief Set the service name for a remote endpoint.
 * @param[in,out] remote_endpoint Endpoint to modify
 * @param[in] service Service name (e.g., "http", "https")
 * @return 0 on success, non-zero on error
 */
CT_EXTERN int ct_remote_endpoint_with_service(ct_remote_endpoint_t* remote_endpoint, const char* service);

/**
 * @brief Free string fields in a remote endpoint without freeing the structure.
 * @param[in] remote_endpoint Endpoint whose strings to free
 */
CT_EXTERN void ct_remote_endpoint_free_strings(ct_remote_endpoint_t* remote_endpoint);

/**
 * @brief Free all resources in a remote endpoint.
 * @param[in] remote_endpoint Endpoint to free
 */
CT_EXTERN void ct_remote_endpoint_free(ct_remote_endpoint_t* remote_endpoint);

/**
 * @brief Initialize a remote endpoint from a sockaddr structure.
 * @param[out] remote_endpoint Endpoint to initialize
 * @param[in] addr Socket address structure
 * @return 0 on success, non-zero on error
 */
CT_EXTERN int ct_remote_endpoint_from_sockaddr(ct_remote_endpoint_t* remote_endpoint, const struct sockaddr_storage* addr);

/**
 * @brief Resolve a remote endpoint hostname to concrete addresses via DNS.
 * @param[in] remote_endpoint Endpoint to resolve
 * @param[out] out_list Output array of resolved endpoints (caller must free)
 * @param[out] out_count Number of endpoints in output array
 * @return 0 on success, non-zero on error
 */
CT_EXTERN int ct_remote_endpoint_resolve(const ct_remote_endpoint_t* remote_endpoint, ct_remote_endpoint_t** out_list, size_t* out_count);

/**
 * @brief Create a heap-allocated copy of a remote endpoint.
 * @param[in] remote_endpoint Source endpoint
 * @return Pointer to newly allocated copy, or NULL on error
 */
ct_remote_endpoint_t* remote_endpoint_copy(const ct_remote_endpoint_t* remote_endpoint);

/**
 * @brief Create a stack copy of a remote endpoint's content.
 * @param[in] remote_endpoint Source endpoint
 * @return Copy of the endpoint structure
 */
ct_remote_endpoint_t ct_remote_endpoint_copy_content(const ct_remote_endpoint_t* remote_endpoint);

/**
 * @brief Set the IPv4 address for a remote endpoint.
 * @param[in,out] remote_endpoint Endpoint to modify
 * @param[in] ipv4_addr IPv4 address in network byte order
 * @return 0 on success, non-zero on error
 */
CT_EXTERN int ct_remote_endpoint_with_ipv4(ct_remote_endpoint_t* remote_endpoint, in_addr_t ipv4_addr);

/**
 * @brief Set the IPv6 address for a remote endpoint.
 * @param[in,out] remote_endpoint Endpoint to modify
 * @param[in] ipv6_addr IPv6 address structure
 * @return 0 on success, non-zero on error
 */
CT_EXTERN int ct_remote_endpoint_with_ipv6(ct_remote_endpoint_t* remote_endpoint, struct in6_addr ipv6_addr);

/**
 * @brief Get the service for a remote endpoint
 * @param[in] remote_endpoint endpoint to get service for
 *
 * @return char* pointer to service name, NULL if endpoint is null, or if service is not set
 */
CT_EXTERN const char* ct_remote_endpoint_get_service(const ct_remote_endpoint_t* remote_endpoint);

// Message
/**
 * @brief Free all resources in a message including the structure.
 * @param[in] message Message to free
 */
CT_EXTERN void ct_message_free(ct_message_t* message);

/**
 * @brief Allocate a new message on the heap.
 * @return Pointer to newly allocated message, or NULL on failure
 */
CT_EXTERN ct_message_t* ct_message_new(void);

/**
 * @brief Allocate a new message with content.
 * @param[in] content Data buffer for the message
 * @param[in] length Length of data in bytes
 * @return Pointer to newly allocated message with content, or NULL on failure
 */
CT_EXTERN ct_message_t* ct_message_new_with_content(const char* content, size_t length);

/**
 * @brief Get the length of a message.
 * @param[in] message Message to query
 * @return Length of message in bytes, or 0 if message is NULL
 */
CT_EXTERN size_t ct_message_get_length(const ct_message_t* message);

/**
 * @brief Get the content buffer of a message.
 * @param[in] message Message to query
 * @return Pointer to message content, or NULL if message is NULL
 */
CT_EXTERN const char* ct_message_get_content(const ct_message_t* message);

CT_EXTERN void ct_message_set_content(ct_message_t* message, const char* content, size_t length);


// Message Context
/**
 * @brief Initialize a message context with default values.
 * @param[out] message_context Context structure to initialize
 */
CT_EXTERN ct_message_context_t* ct_message_context_new();



/**
 * @brief Free resources in a message context.
 * @param[in] message_context Context to free
 */
CT_EXTERN void ct_message_context_free(ct_message_context_t* message_context);

/**
 * @brief Get message properties from a message context.
 *
 * Returns a pointer to the message properties contained in the message context.
 * The returned pointer is owned by the message context and should not be freed.
 *
 * @param[in] message_context Context to get properties from
 * @return Pointer to message properties, or NULL if message_context is NULL
 */
CT_EXTERN ct_message_properties_t* ct_message_context_get_message_properties(ct_message_context_t* message_context);

/**
 * @brief Get the remote endpoint from a message context.
 *
 * @param[in] message_context Context to get remote endpoint from
 * @return Pointer to remote endpoint, or NULL if message_context is NULL or remote endpoint not set
 */
CT_EXTERN const ct_remote_endpoint_t* ct_message_context_get_remote_endpoint(const ct_message_context_t* message_context);


/**
 * @brief Get the local endpoint from a message context.
 *
 * @param[in] message_context Context to get local endpoint from
 * @return Pointer to local endpoint, or NULL if message_context is NULL or local endpoint not set
 */
CT_EXTERN const ct_local_endpoint_t* ct_message_context_get_local_endpoint(const ct_message_context_t* message_context);

// Message context property setters
CT_EXTERN void ct_message_context_set_uint64(ct_message_context_t* message_context, ct_message_properties_enum_t property, uint64_t value);

CT_EXTERN void ct_message_context_set_uint32(ct_message_context_t* message_context, ct_message_properties_enum_t property, uint32_t value);

CT_EXTERN void ct_message_context_set_boolean(ct_message_context_t* message_context, ct_message_properties_enum_t property, bool value);

CT_EXTERN void ct_message_context_set_capacity_profile(ct_message_context_t* message_context, ct_message_properties_enum_t property, ct_capacity_profile_enum_t value);

CT_EXTERN void ct_message_context_set_safely_replayable(ct_message_context_t* message_context, bool value);

// Message context property getters
CT_EXTERN uint64_t ct_message_context_get_uint64(const ct_message_context_t* message_context, ct_message_properties_enum_t property);

CT_EXTERN uint32_t ct_message_context_get_uint32(const ct_message_context_t* message_context, ct_message_properties_enum_t property);

CT_EXTERN bool ct_message_context_get_boolean(const ct_message_context_t* message_context, ct_message_properties_enum_t property);


/**
 * @brief Create a new preconnection with transport properties and endpoints.
 *
 * Allocates and initializes a new preconnection object on the heap.
 * The returned object must be freed with ct_preconnection_free().
 * This follows the RFC 9622 pattern: Preconnection := NewPreconnection(...)
 *
 * @param[in] remote_endpoints Array of remote endpoints to connect to, or NULL
 * @param[in] num_remote_endpoints Number of remote endpoints (0 if remote_endpoints is NULL)
 * @param[in] transport_properties Transport property preferences, or NULL for defaults
 * @param[in] security_parameters Security configuration (TLS/QUIC), or NULL
 * @return Pointer to newly allocated preconnection, or NULL on allocation failure
 */
CT_EXTERN ct_preconnection_t* ct_preconnection_new(
    const ct_remote_endpoint_t* remote_endpoints,
    const size_t num_remote_endpoints,
    const ct_transport_properties_t* transport_properties,
    const ct_security_parameters_t* security_parameters);

/**
 * @brief Free a preconnection object.
 *
 * Releases all resources associated with the preconnection object,
 * including any dynamically allocated remote endpoints and internal state.
 * After calling this function, the pointer is invalid and must not be used.
 *
 * @param[in] preconnection Pointer to preconnection to free. Does nothing if NULL.
 */
CT_EXTERN void ct_preconnection_free(ct_preconnection_t* preconnection);

/**
 * @brief Add an additional remote endpoint to a preconnection.
 * @param[in,out] preconnection Preconnection to modify
 * @param[in] remote_endpoint Endpoint to add
 */
CT_EXTERN void ct_preconnection_add_remote_endpoint(ct_preconnection_t* preconnection, const ct_remote_endpoint_t* remote_endpoint);

/**
 * @brief Set the local endpoint for a preconnection.
 * @param[in,out] preconnection Preconnection to modify
 * @param[in] local_endpoint Local endpoint to set
 */
CT_EXTERN void ct_preconnection_set_local_endpoint(ct_preconnection_t* preconnection, const ct_local_endpoint_t* local_endpoint);

/**
 * @brief Set a message framer for the preconnection.
 * @param[in,out] preconnection Preconnection to modify
 * @param[in] framer_impl Framer implementation to use, or NULL for no framing
 */
CT_EXTERN void ct_preconnection_set_framer(ct_preconnection_t* preconnection, ct_framer_impl_t* framer_impl);

/**
 * @brief Free resources in a preconnection.
 * @param[in] preconnection Preconnection to free
 */
CT_EXTERN void ct_preconnection_free(ct_preconnection_t* preconnection);

/**
 * @brief Initiate a connection
 *
 * Initiates a connection using the configured Preconnection. The connection is allocated
 * internally and provided to the user via the ready() callback.
 *
 * @param[in] preconnection Pointer to the Preconnection object containing the connection
 *                          configuration.
 * @param[in] connection_callbacks Struct containing callback functions for connection events:
 *
 * @return 0 on no synchronous errors
 * @return Non-zero error code on synchronous error
 *
 * @note Asynchronous errors are reported via the establishment_error callback
 */
CT_EXTERN int ct_preconnection_initiate(ct_preconnection_t* preconnection, ct_connection_callbacks_t connection_callbacks);


/**
 * @brief Initiate a connection and send a message immediately upon establishment.
 *
 * Initiates a connection using the configured Preconnection. The connection is allocated
 * internally and provided to the user via the ready() callback. If the underlying protocol
 * supports 0-RTT or early data, the message may be sent during the handshake.
 * Otherwise the data will be sent immediately after establishment.
 *
 * @param[in] preconnection Pointer to the Preconnection object containing the connection
 *                          configuration.
 * @param[in] connection_callbacks Struct containing callback functions for connection events:
 *
 * @return 0 on no synchronous errors
 * @return Non-zero error code on synchronous error
 *
 * @note Asynchronous errors are reported via the establishment_error callback
 * @note the message context must have the MSG_SAFELY_REPLAYABLE property set to make use of 0-RTT
 */
CT_EXTERN int ct_preconnection_initiate_with_send(ct_preconnection_t* preconnection, ct_connection_callbacks_t connection_callbacks, const ct_message_t* message, const ct_message_context_t* message_context);

/**
 * @brief Start listening for incoming connections using the configured Preconnection.
 *
 * @param[in] preconnection Pointer to preconnection with listener configuration
 * @param[out] listener Pointer to listener object to initialize. Must be allocated by caller.
 * @param[in] listener_callbacks Callbacks for listener events (connection_received, etc.)
 * @return 0 on success, non-zero on error
 *
 * @note The event loop must be running for the listener to accept connections
 * @see ct_listener_stop() for stopping the listener
 */
CT_EXTERN int ct_preconnection_listen(ct_preconnection_t* preconnection, ct_listener_t* listener, ct_listener_callbacks_t listener_callbacks);

// Connection
/**
 * @brief Send a message over a connection with default properties.
 * @param[in] connection The connection to send on
 * @param[in] message The message to send
 * @return 0 on success, non-zero on error
 */
CT_EXTERN int ct_send_message(ct_connection_t* connection, ct_message_t* message);

/**
 * @brief Send a message with custom message context and properties.
 * @param[in] connection The connection to send on
 * @param[in] message The message to send
 * @param[in] message_context Message properties and context
 * @return 0 on success, non-zero on error
 */
CT_EXTERN int ct_send_message_full(ct_connection_t* connection, ct_message_t* message, ct_message_context_t* message_context);

/**
 * @brief Register callbacks to receive messages on a connection.
 * @param[in] connection The connection to receive on
 * @param[in] receive_callbacks Callbacks for receive events
 * @return 0 on success, non-zero on error
 */
CT_EXTERN int ct_receive_message(ct_connection_t* connection, ct_receive_callbacks_t receive_callbacks);

/**
 * @brief Check if a connection is closed.
 * @param[in] connection The connection to check
 * @return true if connection is closed, false if open or connection is NULL
 */
CT_EXTERN bool ct_connection_is_closed(const ct_connection_t* connection);

/**
 * @brief Get the connections callback context.
 * @param[in] connection connection to get callback context for
 * @return Void pointer assigned to callback context
 *         null if connection is null or it hasn't been set
 */
CT_EXTERN void* ct_connection_get_callback_context(const ct_connection_t* connection);

/**
 * @brief Get the UUID of a connection
 * @param[in] connection connection to get uuid for
 * @return Pointer to uuid string
 */
CT_EXTERN const char* ct_connection_get_uuid(const ct_connection_t* connection);

/**
 * @brief Get the name of the underlying protocol
 * @param[in] connection connection to get protocol name for
 * @return Pointer to protocol name, NULL of connection is NULL 
 */
CT_EXTERN const char* ct_connection_get_protocol_name(const ct_connection_t* connection);

/**
 * TODO - maybe this has to return an array?
 *
 * @brief Get the remote endpoint for the connection
 * @param[in] connection connection to get remote endpoint for
 * @return Pointer to remote endpoint, NULL of connection is NULL 
 */
CT_EXTERN const ct_remote_endpoint_t* ct_connection_get_remote_endpoint(const ct_connection_t* connection);

/**
 * @brief Check if a connection is established.
 * @param[in] connection The connection to check
 * @return true if connection is established, false if open or connection is NULL
 */
bool ct_connection_is_established(const ct_connection_t* connection);

/**
 * @brief Check if a connection is currently being closed.
 * @param[in] connection The connection to check
 * @return true if connection is closed, false if open or connection is NULL
 */
CT_EXTERN bool ct_connection_is_closing(const ct_connection_t* connection);

/**
 * @brief Check if a connection is closed.
 * @param[in] connection The connection to check
 * @return true if connection is closed, false if open or connection is NULL
 */
CT_EXTERN bool ct_connection_is_closed_or_closing(const ct_connection_t* connection);

/**
 * @brief Check if a connection is a client connection.
 * @param[in] connection The connection to check
 * @return true if connection is client role, false otherwise or if connection is NULL
 */
CT_EXTERN bool ct_connection_is_client(const ct_connection_t* connection);

/**
 * @brief Check if a connection is a server connection.
 * @param[in] connection The connection to check
 * @return true if connection is server role, false otherwise or if connection is NULL
 */
CT_EXTERN bool ct_connection_is_server(const ct_connection_t* connection);

/**
 * @brief Check the value of the canSend connection property.
 * @param[in] connection The connection to check
 * @return false if connection is NULL, closed, not established or "Final" message property has been sent.
 */
CT_EXTERN bool ct_connection_can_send(const ct_connection_t* connection);

/**
 * @brief Check the value of the canReceive connection property.
 * @param[in] connection The connection to check
 * @return false if connection is NULL, closed, not established or one way closed from remote.
 */
CT_EXTERN bool ct_connection_can_receive(const ct_connection_t* connection);

/**
 * @brief Free resources in a connection.
 * @param[in] connection Connection to free
 */
CT_EXTERN void ct_connection_free(ct_connection_t* connection);

/**
 * @brief Close a connection gracefully.
 * @param[in] connection Connection to close
 */
CT_EXTERN void ct_connection_close(ct_connection_t* connection);

/**
 * @brief Forcefully abort a connection without graceful shutdown.
 *
 * Unlike ct_connection_close() which performs a graceful shutdown (e.g., TCP FIN),
 * this immediately terminates the connection (e.g., TCP RST). Use when an error
 * condition requires immediate termination.
 *
 * @param[in] connection Connection to abort
 */
CT_EXTERN void ct_connection_abort(ct_connection_t* connection);

/**
 * @brief Clone a connection to create a new connection in the same connection group.
 *
 * Creates a new connection that shares the same transport session as the parent
 * connection. This enables multi-streaming protocols like QUIC and SCTP to create
 * multiple logical connections (streams) over a single transport session.
 *
 * The callbacks of the source connection are copied into the cloned connection.
 * The ready callback is invoked with the cloned connection as a parameter, when
 * connection succeeds.
 *
 * @param[in] source_connection The connection to clone
 * @param[in] framer Optional framer for the cloned connection (NULL to inherit)
 * @param[in] connection_properties Optional properties for cloned connection (NULL to inherit)
 * @param[in] connection_callbacks Callbacks for the cloned connection
 * @return An allocated connection object on success, or NULL on error
 */
CT_EXTERN int ct_connection_clone_full(
    const ct_connection_t* source_connection,
    ct_framer_impl_t* framer,
    const ct_transport_properties_t* connection_properties
);

/**
 * @brief Clone a connection with only mandatory parameters.
 *
 * Is a wrapper around ct_connection_clone_full()
 *
 * @see ct_connection_clone_full
 * @param[in] source_connection The connection to clone
 *
 * @return An allocated connection object on success, or NULL on error
 */
CT_EXTERN int ct_connection_clone(ct_connection_t* source_connection);

/**
 * @brief Get all open connections in the same connection group.
 *
 * Returns an array of pointers to all active (non-closed) connections in the same
 * connection group.
 *
 * @param[in] connection The connection to query
 * @param[out] out_count Number of connections in the returned array
 * @return Pointer to array of connection pointers (caller must free with free()), or NULL on error
 *
 * @note The caller must free the returned array with free(), but NOT the individual connection
 *       pointers (they remain owned by the connection group and will be freed when closed)
 * @note Only non-closed connections are included in the returned array
 * @note Returns NULL if connection is NULL, or if memory allocation fails
 * @note Returns NULL with *out_count=0 if there are no active connections in the group
 */
CT_EXTERN ct_connection_t** ct_connection_get_grouped_connections(
    const ct_connection_t* connection,
    size_t* out_count
);

/**
 * @brief Get the total number of connections in a connection group (including closed ones).
 *
 * @param[in] connection The connection to query
 * @return The total number of connections in the group
 */
CT_EXTERN size_t ct_connection_get_total_num_grouped_connections(const ct_connection_t* connection);

/**
 * @brief Get the number of open connections in a connection group.
 *
 * @param[in] connection The connection to query
 * @return The number of open (non-closed) connections in the group
 */
CT_EXTERN size_t ct_connection_get_num_open_grouped_connections(const ct_connection_t* connection);

/**
 * @brief Close all connections in the same connection group gracefully.
 *
 * Performs graceful shutdown of all connections in the group (the connection
 * and all its clones). This is equivalent to calling ct_connection_close() on
 * each connection in the group.
 *
 * @param[in] connection Any connection in the group to close
 */
CT_EXTERN void ct_connection_close_group(ct_connection_t* connection);

/**
 * @brief Forcefully abort all connections in the same connection group.
 *
 * Immediately terminates all connections in the group without graceful shutdown.
 * This is equivalent to calling ct_connection_abort() on each connection in the group.
 *
 * @param[in] connection Any connection in the group to abort
 */
CT_EXTERN void ct_connection_abort_group(ct_connection_t* connection);

// Listener
/**
 * @brief Stop a listener from accepting new connections.
 * @param[in] listener Listener to stop
 */
CT_EXTERN void ct_listener_stop(ct_listener_t* listener);

/**
 * @brief Close a listener and free its socket resources.
 * @param[in] listener Listener to close
 */
CT_EXTERN void ct_listener_close(ct_listener_t* listener);

/**
 * @brief Free resources in a listener.
 * @param[in] listener Listener to free
 */
CT_EXTERN void ct_listener_free(ct_listener_t* listener);

/**
 * @brief Get the local endpoint a listener is bound to.
 * @param[in] listener The listener
 * @return Local endpoint structure (copy)
 */
ct_local_endpoint_t ct_listener_get_local_endpoint(const ct_listener_t* listener);

#define MAX_PROTOCOLS 256  ///< Maximum number of protocols that can be registered

/// Global array of registered protocol implementations
__attribute__((unused)) static const ct_protocol_impl_t* ct_supported_protocols[MAX_PROTOCOLS] = {0};

/**
 * @brief Register a custom protocol implementation.
 *
 * Add a custom protocol to the registry so it can be selected during
 * candidate gathering and connection establishment.
 *
 * @param[in] proto Pointer to protocol implementation structure
 *
 * @note The protocol implementation must remain valid for the lifetime of the library
 * @see ct_protocol_impl_t for the protocol interface
 */
CT_EXTERN void ct_register_protocol(const ct_protocol_impl_t* proto);

/**
 * @brief Get the array of all registered protocols.
 * @return Pointer to protocol array
 */
const ct_protocol_impl_t** ct_get_supported_protocols();

/**
 * @brief Get the number of registered protocols.
 * @return Number of protocols in the registry
 */
size_t ct_get_num_protocols();

// =============================================================================
// Connection functions
// =============================================================================

typedef enum CT_TRANSPORT_PROTOCOL_ENUM_E {
  CT_PROTOCOL_ERROR = -1, // returned from getters in errors, e.g. null connection
  CT_PROTOCOL_TCP,
  CT_PROTOCOL_UDP,
  CT_PROTOCOL_QUIC,
} ct_protocol_enum_t;

CT_EXTERN const ct_connection_properties_t* ct_connection_get_connection_properties(const ct_connection_t* connection);

CT_EXTERN ct_protocol_enum_t ct_connection_get_transport_protocol(const ct_connection_t* connection);

CT_EXTERN bool ct_connection_used_0rtt(const ct_connection_t* connection);


#endif  // CTAPS_H

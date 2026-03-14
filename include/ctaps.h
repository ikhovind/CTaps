//
// CTaps - C implementation Transport Services as described in RFC 9621 - 9623
//

#ifndef CTAPS_H
#define CTAPS_H

#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/socket.h>

// Symbol visibility control - only export public API functions
#if defined(__GNUC__)
#define CT_EXTERN __attribute__((visibility("default")))
#else
#define CT_EXTERN
#endif

#define CT_CONNECTION_DEFAULT_PRIORITY 100

/**
 * @ingroup connection
 * @struct ct_connection_t
 * @brief Opaque handle representing a connection.
 *
 * Received via callback when a preconnection has been initiated or
 * a listener has received a new connection.
 */
typedef struct ct_connection_s ct_connection_t;

/**
 * @ingroup listener
 * @struct ct_listener_t
 * @brief Opaque handle representing a listener.
 *
 * Received via callback after ct_preconnection_listen()
 *
 * @note Despite being received via callback, the creation
 * of the listener is currently synchronous so the callback
 * is fired immediately.
 */
typedef struct ct_listener_s ct_listener_t;

/**
 * @ingroup listener
 * @brief Check if a listener is closed.
 * @param[in] listener Listener to check
 * @return true if the listener is closed or NULL is passed, false otherwise
 */
CT_EXTERN bool ct_listener_is_closed(const ct_listener_t* listener);

/**
 * @ingroup listener
 * @brief Get the callback context associated with a listener.
 * @param[in] listener Listener to get context from
 * @return Pointer to callback context, or NULL if no context is set
 */
CT_EXTERN void* ct_listener_get_callback_context(const ct_listener_t* listener);

/**
 * @ingroup listener
 * @brief Close a listener and stop accepting new connections.
 * @param[in] listener Listener to close
 */
CT_EXTERN void ct_listener_close(ct_listener_t* listener);

/**
 * @ingroup listener
 * @brief Free resources in a listener.
 * @param[in] listener Listener to free
 */
CT_EXTERN void ct_listener_free(ct_listener_t* listener);

/**
 * @ingroup library
 * @brief Initialize the CTaps library
 *
 * This function must be called before any other CTaps functions. It initializes
 * the event loop and sets default logging level
 *
 * @return 0 on success
 * @return negative error code on failure
 *
 * @note Must be called before ct_start_event_loop()
 * @note This function initializes global state and is not thread-safe
 *
 * @see ct_start_event_loop() for starting the event loop after initialization
 * @see ct_close() for cleanup and shutdown
 */
CT_EXTERN int ct_initialize(void);

/**
 * @ingroup library
 * @brief Start the CTaps event loop (blocking operation).
 *
 * @note Must be called after ct_initialize()
 * @note All CTaps callbacks are invoked from within this event loop's thread context
 * @note Returns when there are no more active and referenced handles
 *
 * @see ct_initialize() for library initialization
 */
CT_EXTERN void ct_start_event_loop(void);

/**
 * @ingroup library
 * @brief Close and cleanup the CTaps library.
 *
 * @return 0 on success
 * @return Non-zero error code on failure
 *
 * @see ct_initialize() for re-initializing the library
 */
CT_EXTERN int ct_close(void);

// =============================================================================
// Logging Configuration
// =============================================================================

/**
 * @ingroup logging
 * @brief Log level enumeration for filtering log output.
 *
 * Log levels range from TRACE (most verbose) to FATAL (critical errors only).
 * Setting a log level filters out all messages below that level.
 */
typedef enum {
    CT_LOG_TRACE = 0, ///< Trace-level debugging (most verbose)
    CT_LOG_DEBUG = 1, ///< Debug-level information
    CT_LOG_INFO = 2,  ///< Informational messages (default)
    CT_LOG_WARN = 3,  ///< Warning messages
    CT_LOG_ERROR = 4, ///< Error messages
    CT_LOG_FATAL = 5  ///< Fatal errors (least verbose)
} ct_log_level_enum_t;

/**
 * @ingroup logging
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
 * @see ct_log_level_enum_t for available log levels
 */
CT_EXTERN void ct_set_log_level(ct_log_level_enum_t level);

/**
 * @ingroup logging
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
CT_EXTERN int ct_add_log_file(const char* file_path, ct_log_level_enum_t min_level);

// =============================================================================
// Selection Properties - Transport property preferences for protocol selection
// =============================================================================
//

/**
 * @ingroup selection_properties
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
    PROHIBIT = -2, ///< Protocol MUST NOT have this property (eliminates candidates)
    AVOID,         ///< Prefer protocols without this property if possible
    NO_PREFERENCE, ///< No preference - property does not affect selection
    PREFER,        ///< Prefer protocols with this property if available
    REQUIRE,       ///< Protocol MUST have this property (eliminates candidates)
} ct_selection_preference_enum_t;


/**
 * @ingroup selection_properties
 * @brief Direction of communication for a connection.
 */
typedef enum {
    CT_DIRECTION_BIDIRECTIONAL,       ///< Two-way communication (send and receive)
    CT_DIRECTION_UNIDIRECTIONAL_SEND, ///< One-way, send only
    CT_DIRECTION_UNIDIRECTIONAL_RECV  ///< One-way, receive only
} ct_direction_of_communication_enum_t;

/**
 * @ingroup selection_properties
 * @brief Multipath transport modes.
 */
typedef enum {
    CT_MULTIPATH_DISABLED, ///< Do not use multipath
    CT_MULTIPATH_ACTIVE,   ///< Actively use multiple paths simultaneously
    CT_MULTIPATH_PASSIVE   ///< TBD
} ct_multipath_enum_t;

// Since it is a struct, wrap with {0}
#define EMPTY_PREFERENCE_SET_DEFAULT {0}
#define RUNTIME_DEPENDENT_DEFAULT 0

// clang-format off
#define get_selection_property_list(f)                                                                    \
f(RELIABILITY,                 "reliability",                ct_selection_preference_enum_t,           reliability,                 REQUIRE,                   TYPE_PREFERENCE)        \
f(PRESERVE_MSG_BOUNDARIES,     "preserveMsgBoundaries",      ct_selection_preference_enum_t,           preserve_msg_boundaries,     NO_PREFERENCE,             TYPE_PREFERENCE )  \
f(PER_MSG_RELIABILITY,         "perMsgReliability",          ct_selection_preference_enum_t,           per_msg_reliability,         NO_PREFERENCE,             TYPE_PREFERENCE)  \
f(PRESERVE_ORDER,              "preserveOrder",              ct_selection_preference_enum_t,           preserve_order,              REQUIRE,                   TYPE_PREFERENCE)        \
f(ZERO_RTT_MSG,                "zeroRttMsg",                 ct_selection_preference_enum_t,           zero_rtt_msg,                NO_PREFERENCE,             TYPE_PREFERENCE)  \
f(MULTISTREAMING,              "multistreaming",             ct_selection_preference_enum_t,           multistreaming,              PREFER,                    TYPE_PREFERENCE)         \
f(FULL_CHECKSUM_SEND,          "fullChecksumSend",           ct_selection_preference_enum_t,           full_checksum_send,          REQUIRE,                   TYPE_PREFERENCE)        \
f(FULL_CHECKSUM_RECV,          "fullChecksumRecv",           ct_selection_preference_enum_t,           full_checksum_recv,          REQUIRE,                   TYPE_PREFERENCE)        \
f(CONGESTION_CONTROL,          "congestionControl",          ct_selection_preference_enum_t,           congestion_control,          REQUIRE,                   TYPE_PREFERENCE)        \
f(KEEP_ALIVE,                  "keepAlive",                  ct_selection_preference_enum_t,           keep_alive,                  NO_PREFERENCE,             TYPE_PREFERENCE)  \
f(USE_TEMPORARY_LOCAL_ADDRESS, "useTemporaryLocalAddress",   ct_selection_preference_enum_t,           use_temporary_local_address, RUNTIME_DEPENDENT_DEFAULT, TYPE_PREFERENCE)         \
f(MULTIPATH,                   "multipath",                  ct_multipath_enum_t,                      multipath,                   RUNTIME_DEPENDENT_DEFAULT, TYPE_ENUM)  \
f(ADVERTISES_ALT_ADDRESS,      "advertisesAltAddr",          bool,                                advertises_alt_address,      false,                     TYPE_BOOL)  \
f(DIRECTION,                   "direction",                  ct_direction_of_communication_enum_t,     direction,                   CT_DIRECTION_BIDIRECTIONAL,   TYPE_ENUM)  \
f(SOFT_ERROR_NOTIFY,           "softErrorNotify",            ct_selection_preference_enum_t,           soft_error_notify,           NO_PREFERENCE,             TYPE_PREFERENCE)  \
f(ACTIVE_READ_BEFORE_SEND,     "activeReadBeforeSend",       ct_selection_preference_enum_t,           active_read_before_send,     NO_PREFERENCE,             TYPE_PREFERENCE)

#define get_preference_set_selection_property_list(f)                                              \
    f(INTERFACE, "interface", ct_preference_set_t, interface, EMPTY_PREFERENCE_SET_DEFAULT,        \
      TYPE_PREFERENCE_SET)                                                                         \
        f(PVD, "pvd", ct_preference_set_t, pvd, EMPTY_PREFERENCE_SET_DEFAULT, TYPE_PREFERENCE_SET)

#define output_enum(enum_name, string_name, property_type, token_name, default_value, type)        \
    enum_name,

// clang-format on

// =============================================================================
// Connection Properties
// =============================================================================
/**
 * @brief Collection of all connection properties.
 */
typedef struct ct_connection_properties_s ct_connection_properties_t;

/**
 * @ingroup connection_properties
 * @brief Special value: No timeout
 */
#define CT_CONN_TIMEOUT_DISABLED UINT32_MAX       ///< Special value: no timeout
/**
 * @ingroup connection_properties
 * @brief Special value: No rate limit
 */
#define CT_CONN_RATE_UNLIMITED UINT64_MAX         ///< Special value: no rate limit
/**
 * @ingroup connection_properties
 * @brief Special value: Full checksum
 */
#define CT_CONN_CHECKSUM_FULL_COVERAGE UINT32_MAX ///< Special value: checksum entire message
/**
 * @ingroup connection_properties
 * @brief Special value: No max message length
 */
#define CT_CONN_MSG_MAX_LEN_NOT_APPLICABLE 0      ///< Special value: no maximum length

/**
 * @ingroup connection_properties
 * @brief Connection lifecycle states.
 */
typedef enum {
    CT_CONN_STATE_INVALID = -1,     ///< Invalid state (used for error handling)
    CT_CONN_STATE_ESTABLISHING,     ///< Connection is being established
    CT_CONN_STATE_ESTABLISHED,      ///< Connection is ready for data transfer
    CT_CONN_STATE_CLOSING,          ///< Connection is closing gracefully
    CT_CONN_STATE_CLOSED            ///< Connection is fully closed
} ct_connection_state_enum_t;

/**
 * @ingroup connection_properties
 * @brief Connection scheduling algorithms for multipath.
 */
typedef enum {
    CT_CONN_SCHEDULER_WEIGHTED_FAIR_QUEUEING = 0, ///< Weighted fair queueing across paths
} ct_connection_scheduler_enum_t;

/**
 * @ingroup connection_properties
 * @brief QoS capacity profiles for traffic classification.
 */
typedef enum {
    CT_CAPACITY_PROFILE_BEST_EFFORT = 0,             ///< Default best-effort traffic
    CT_CAPACITY_PROFILE_SCAVENGER,                   ///< Background/bulk traffic
    CT_CAPACITY_PROFILE_LOW_LATENCY_INTERACTIVE,     ///< Interactive low-latency (e.g., gaming, VoIP)
    CT_CAPACITY_PROFILE_LOW_LATENCY_NON_INTERACTIVE, ///< Non-interactive low-latency (e.g., streaming)
    CT_CAPACITY_PROFILE_CONSTANT_RATE_STREAMING,     ///< Constant bitrate streaming
    CT_CAPACITY_PROFILE_CAPACITY_SEEKING             ///< Throughput-seeking traffic
} ct_capacity_profile_enum_t;

/**
 * @ingroup connection_properties
 * @brief Policies for multipath traffic distribution.
 */
typedef enum {
    CT_MULTIPATH_POLICY_HANDOVER = 0, ///< Use paths sequentially (failover only)
    CT_MULTIPATH_POLICY_INTERACTIVE,  ///< Optimize for low latency
    CT_MULTIPATH_POLICY_AGGREGATE     ///< Use all paths for maximum throughput
} ct_multipath_policy_enum_t;

// clang-format off
#define get_writable_connection_property_list(f)                                                                                   \
f(RECV_CHECKSUM_LEN,     "recvChecksumLen",     uint32_t,                       recv_checksum_len,     CT_CONN_CHECKSUM_FULL_COVERAGE,           TYPE_UINT32) \
f(CONN_PRIORITY,         "connPriority",        uint8_t,                        conn_priority,         CT_CONNECTION_DEFAULT_PRIORITY,           TYPE_UINT8) \
f(CONN_TIMEOUT,          "connTimeout",         uint32_t,                       conn_timeout,          CT_CONN_TIMEOUT_DISABLED,                 TYPE_UINT32) \
f(KEEP_ALIVE_TIMEOUT,    "keepAliveTimeout",    uint32_t,                       keep_alive_timeout,    CT_CONN_TIMEOUT_DISABLED,                 TYPE_UINT32) \
f(CONN_SCHEDULER,        "connScheduler",       ct_connection_scheduler_enum_t, conn_scheduler,        CT_CONN_SCHEDULER_WEIGHTED_FAIR_QUEUEING, TYPE_ENUM)  \
f(CONN_CAPACITY_PROFILE, "connCapacityProfile", ct_capacity_profile_enum_t,     conn_capacity_profile, CT_CAPACITY_PROFILE_BEST_EFFORT,          TYPE_ENUM)  \
f(MULTIPATH_POLICY,      "multipathPolicy",     ct_multipath_policy_enum_t,     multipath_policy,      CT_MULTIPATH_POLICY_HANDOVER,             TYPE_ENUM)  \
f(MIN_SEND_RATE,         "minSendRate",         uint64_t,                       min_send_rate,         CT_CONN_RATE_UNLIMITED,                   TYPE_UINT64) \
f(MIN_RECV_RATE,         "minRecvRate",         uint64_t,                       min_recv_rate,         CT_CONN_RATE_UNLIMITED,                   TYPE_UINT64) \
f(MAX_SEND_RATE,         "maxSendRate",         uint64_t,                       max_send_rate,         CT_CONN_RATE_UNLIMITED,                   TYPE_UINT64) \
f(MAX_RECV_RATE,         "maxRecvRate",         uint64_t,                       max_recv_rate,         CT_CONN_RATE_UNLIMITED,                   TYPE_UINT64) \
f(GROUP_CONN_LIMIT,      "groupConnLimit",      uint64_t,                       group_conn_limit,      CT_CONN_RATE_UNLIMITED,                   TYPE_UINT64) \
f(ISOLATE_SESSION,       "isolateSession",      bool,                           isolate_session,       false,                                 TYPE_BOOL)

#define get_read_only_connection_properties(f)                                                                                          \
f(SINGULAR_TRANSMISSION_MSG_MAX_LEN, "singularTransmissionMsgMaxLen", uint64_t,                   singular_transmission_msg_max_len, 0,     TYPE_UINT64) \
f(SEND_MESSAGE_MAX_LEN,              "sendMsgMaxLen",                 uint64_t,                   send_message_max_len,              0,     TYPE_UINT64) \
f(RECV_MESSAGE_MAX_LEN,              "recvMessageMaxLen",             uint64_t,                   recv_message_max_len,              0,     TYPE_UINT64)

#define get_tcp_connection_properties(f)                                                                              \
f(USER_TIMEOUT_VALUE_MS,   "userTimeoutValueMs",   uint32_t, user_timeout_value_ms,   TCP_USER_TIMEOUT, TYPE_UINT32) \
f(USER_TIMEOUT_ENABLED,    "userTimeoutEnabled",    bool,     user_timeout_enabled,    false,            TYPE_BOOL)   \
f(USER_TIMEOUT_CHANGEABLE, "userTimeoutChangeable", bool,     user_timeout_changeable, true,             TYPE_BOOL)

// =============================================================================
// Transport Properties - Combination of selection and connection properties
// =============================================================================

/**
 * @ingroup transport_properties
 * @brief Opaque handle representing a listener used for selecting and configuring protocols.
 *
 * Allocate a new instance using ct_transport_properties_new().
 * Use setter functions to configure properties, then pass to ct_preconnection_new() or similar.
 */
typedef struct ct_transport_properties_s ct_transport_properties_t;

#define output_transport_property_getter_declaration(enum_name, string_name, property_type,        \
                                                     token_name, default_value, type_enum)         \
    CT_EXTERN property_type ct_transport_properties_get_##token_name(                              \
        const ct_transport_properties_t* transport_props);

#define output_transport_property_preference_getter_declaration(                                   \
    enum_name, string_name, property_type, token_name, default_value, type)                        \
    CT_EXTERN ct_selection_preference_enum_t ct_transport_properties_get_##token_name##_preference(     \
        const ct_transport_properties_t* transport_props, const char* value);

#define output_transport_property_setter_declaration(enum_name, string_name, property_type,        \
                                                     token_name, default_value, type_enum)         \
    CT_EXTERN void ct_transport_properties_set_##token_name(                                       \
        ct_transport_properties_t* transport_props, property_type val);

#define output_transport_property_preference_set_adder(enum_name, string_name, property_type,      \
                                                       token_name, default_value, type)            \
    CT_EXTERN int ct_transport_properties_add_##token_name##_preference(                           \
        ct_transport_properties_t* transport_props, const char* value,                             \
        ct_selection_preference_enum_t preference);

/** @addtogroup selection_properties
 *  @{
 */
get_selection_property_list(output_transport_property_getter_declaration)
get_selection_property_list(output_transport_property_setter_declaration)

get_preference_set_selection_property_list(
    output_transport_property_preference_getter_declaration)
get_preference_set_selection_property_list(
    output_transport_property_preference_set_adder)
/** @} */



/** @addtogroup connection_properties 
 *  @{
 */
get_writable_connection_property_list(output_transport_property_getter_declaration)
get_writable_connection_property_list(
    output_transport_property_setter_declaration)

get_tcp_connection_properties(output_transport_property_getter_declaration)
get_tcp_connection_properties(
    output_transport_property_setter_declaration)

get_read_only_connection_properties(
    output_transport_property_getter_declaration)
/** @} */

// clang-format on

// =============================================================================
// Message properties
// =============================================================================

/**
 * @ingroup message_properties
 * @brief Collection of message properties for per-message transmission control.
 *
 * ## Message Properties Ownership Model
 *
 * ### Lifecycle
 * - Create with ct_message_properties_new()
 * - Embed in a message context with ct_message_context_new()
 *   - This takes a deep copy internally
 * - Free your copy with ct_message_properties_free() when done
 * - CTaps-owned copies are freed automatically
 */
typedef struct ct_message_properties_s ct_message_properties_t;

/**
 * @ingroup message_properties
 * @brief Special value: Full checksum coverage for individual message.
 */
#define CT_MESSAGE_CHECKSUM_FULL_COVERAGE UINT32_MAX

// clang-format off
#define get_message_property_list(f)                                                                                                    \
f(MSG_LIFETIME,          "msgLifetime",         uint64_t,                   lifetime,          0,                             TYPE_UINT64) \
f(MSG_PRIORITY,          "msgPriority",         uint32_t,                   priority,          100,                           TYPE_UINT32) \
f(MSG_ORDERED,           "msgOrdered",          bool,                       ordered,           true,                          TYPE_BOOL)   \
f(MSG_SAFELY_REPLAYABLE, "msgSafelyReplayable", bool,                       safely_replayable, false,                         TYPE_BOOL)   \
f(FINAL,                 "final",               bool,                       final,             false,                         TYPE_BOOL)   \
f(MSG_CHECKSUM_LEN,      "msgChecksumLen",      uint32_t,                   checksum_len,      CT_MESSAGE_CHECKSUM_FULL_COVERAGE, TYPE_UINT32) \
f(MSG_RELIABLE,          "msgReliable",         bool,                       reliable,          true,                          TYPE_BOOL)   \
f(MSG_CAPACITY_PROFILE,  "msgCapacityProfile",  ct_capacity_profile_enum_t, capacity_profile,  CT_CAPACITY_PROFILE_BEST_EFFORT,  TYPE_ENUM)   \
f(NO_FRAGMENTATION,      "noFragmentation",     bool,                       no_fragmentation,  false,                         TYPE_BOOL)   \
f(NO_SEGMENTATION,       "noSegmentation",      bool,                       no_segmentation,   false,                         TYPE_BOOL)

#define output_message_property_getter_declaration(enum_name, string_name, property_type,          \
                                                   token_name, default_value, type_enum)           \
    CT_EXTERN property_type ct_message_properties_get_##token_name(                                \
        const ct_message_properties_t* msg_props);

#define output_message_property_setter_declaration(enum_name, string_name, property_type,          \
                                                   token_name, default_value, type_enum)           \
    CT_EXTERN void ct_message_properties_set_##token_name(ct_message_properties_t* msg_props,      \
                                                          property_type val);

get_message_property_list(output_message_property_getter_declaration)
    get_message_property_list(output_message_property_setter_declaration)

// clang-format on

/**
 * @ingroup message_properties
 * @brief Create a new message properties object with default values.
 *
 * Allocates and initializes a new message properties object on the heap.
 * The returned object must be freed with ct_message_properties_free().
 *
 * @return Pointer to newly allocated message properties, or NULL on allocation failure.
 */
CT_EXTERN ct_message_properties_t* ct_message_properties_new(void);

/**
 * @ingroup message_properties
 * @brief Free resources in message properties.
 * @param[in] message_properties structure to free
 */
CT_EXTERN void ct_message_properties_free(ct_message_properties_t* message_properties);

// =============================================================================
// Security Parameters
// =============================================================================

/**
 * @brief Collection of all security parameters.
 *
 * @brief Opaque handle representing a security parameters used to configure security settings for connections and listeners.
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
 * - Pass to preconnection/connection functions
 *   - This makes a deep copy internally
 * - Free your copy with ct_security_parameters_free() when done
 * - CTaps-owned copies are freed automatically
 */
typedef struct ct_security_parameters_s ct_security_parameters_t;

/**
 * @ingroup security_parameters
 * @brief Set the path to the ticket store for QUIC session resumption.
 *
 * Optional parameter, but needed if you want client session resumption.
 *
 * Frees any existing ticket store path
 *
 * Takes a deep copy of the provided string.
 *
 * @param[in] sec Pointer to security parameters object to configure
 * @param[in] ticket_store_path Filesystem path to the ticket store (e.g., "quic_tickets.dat")
 * @return 0 on success, -ENOMEM on allocation failure or -EINVAL on invalid parameters
 */
CT_EXTERN int ct_security_parameters_set_ticket_store_path(ct_security_parameters_t* sec,
                                                           const char* ticket_store_path);

/**
 * @ingroup security_parameters
 * @brief Set the server name identification (SNI) for TLS connections.
 *
 * Frees any existing server name identification value.
 *
 * @param[in] sec Pointer to security parameters object to configure
 * @param[in] sni Server name for TLS SNI extension (e.g., "example.com")
 * @return 0 on success, -ENOMEM on allocation failure or -EINVAL on invalid parameters
 */
CT_EXTERN int ct_security_parameters_set_server_name_identification(ct_security_parameters_t* sec,
                                                                    const char* sni);

/**
 * @ingroup security_parameters
 * @brief Add a server certificate and private key for TLS connections.
 * @param[in] sec Pointer to security parameters object to configure
 * @param[in] cert_file Filesystem path to the certificate file (PEM format)
 * @param[in] key_file Filesystem path to the private key file (PEM format), or NULL if not applicable
 * @return 0 on success, -ENOMEM on allocation failure, -EINVAL on invalid parameters
 */
CT_EXTERN int ct_security_parameters_add_server_certificate(ct_security_parameters_t* sec,
                                                            const char* cert_file,
                                                            const char* key_file);

/**
 * @ingroup security_parameters
 * @brief Add a client certificate and private key for TLS connections.
 * @param[in] sec Pointer to security parameters object to configure
 * @param[in] cert_file Filesystem path to the certificate file (PEM format)
 * @param[in] key_file Filesystem path to the private key file (PEM format), or NULL if not applicable
 * @return 0 on success, -ENOMEM on allocation failure, -EINVAL on invalid parameters
 */
CT_EXTERN int ct_security_parameters_add_client_certificate(ct_security_parameters_t* sec,
                                                            const char* cert_file,
                                                            const char* key_file);
/**
 * @ingroup security_parameters
 * @brief Add an ALPN protocol identifier to the list of supported ALPNs for TLS connections.
 * @param[in] sec Pointer to security parameters object to configure
 * @param[in] alpn ALPN protocol identifier string (e.g., "h3-29")
 * @return 0 on success, -ENOMEM on allocation failure, -EINVAL on invalid parameters
 */
CT_EXTERN int ct_security_parameters_add_alpn(ct_security_parameters_t* sec, const char* alpn);
/**
 * @ingroup security_parameters
 * @brief Free and clear all configured ALPN protocol identifiers from the security parameters.
 * @param[in] sec Pointer to security parameters object to configure
 * @return 0 on success, -EINVAL if sec is NULL
 */
CT_EXTERN int ct_security_parameters_clear_alpn(ct_security_parameters_t* sec);

/**
 * @ingroup security_parameters
 * @brief Set the session ticket encryption key for QUIC session resumption.
 *
 * This is an optional parameter, but needed if you want server session resumption.
 *
 * Frees any existing session ticket encryption key.
 *
 * Takes a deep copy of the provided key data.
 *
 * @param[in] sec Pointer to security parameters object to configure
 * @param[in] key Binary key data for encrypting session tickets
 * @param[in] key_len Length of the key data in bytes
 * @return 0 on success, -ENOMEM on allocation failure, -EINVAL on invalid parameters
 */
CT_EXTERN int
ct_security_parameters_set_session_ticket_encryption_key(ct_security_parameters_t* sec,
                                                         const uint8_t* key,
                                                         size_t key_len);

/**
 * @brief Allocate a new security parameters object on the heap.
 * @return Pointer to newly allocated security parameters, or NULL on failure
 */
CT_EXTERN ct_security_parameters_t* ct_security_parameters_new(void);

/**
 * @brief Free resources in security parameters including the structure itself.
 * @param[in] security_parameters structure to free
 */
CT_EXTERN void ct_security_parameters_free(ct_security_parameters_t* security_parameters);


/** @addtogroup security_parameters
 *  @{
 */
CT_EXTERN const char*
ct_security_parameters_get_ticket_store_path(const ct_security_parameters_t* sec);
CT_EXTERN const char*
ct_security_parameters_get_server_name_identification(const ct_security_parameters_t* sec);

CT_EXTERN size_t
ct_security_parameters_get_server_certificate_count(const ct_security_parameters_t* sec);
CT_EXTERN const char*
ct_security_parameters_get_server_certificate_file(const ct_security_parameters_t* sec,
                                                   size_t index);
CT_EXTERN const char*
ct_security_parameters_get_server_certificate_key_file(const ct_security_parameters_t* sec,
                                                       size_t index);

CT_EXTERN size_t
ct_security_parameters_get_client_certificate_count(const ct_security_parameters_t* sec);
CT_EXTERN const char*
ct_security_parameters_get_client_certificate_file(const ct_security_parameters_t* sec,
                                                   size_t index);
CT_EXTERN const char*
ct_security_parameters_get_client_certificate_key_file(const ct_security_parameters_t* sec,
                                                       size_t index);

CT_EXTERN const char** ct_security_parameters_get_alpns(const ct_security_parameters_t* sec,
                                                        size_t* num_alpns);
CT_EXTERN const uint8_t*
ct_security_parameters_get_session_ticket_encryption_key(const ct_security_parameters_t* sec,
                                                         size_t* key_len);
/** @} */

/**
 * @ingroup transport_properties
 * @brief Create a new transport properties object with default values.
 * @note The returned object must be freed with ct_transport_properties_free().
 *
 * @return Pointer to newly allocated transport properties, or NULL on allocation failure.
 */
CT_EXTERN ct_transport_properties_t* ct_transport_properties_new(void);

/**
 * @ingroup transport_properties
 * @brief Free a transport properties object.
 *
 * @param[in] props Pointer to transport properties to free. Does nothing if NULL.
 */
CT_EXTERN void ct_transport_properties_free(ct_transport_properties_t* props);

// =============================================================================
// Endpoints - Opaque types for local and remote endpoints
// =============================================================================

/**
 * @ingroup local_endpoints
 * @brief Opaque handle representing a local endpoint (generic or resolved to specific ip address and port).
 *
 * Use ct_local_endpoint_new() to create and ct_local_endpoint_with_*() to configure.
 */
typedef struct ct_local_endpoint_s ct_local_endpoint_t;

/**
 * @ingroup remote_endpoints
 * @brief Opaque handle representing a remote endpoint (generic or resolved to specific ip address and port).
 *
 * Use ct_remote_endpoint_new() to create and ct_remote_endpoint_with_*() to configure.
 */
typedef struct ct_remote_endpoint_s ct_remote_endpoint_t;

/**
 * @ingroup local_endpoints
 * @brief Create a new heap-allocated local endpoint.
 *
 * The caller owns the returned endpoint and must free it with ct_local_endpoint_free()
 * when done.
 *
 * @return Pointer to newly allocated endpoint, or NULL on error
 * @see endpoint_ownership
 */
CT_EXTERN ct_local_endpoint_t* ct_local_endpoint_new(void);

/**
 * @ingroup local_endpoints
 * @brief Set the network interface for a local endpoint.
 * @param[in,out] local_endpoint Endpoint to modify
 * @param[in] interface_name Interface name (e.g., "eth0", "wlan0")
 * @return 0 on success, negative error code on failure
 */
CT_EXTERN int ct_local_endpoint_with_interface(ct_local_endpoint_t* local_endpoint,
                                               const char* interface_name);

/**
 * @ingroup local_endpoints
 * @brief Set the port number for a local endpoint.
 * @param[in,out] local_endpoint Endpoint to modify
 * @param[in] port Port number
 */
CT_EXTERN void ct_local_endpoint_with_port(ct_local_endpoint_t* local_endpoint, uint16_t port);

/**
 * @ingroup local_endpoints
 * @brief Set the service name for a local endpoint.
 *
 * Takes a deep copy of the provided string.
 *
 * @param[in,out] local_endpoint Endpoint to modify
 * @param[in] service Service name (e.g., "http", "https")
 * @return 0 on success, non-zero on error
 */
CT_EXTERN int ct_local_endpoint_with_service(ct_local_endpoint_t* local_endpoint, const char* service);

/**
 * @ingroup local_endpoints
 * @brief Set the IPv4 address for a local endpoint.
 * @param[in,out] local_endpoint Endpoint to modify
 * @param[in] ipv4_addr IPv4 address in network byte order
 * @return 0 on success, non-zero on error
 */
CT_EXTERN int ct_local_endpoint_with_ipv4(ct_local_endpoint_t* local_endpoint, in_addr_t ipv4_addr);

/**
 * @ingroup local_endpoints
 * @brief Set the IPv6 address for a local endpoint.
 * @param[in,out] local_endpoint Endpoint to modify
 * @param[in] ipv6_addr IPv6 address in network byte order
 * @return 0 on success, non-zero on error
 */
CT_EXTERN int ct_local_endpoint_with_ipv6(ct_local_endpoint_t* local_endpoint,
                                           struct in6_addr ipv6_addr);

/**
 * @ingroup local_endpoints
 * @brief Initialize a local endpoint from a sockaddr structure.
 *
 * @note caller retains ownership of passed sockaddr structure
 *
 * @param[out] local_endpoint Endpoint to initialize
 * @param[in] addr Socket address structure
 * @return 0 on success, non-zero on error
 */
CT_EXTERN int ct_local_endpoint_from_sockaddr(ct_local_endpoint_t* local_endpoint,
                                              const struct sockaddr_storage* addr);
/**
 * @ingroup local_endpoints
 * @brief Free all resources in a local endpoint including the structure itself.
 * @param[in] local_endpoint Endpoint to free
 */
CT_EXTERN void ct_local_endpoint_free(ct_local_endpoint_t* local_endpoint);

/**
 * @ingroup local_endpoints
 * @brief Create a heap-allocated copy of a local endpoint.
 * @param[in] local_endpoint Source endpoint
 * @return Pointer to newly allocated copy, or NULL on error
 */
CT_EXTERN ct_local_endpoint_t* ct_local_endpoint_deep_copy(const ct_local_endpoint_t* local_endpoint);

/**
 * @ingroup local_endpoints
 * @brief Get the service for a local endpoint
 * @param[in] local_endpoint endpoint to get service for
 *
 * @return char* pointer to service name, NULL if endpoint is null, or if service is not set
 */
CT_EXTERN const char* ct_local_endpoint_get_service(const ct_local_endpoint_t* local_endpoint);

/**
 * @ingroup local_endpoints
 * @brief Get the resolved port for a local endpoint after binding.
 *
 * @return Port number assigned to the local endpoint in host order
 */
CT_EXTERN uint16_t ct_local_endpoint_get_resolved_port(const ct_local_endpoint_t* local_endpoint);


/**
 * @ingroup remote_endpoints
 * @brief Create a new heap-allocated remote endpoint.
 *
 * The caller owns the returned endpoint and must free it with ct_remote_endpoint_free()
 * when done.
 *
 * @return Pointer to newly allocated endpoint, or NULL on error
 */
CT_EXTERN ct_remote_endpoint_t* ct_remote_endpoint_new(void);

/**
 * @ingroup remote_endpoints
 * @brief Set the hostname for a remote endpoint.
 * @param[in,out] remote_endpoint Endpoint to modify
 * @param[in] hostname Hostname or IP address string
 * @return 0 on success, non-zero on error
 */
CT_EXTERN int ct_remote_endpoint_with_hostname(ct_remote_endpoint_t* remote_endpoint,
                                               const char* hostname);

/**
 * @ingroup remote_endpoints
 * @brief Set the port number for a remote endpoint.
 * @param[in,out] remote_endpoint Endpoint to modify
 * @param[in] port Port number
 */
CT_EXTERN void ct_remote_endpoint_with_port(ct_remote_endpoint_t* remote_endpoint, uint16_t port);

/**
 * @ingroup remote_endpoints
 * @brief Set the service name for a remote endpoint.
 * @param[in,out] remote_endpoint Endpoint to modify
 * @param[in] service Service name (e.g., "http", "https")
 * @return 0 on success, non-zero on error
 */
CT_EXTERN int ct_remote_endpoint_with_service(ct_remote_endpoint_t* remote_endpoint,
                                              const char* service);

/**
 * @ingroup remote_endpoints
 * @brief Free string fields in a remote endpoint without freeing the structure.
 * @param[in] remote_endpoint Endpoint whose strings to free
 */
CT_EXTERN void ct_remote_endpoint_free_content(ct_remote_endpoint_t* remote_endpoint);

/**
 * @ingroup remote_endpoints
 * @brief Free all resources in a remote endpoint including the structure itself.
 * @param[in] remote_endpoint Endpoint to free
 */
CT_EXTERN void ct_remote_endpoint_free(ct_remote_endpoint_t* remote_endpoint);

/**
 * @ingroup remote_endpoints
 * @brief Initialize a remote endpoint from a sockaddr structure.
 * @param[out] remote_endpoint Endpoint to initialize
 * @param[in] addr Socket address structure
 * @return 0 on success, non-zero on error
 */
CT_EXTERN int ct_remote_endpoint_from_sockaddr(ct_remote_endpoint_t* remote_endpoint,
                                               const struct sockaddr_storage* addr);

/**
 * @ingroup remote_endpoints
 * @brief Create a heap-allocated copy of a remote endpoint.
 * @param[in] remote_endpoint Source endpoint
 * @return Pointer to newly allocated copy, or NULL on error
 */
CT_EXTERN ct_remote_endpoint_t* ct_remote_endpoint_deep_copy(const ct_remote_endpoint_t* remote_endpoint);

/**
 * @ingroup remote_endpoints
 * @brief Set the IPv4 address for a remote endpoint.
 * @param[in,out] remote_endpoint Endpoint to modify
 * @param[in] ipv4_addr IPv4 address in network byte order
 * @return 0 on success, non-zero on error
 */
CT_EXTERN int ct_remote_endpoint_with_ipv4(ct_remote_endpoint_t* remote_endpoint,
                                           in_addr_t ipv4_addr);

/**
 * @ingroup remote_endpoints
 * @brief Set the IPv6 address for a remote endpoint.
 * @param[in,out] remote_endpoint Endpoint to modify
 * @param[in] ipv6_addr IPv6 address structure
 * @return 0 on success, non-zero on error
 */
CT_EXTERN int ct_remote_endpoint_with_ipv6(ct_remote_endpoint_t* remote_endpoint,
                                           struct in6_addr ipv6_addr);

/**
 * @ingroup remote_endpoints
 * @brief Get the service for a remote endpoint
 * @param[in] remote_endpoint endpoint to get service for
 *
 * @return char* pointer to service name, NULL if endpoint is null, or if service is not set
 */
CT_EXTERN const char* ct_remote_endpoint_get_service(const ct_remote_endpoint_t* remote_endpoint);

// =============================================================================
// Messages - Message and message context structures
// =============================================================================

/**
 * @ingroup message
 * @brief Opaque handle representing a single message to be sent or received.
 *
 * Use ct_message_new()/ct_message_new_with_content() to create, and ct_message_free() to free.
 *
 * ## Message Ownership Model
 *
 * ### Sending Messages
 * When you send a message using ct_send_message() or ct_send_message_full():
 * - **You retain ownership** of your original message
 * - CTaps makes a **deep copy** internally for transmission
 * - **You can free your message immediately** after the send function returns
 * - CTaps manages the lifecycle of its internal copy
 *
 * ### Receiving Messages
 *
 * When you receive a message in a receive callback:
 * - The message is only valid during the callback execution,
 *   you must take a deep copy if you need to use it after the callback returns
 *
 * ### Example
 * @code{.c}
 * ct_message_t* msg = ct_message_new_with_content("Hello", 5);
 * ct_send_message(connection, msg);
 * ct_message_free(msg);  // Safe to free immediately after send
 * @endcode
 */
typedef struct ct_message_s ct_message_t;

/**
 * @ingroup message
 * @brief Free all resources in a message including the structure.
 * @param[in] message Message to free
 */
CT_EXTERN void ct_message_free(ct_message_t* message);

/**
 * @ingroup message
 *
 * @note The caller maintains ownership even after passing to send functions,
 * CTaps makes deep copies internally.
 *
 * @brief Allocate a new message on the heap.
 * @return Pointer to newly allocated message, or NULL on failure
 */
CT_EXTERN ct_message_t* ct_message_new(void);

/**
 * @ingroup message
 * @brief Allocate a new message with content.
 * @param[in] content Data buffer for the message
 * @param[in] length Length of data in bytes
 * @return Pointer to newly allocated message with content, or NULL on failure
 */
CT_EXTERN ct_message_t* ct_message_new_with_content(const char* content, size_t length);

/**
 * @ingroup message
 * @brief Get the length of a message.
 * @param[in] message Message to query
 * @return Length of message in bytes, or 0 if message is NULL
 */
CT_EXTERN size_t ct_message_get_length(const ct_message_t* message);


/**
 * @ingroup message
 * @brief Create a deep copy of a message, including its content.
 * @param[in] message Message to copy
 * @return Pointer to newly allocated copy of the message, or NULL on failure
 */
CT_EXTERN ct_message_t* ct_message_deep_copy(const ct_message_t* message);

/**
 * @ingroup message
 * @brief Get the content buffer of a message.
 * @param[in] message Message to query
 * @return Pointer to message content, or NULL if message is NULL
 */
CT_EXTERN const char* ct_message_get_content(const ct_message_t* message);

/**
 * @ingroup message
 * @brief Set the content of a message, replacing any existing content.
 * @param[in,out] message Message to modify
 * @param[in] content New data buffer for the message
 * @param[in] length Length of new data in bytes
 */
CT_EXTERN void ct_message_set_content(ct_message_t* message, const char* content, size_t length);

/**
 * @ingroup message
 * @brief Opaque handle representing message metadata to pass to sending protocol.
 *
 * ## Message Context Ownership Model
 *
 * ### Passing to Sending Functions
 * When you pass a message context to ct_send_message_full() or similar functions:
 * - **You retain ownership** of your original message_context
 * - CTaps makes a **deep copy** internally if it needs to store the context
 * - **You can free your message_context** after the function returns
 *
 * ### Receiving in Callbacks
 * When a message context is passed to your receive callback:
 * - The message context is only valid during the callback execution,
 *   you must take a deep copy if you need to use it after the callback returns
 *
 *
 * @code{.c}
 * ct_message_t* msg = ct_message_new_with_content("Hello", 5);
 * ct_message_context_t* msg_ctx = ct_message_context_new();
 * ct_send_message_full(connection, msg, msg_ctx);
 * ct_message_free(msg);
 * ct_message_context_free(msg_ctx);  // Safe to free immediately after send
 * @endcode
 *
 */
typedef struct ct_message_context_s ct_message_context_t;

/**
 * @ingroup message_context
 * @brief Initialize a message context with default values.
 * @return Heap allocated empty message context
 */
CT_EXTERN ct_message_context_t* ct_message_context_new(void);

/**
 * @ingroup message_context
 * @brief Free resources in a message context.
 * @param[in] message_context Context to free
 */
CT_EXTERN void ct_message_context_free(ct_message_context_t* message_context);

/**
 * @ingroup message_context
 * @brief Get message properties from a message context.
 *
 * Returns a pointer to the message properties contained in the message context.
 * The returned pointer is owned by the message context and should not be freed.
 *
 * @param[in] message_context Context to get properties from
 * @return Pointer to message properties, or NULL if message_context is NULL
 */
CT_EXTERN const ct_message_properties_t*
ct_message_context_get_message_properties(const ct_message_context_t* message_context);

/**
 * @ingroup message_context
 * @brief Get the remote endpoint from a message context.
 *
 * @param[in] message_context Context to get remote endpoint from
 * @return Pointer to remote endpoint, or NULL if message_context is NULL or remote endpoint not set
 */
CT_EXTERN const ct_remote_endpoint_t*
ct_message_context_get_remote_endpoint(const ct_message_context_t* message_context);

/**
 * @ingroup message_context
 * @brief Get the local endpoint from a message context.
 *
 * @param[in] message_context Context to get local endpoint from
 * @return Pointer to local endpoint, or NULL if message_context is NULL or local endpoint not set
 */
CT_EXTERN const ct_local_endpoint_t*
ct_message_context_get_local_endpoint(const ct_message_context_t* message_context);

/**
 * @ingroup message_context
 * @brief Get the receive context from a message context.
 *
 * The receive context is a user-provided pointer that can be set in the ct_receive_callbacks_t structure.
 * It is intended to allow users to associate custom data with a specific receive callback invocation.
 *
 * @see ct_receive_callbacks_t::per_receive_context
 *
 * @param[in] message_context Context to get receive context from
 * @return User-provided receive context pointer, or NULL if message_context is NULL or no receive context set
 */
CT_EXTERN void* ct_message_context_get_receive_context(const ct_message_context_t* message_context);


#define output_message_context_getter_declaration(enum_name, string_name, property_type,           \
                                                  token_name, default_value, type_enum)            \
    CT_EXTERN property_type ct_message_context_get_##token_name(                                   \
        const ct_message_context_t* msg_ctx);

#define output_message_context_setter_declaration(enum_name, string_name, property_type,           \
                                                  token_name, default_value, type_enum)            \
    CT_EXTERN void ct_message_context_set_##token_name(ct_message_context_t* msg_ctx,              \
                                                       property_type val);

get_message_property_list(output_message_context_getter_declaration)
    get_message_property_list(output_message_context_setter_declaration)

// =============================================================================
// Callbacks - Connection and Listener callback structures
// =============================================================================

/**
 * @ingroup connection
 * @brief Callback functions for receiving messages on a connection.
 *
 * Set these callbacks via ct_receive_message() to handle incoming data.
 */
typedef struct ct_receive_callbacks_s {
  /** @brief Called when a complete message is received.
   * @param[in] connection The connection that received the message
   * @param[in] received_message Pointer to received message. Only valid during callback execution - copy if needed after return.
   * @param[in] ctx Message context with properties and endpoints
   */
    void (*receive_callback)(ct_connection_t* connection, ct_message_t* received_message,
                            ct_message_context_t* ctx);

    /** @brief Called when a receive error occurs.
   * @param[in] connection The connection that experienced the error
   * @param[in] ctx Message context
   * @param[in] reason Error description string
   */
    void (*receive_error)(ct_connection_t* connection, ct_message_context_t* ctx,
                         const char* reason);

    /**
     * @brief Per-receive user context accessible when this specific callback is invoked.
     * Can be fetched within ct_message_context_get_receive_context(ctx)
     *
     * @code{.c}
     *
     * void check_specific_callback_was_invoked(ct_connection_t* connection,
     *                                       ct_message_t* received_message,
     *                                       ct_message_context_t* message_context) {
     *   my_custom_struct* recv_ctx = ct_message_context_get_receive_context(message_context);
     *   recv_ctx->was_invoked = true;
     * }
     * @endcode
     */
    void* per_receive_context;
} ct_receive_callbacks_t;

/**
 * @ingroup connection
 * @brief Callback functions for connection lifecycle events.
 *
 * Set these callbacks via ct_preconnection_initiate() or ct_preconnection_listen().
 * All callbacks are invoked from the event loop thread.
 */
typedef struct ct_connection_callbacks_s {
    /** @brief Called when a connection error occurs after establishment. */
    void (*connection_error)(ct_connection_t* connection);

    /** @brief Called when connection establishment fails. */
    void (*establishment_error)(ct_connection_t* connection);

    /** @brief Called when a connection expires (e.g., idle timeout). */
    void (*expired)(ct_connection_t* connection);

    /** @brief Called when the connection's network path changes (multipath). */
    void (*path_change)(ct_connection_t* connection);

    /** @brief Called when connection is established and ready for data transfer. */
    void (*ready)(ct_connection_t* connection);

    /** @brief Called when connection is established and ready for data transfer. */
    void (*closed)(ct_connection_t* connection);

    /** @brief Called when a message send operation fails. */
    void (*send_error)(ct_connection_t* connection, ct_message_context_t* message_context,
                       int reason_code);

    /** @brief Called when a sent message is acknowledged by the transport. */
    void (*sent)(ct_connection_t* connection, ct_message_context_t* message_context);

    /** @brief Called when a non-fatal error occurs (e.g., congestion). */
    void (*soft_error)(ct_connection_t* connection);

    /**
     * Per connection context accessible whenever a given connection is
     * passed to a callback. 
     *
     * Can be fetched with ct_connection_get_callback_context(connection)
     *
     * @code{.c}
     * void count_number_of_received_messages(ct_connection_t* connection,
     *                                       ct_message_t* received_message,
     *                                       ct_message_context_t* message_context) {
     *   my_custom_struct* recv_ctx = ct_connection_get_callback_context(connection);
     *   recv_ctx->num_received_messages++;
     * }
     * @endcode
     */
    void* per_connection_context; ///< User-provided context for the connection lifetime
} ct_connection_callbacks_t;

/**
 * @ingroup listener
 * @brief Callback functions for listener events.
 *
 * Set these callbacks via ct_preconnection_listen().
 */
typedef struct ct_listener_callbacks_s {

    /*** 
   * @brief Called when the listener starts listening and is ready to accept connections.
   *
   * Listener creation is currently synchronous, so this callback is invoked immediately
   * within ct_preconnection_listen()
   */
    void (*listener_ready)(ct_listener_t* listener);

    /** @brief Called when a new connection is received. */
    void (*connection_received)(ct_listener_t* listener, ct_connection_t* new_conn);

    /** 
   * @brief Called when connection establishment fails for an incoming connection.
   * @param[in] listener The listener which failed, or NULL if a listener could not be created
   * @param[in] reason Error code
   */
    void (*establishment_error)(ct_listener_t* listener, int error_code);

    /** @brief Called when the listener has been closed and will accept no more connections. */
    void (*listener_closed)(ct_listener_t* listener);


    /**
     * Per listener context accessible whenever a given listener is
     * passed to a callback. 
     *
     * Can be fetched with ct_listener_get_callback_context(listener)
     *
     * @code{.c}
     *
     * void count_num_received_connections(ct_listener_t* listener,
     *                                     ct_connection_t* new_connection) {
     *   my_custom_struct* recv_ctx = ct_listener_get_callback_context(listener);
     *   recv_ctx->num_received_messages++;
     * }
     * @endcode
     */
    void* per_listener_context;
} ct_listener_callbacks_t;

// =============================================================================
// Message Framer - Optional message framing/parsing layer
// =============================================================================

/**
 * @ingroup framer
 * @brief Opaque handle representing a framer layer, wrapping or unwrapping sent/received
 * messages.
 *
 * Useful in for example HTTP TCP
 *
 * Currently only one framer is supported per connection.
 */
typedef struct ct_framer_impl_s ct_framer_impl_t;

/**
 * @ingroup framer
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
 * @ingroup framer
 * @brief Callback invoked by framer when message decoding is complete.
 *
 * @param[in] connection The connection
 * @param[in] decoded_message The decoded message
 * @param[in] context Message context
 */
typedef void (*ct_framer_done_decoding_callback)(ct_connection_t* connection,
                                                 ct_message_t* decoded_message,
                                                 ct_message_context_t* context);

/**
 * @ingroup framer
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
    int (*encode_message)(ct_connection_t* connection, ct_message_t* message,
                          ct_message_context_t* context, ct_framer_done_encoding_callback callback);

  /**
   * @brief Decode inbound data into application messages.
   *
   * @param[in] connection The connection
   * @param[in] message received from transport layer
   * @param[in] context Message context containing endpoint info
   * @param[in] callback Callback to invoke when decoding is complete
   */
    void (*decode_data)(ct_connection_t* connection, ct_message_t* message,
                        ct_message_context_t* context, ct_framer_done_decoding_callback callback);
};


/**
 * @ingroup preconnection
 * @struct ct_preconnection_t
 * @brief Opaque handle representing a preconnection.
 *
 * Created before initiating a connection or listener, this object holds all configuration
 * (endpoints, properties, security) needed to initiate a connection or start a listener.
 *
 * Created via ct_preconnection_new().
 */
typedef struct ct_preconnection_s ct_preconnection_t;

/**
 * @ingroup preconnection
 * @brief Create a new preconnection with transport properties and endpoints.
 *
 * Allocates and initializes a new preconnection object on the heap.
 * The returned object must be freed with ct_preconnection_free().
 * This follows the RFC 9622 pattern: Preconnection := NewPreconnection(...)
 *
 * Takes deep copies of all passed endpoints and transport/security parameters.
 * The caller can therefore safely free or reuse the original parameters after this function returns.
 *
 * @param local_endpoints
 * @param num_local_endpoints
 * @param[in] remote_endpoints Array of remote endpoints to connect to, or NULL
 * @param[in] num_remote_endpoints Number of remote endpoints (0 if remote_endpoints is NULL)
 * @param[in] transport_properties Transport property preferences, or NULL for defaults
 * @param[in] security_parameters Security configuration (TLS/QUIC), or NULL
 * @return Pointer to newly allocated preconnection, or NULL on allocation failure
 */
CT_EXTERN ct_preconnection_t*
ct_preconnection_new(const ct_local_endpoint_t* local_endpoints, size_t num_local_endpoints,
                     const ct_remote_endpoint_t* remote_endpoints, size_t num_remote_endpoints,
                     const ct_transport_properties_t* transport_properties,
                     const ct_security_parameters_t* security_parameters);

/**
 * @ingroup preconnection
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
 * @ingroup preconnection
 * @brief Set a message framer for the preconnection.
 * @param[in,out] preconnection Preconnection to modify
 * @param[in] framer_impl Framer implementation to use, or NULL for no framing
 */
CT_EXTERN void ct_preconnection_set_framer(ct_preconnection_t* preconnection,
                                           ct_framer_impl_t* framer_impl);

/**
 * @ingroup preconnection
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
CT_EXTERN int ct_preconnection_initiate(ct_preconnection_t* preconnection,
                                        ct_connection_callbacks_t connection_callbacks);

/**
 * @ingroup preconnection
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
CT_EXTERN int ct_preconnection_initiate_with_send(ct_preconnection_t* preconnection,
                                                  ct_connection_callbacks_t connection_callbacks,
                                                  const ct_message_t* message,
                                                  const ct_message_context_t* message_context);

/**
 * @ingroup preconnection
 * @brief Start listening for incoming connections using the configured Preconnection.
 *
 * @param[in] preconnection Pointer to preconnection with listener configuration
 * @param[in] listener_callbacks Callbacks for listener events (ready, connection_received, etc.)
 * @param[in] connection_callbacks Callbacks for connection events on accepted connections
 * @return 0 on success, negative errno on synchronous failure
 *
 * @see ct_listener_close() to stop accepting new connections
 */
CT_EXTERN int ct_preconnection_listen(const ct_preconnection_t* preconnection,
                                      ct_listener_callbacks_t listener_callbacks,
                                      const ct_connection_callbacks_t* connection_callbacks);

/**
 * @ingroup connection
 * @brief Send a message over a connection with default properties.
 *
 * Takes a deep copy of the sent message, application can free or
 * reuse the original message after this function returns.
 *
 * @param[in] connection The connection to send on
 * @param[in] message The message to send
 * @return 0 on success, non-zero on error
 */
CT_EXTERN int ct_send_message(ct_connection_t* connection, const ct_message_t* message);

/**
 * @ingroup connection
 * @brief Send a message with custom message context and properties.
 *
 * Takes a deep copy of the sent message, application can free or
 * reuse the original message after this function returns.
 *
 * @param[in] connection The connection to send on
 * @param[in] message The message to send
 * @param[in] message_context Message properties and context
 * @return 0 on success, non-zero on error
 */
CT_EXTERN int ct_send_message_full(ct_connection_t* connection, const ct_message_t* message,
                                   const ct_message_context_t* message_context);

/**
 * @ingroup connection
 * @brief Register callbacks to receive messages on a connection.
 * @param[in] connection The connection to receive on
 * @param[in] receive_callbacks Callbacks for receive events
 * @return 0 on success, non-zero on error
 */
CT_EXTERN int ct_receive_message(ct_connection_t* connection,
                                 ct_receive_callbacks_t receive_callbacks);

/**
 * @ingroup connection
 * @brief get shared connection properties for a connection
 * @param[in] connection The connection to query
 * @return pointer to transport properties shared with connections in the same connection group, or NULL if connection is NULL
 */
CT_EXTERN const ct_transport_properties_t*
ct_connection_get_transport_properties(const ct_connection_t* connection);

/**
 * @ingroup connection
 * @brief Get relative priority when compared to other connections in the same group.
 *
 * Lower values are higher priority.
 *
 * Defaults to 100.
 *
 * @return Priority value, or UINT8_MAX if connection is NULL
 */
CT_EXTERN uint8_t ct_connection_get_priority(const ct_connection_t* connection);

/**
 * @ingroup connection
 * @brief Set relative priority for a connection compared to other connections in the same group.
 *
 * @return 0 if the priority was set successfully, non-zero on error (e.g., connection is NULL)
 * @note If a protocol does not support prioritization this does not return any error, but the value is not used.
 */
CT_EXTERN int ct_connection_set_priority(ct_connection_t* connection, uint8_t priority);

/**
 * @ingroup connection
 * @brief Get the connections callback context.
 * @param[in] connection connection to get callback context for
 * @return Void pointer assigned to callback context
 *         null if connection is null or it hasn't been set
 */
CT_EXTERN void* ct_connection_get_callback_context(const ct_connection_t* connection);

/**
 * @ingroup connection
 * @brief Get the UUID of a connection
 * @param[in] connection connection to get uuid for
 * @return Pointer to uuid string
 */
CT_EXTERN const char* ct_connection_get_uuid(const ct_connection_t* connection);

/**
 * @ingroup connection
 * @brief Get the name of the underlying protocol
 * @param[in] connection connection to get protocol name for
 * @return Pointer to protocol name, NULL of connection is NULL 
 */
CT_EXTERN const char* ct_connection_get_protocol_name(const ct_connection_t* connection);

/**
 * @ingroup connection
 * @brief Get the currently active remote endpoint for the connection
 * @param[in] connection connection to get remote endpoint for
 * @return Pointer to endpoint, NULL if connection is NULL 
 */
CT_EXTERN const ct_remote_endpoint_t*
ct_connection_get_active_remote_endpoint(const ct_connection_t* connection);

/**
 * @ingroup connection
 * @brief Get the currently active local endpoint
 * @param[in] connection connection to get remote endpoint for
 * @return Pointer to endpoint, NULL if connection is NULL 
 */
CT_EXTERN const ct_local_endpoint_t*
ct_connection_get_active_local_endpoint(const ct_connection_t* connection);

/**
 * @ingroup connection
 * @brief Get the current state of a connection.
 * @param[in] connection The connection to query
 * @return connection lifecycle state, -1 if connection is NULL
 */
CT_EXTERN ct_connection_state_enum_t ct_connection_get_state(const ct_connection_t* connection);

/**
 * @ingroup connection
 * @brief Check if a connection's state is CT_CONN_STATE_CLOSED.
 *
 * @see ct_connection_get_state() for a generic getter
 *
 * @param[in] connection The connection to check
 * @return true if connection is closed, false if open or connection is NULL
 */
CT_EXTERN bool ct_connection_is_closed(const ct_connection_t* connection);

/**
 * @ingroup connection
 * @brief Check if a connection's state is CT_CONN_STATE_ESTABLISHED.
 *
 * @see ct_connection_get_state() for a generic getter
 *
 * @param[in] connection The connection to check
 * @return true if connection state matches, false otherwise, including if connection is NULL
 */
CT_EXTERN bool ct_connection_is_established(const ct_connection_t* connection);

/**
 * @ingroup connection
 * @brief Check if a connection's state is CT_CONN_STATE_ESTABLISHING
 *
 * @see ct_connection_get_state() for a generic getter
 *
 * @param[in] connection The connection to check
 * @return true if connection state matches, false otherwise, including if connection is NULL
 */
CT_EXTERN bool ct_connection_is_establishing(const ct_connection_t* connection);

/**
 * @ingroup connection
 * @brief Check if a connection's state is CT_CONN_STATE_CLOSING
 *
 * @see ct_connection_get_state() for a generic getter
 *
 * @param[in] connection The connection to check
 * @return true if connection state matches, false otherwise, including if connection is NULL
 */
CT_EXTERN bool ct_connection_is_closing(const ct_connection_t* connection);

/**
 * @ingroup connection
 * @brief Check if a connection's state is CT_CONN_STATE_CLOSED or CT_CONN_STATE_CLOSING
 *
 * @see ct_connection_get_state() for a generic getter
 *
 * @param[in] connection The connection to check
 * @return true if connection state matches, false otherwise, including if connection is NULL
 */
CT_EXTERN bool ct_connection_is_closed_or_closing(const ct_connection_t* connection);

/**
 * @ingroup connection
 * @brief Check if a connection is a client connection.
 *
 * @param[in] connection The connection to check
 * @return true if connection role matches, false otherwise, including if connection is NULL
 */
CT_EXTERN bool ct_connection_is_client(const ct_connection_t* connection);

/**
 * @ingroup connection
 * @brief Check if a connection is a server connection.
 * @param[in] connection The connection to check
 * @return true if connection role matches, false otherwise, including if connection is NULL
 */
CT_EXTERN bool ct_connection_is_server(const ct_connection_t* connection);

/**
 * @ingroup connection
 * @brief Check the value of the canSend connection property.
 * @param[in] connection The connection to check
 * @return false if connection is NULL, closed, not established or "Final" message property has been sent.
 */
CT_EXTERN bool ct_connection_can_send(const ct_connection_t* connection);

/**
 * @ingroup connection
 * @brief Check the value of the canReceive connection property.
 *
 * @param[in] connection The connection to check
 * @return false if connection is NULL of if canReceive is false.
 */
CT_EXTERN bool ct_connection_can_receive(const ct_connection_t* connection);

/**
 * @ingroup connection
 * @brief Free resources in a connection.
 * @param[in] connection Connection to free
 */
CT_EXTERN void ct_connection_free(ct_connection_t* connection);

/**
 * @ingroup connection
 * @brief Close a connection gracefully.
 *
 * Exact behaviour depends on the underlying transport protocol.
 * For TCP, this performs a graceful shutdown (e.g., TCP FIN).
 * For UDP this simply stops further sends and receives and closes the socket.
 * For QUIC it closes the connection, if the connection it is invoked on
 * is the last open connection in the connection group. Otherwise it closes
 * the connection one way (sending a FIN), stopping sending but allowing receives to continue
 * until the remote also closes.
 *
 * @param[in] connection Connection to close
 */
CT_EXTERN void ct_connection_close(ct_connection_t* connection);

/**
 * @ingroup connection
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
 * @ingroup connection
 * @brief Clone a connection to create a new connection in the same connection group.
 *
 * Creates a new connection that shares the same transport session as the parent
 * connection. This enables a multi-streaming protocols like QUIC to create
 * multiple logical connections (streams) over a single transport session.
 *
 * The callbacks of the source connection are copied into the cloned connection.
 * The ready callback is invoked with the cloned connection as a parameter, when
 * connection succeeds.
 *
 * @note The cloned connection will share callbacks with the source connection.
 * When the clone is ready, it will invoke the source connection's ready callback
 * with the cloned connection as a parameter.
 *
 * @param[in] source_connection The connection to clone
 * @param[in] framer Optional framer for the cloned connection (NULL to inherit)
 * @param[in] connection_properties Optional properties for cloned connection (NULL to inherit)
 * @return 0 on success, non-zero on error (e.g., source_connection is NULL, or cloning not supported by protocol)
 */
CT_EXTERN int ct_connection_clone_full(const ct_connection_t* source_connection,
                                       ct_framer_impl_t* framer,
                                       const ct_transport_properties_t* connection_properties);

/**
 * @ingroup connection
 * @brief Clone a connection with only mandatory parameters.
 *
 * @see ct_connection_clone_full
 */
CT_EXTERN int ct_connection_clone(ct_connection_t* source_connection);

/**
 * @ingroup connection
 * @brief Get the total number of connections in a connection group (including closed ones).
 *
 * @param[in] connection The connection to query
 * @return The total number of connections in the group
 */
CT_EXTERN size_t ct_connection_get_total_num_grouped_connections(const ct_connection_t* connection);

/**
 * @ingroup connection
 * @brief Get the number of open connections in a connection group.
 *
 * @param[in] connection The connection to query
 * @return The number of open (non-closed) connections in the group
 */
CT_EXTERN size_t ct_connection_get_num_open_grouped_connections(const ct_connection_t* connection);

/**
 * @ingroup connection
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
 * @ingroup connection
 * @brief Forcefully abort all connections in the same connection group.
 *
 * Immediately terminates all connections in the group without graceful shutdown.
 * This is equivalent to calling ct_connection_abort() on each connection in the group.
 *
 * @param[in] connection Any connection in the group to abort
 */
CT_EXTERN void ct_connection_abort_group(ct_connection_t* connection);


/**
 * @ingroup connection
 * @brief Enumeration of currently supported transport protocols.
 *
 * @param[in] connection The connection to query
 * @return The transport protocol enum value, or CT_PROTOCOL_ERROR if protocol cannot be determined
 */
typedef enum {
    CT_PROTOCOL_ERROR = -1, // returned from getters in errors, e.g. null connection
    CT_PROTOCOL_TCP,
    CT_PROTOCOL_UDP,
    CT_PROTOCOL_QUIC,
} ct_protocol_enum_t;

/**
 * @ingroup connection
 * @brief Get the shared connection properties used by a connection.
 *
 * @param[in] connection The connection to query
 * @return A pointer to the connection properties struct, or NULL if connection is NULL
 */
CT_EXTERN const ct_connection_properties_t*
ct_connection_get_connection_properties(const ct_connection_t* connection);


/** 
 * @ingroup connection
 * @brief Get the transport protocol used by a connection.
 *
 * @param[in] connection The connection to query
 * @return The transport protocol enum value, or CT_PROTOCOL_ERROR if protocol cannot be determined
 */
CT_EXTERN ct_protocol_enum_t
ct_connection_get_transport_protocol(const ct_connection_t* connection);

/**
 * @ingroup connection
 * @brief Check if a connection has sent early data (e.g., 0-RTT).
 *
 * @param[in] connection The connection to check
 * @return true if early data was sent, false if not or if connection is NULL
 */
CT_EXTERN bool ct_connection_sent_early_data(const ct_connection_t* connection);

#endif // CTAPS_H

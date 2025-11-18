#ifndef CONNECTION_PROPERTIES_H
#define CONNECTION_PROPERTIES_H

#include <stdbool.h>
#include <stdint.h>
#include <netinet/tcp.h>

#define CONN_TIMEOUT_DISABLED UINT32_MAX
#define CONN_RATE_UNLIMITED UINT64_MAX
#define CONN_CHECKSUM_FULL_COVERAGE UINT32_MAX
#define CONN_MSG_MAX_LEN_NOT_APPLICABLE 0

// TODO - this should really be shared with selection properties, figure out how
#define output_con_enum(enum_name, string_name, property_type, default_value) enum_name,

typedef enum {
  CONN_STATE_ESTABLISHING = 0,
  CONN_STATE_ESTABLISHED,
  CONN_STATE_CLOSING,
  CONN_STATE_CLOSED
} ct_connection_state_enum_t;

typedef enum {
  CONN_SCHEDULER_WEIGHTED_FAIR_QUEUEING = 0,
  // ... other schedulers
} ct_connection_scheduler_enum_t;

// 8.1.6: Capacity Profile (connCapacityProfile)
typedef enum {
  CAPACITY_PROFILE_BEST_EFFORT = 0,
  CAPACITY_PROFILE_SCAVENGER,
  CAPACITY_PROFILE_LOW_LATENCY_INTERACTIVE,
  CAPACITY_PROFILE_LOW_LATENCY_NON_INTERACTIVE,
  CAPACITY_PROFILE_CONSTANT_RATE_STREAMING,
  CAPACITY_PROFILE_CAPACITY_SEEKING
} ct_capacity_profile_enum_t;

// 8.1.7: Policy for Using Multipath Transports (multipathPolicy)
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

typedef struct ct_connection_property_t {
  char* name;
  bool read_only;
  ct_connection_property_value_t value;
} ct_connection_property_t;


// clang-format off
// Done this way to avoid having to keep a full list of names several places for enums, structs, defaults, etc.
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

#define output_struct(enum_name, name, property_type, default_value) property_type name;
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

void ct_connection_properties_build(ct_connection_properties_t* properties);

int ct_cp_set_prop_uint32(ct_connection_properties_t* props, ct_connection_property_enum_t prop_enum, uint32_t val);
int ct_cp_set_prop_uint64(ct_connection_properties_t* props, ct_connection_property_enum_t prop_enum, uint64_t val);
int ct_cp_set_prop_bool(ct_connection_properties_t* props, ct_connection_property_enum_t prop_enum, bool val);
int ct_cp_set_prop_enum(ct_connection_properties_t* props, ct_connection_property_enum_t prop_enum, int val);



#endif  // CONNECTION_PROPERTIES_H
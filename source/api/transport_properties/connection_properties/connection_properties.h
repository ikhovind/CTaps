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
} ConnectionStateEnum;

typedef enum {
  CONN_SCHEDULER_WEIGHTED_FAIR_QUEUEING = 0,
  // ... other schedulers
} ConnectionSchedulerEnum;

// 8.1.6: Capacity Profile (connCapacityProfile)
typedef enum {
  CAPACITY_PROFILE_DEFAULT = 0,
  CAPACITY_PROFILE_SCAVENGER,
  CAPACITY_PROFILE_LOW_LATENCY_INTERACTIVE,
  CAPACITY_PROFILE_LOW_LATENCY_NON_INTERACTIVE,
  CAPACITY_PROFILE_CONSTANT_RATE_STREAMING,
  CAPACITY_PROFILE_CAPACITY_SEEKING
} CapacityProfileEnum;

// 8.1.7: Policy for Using Multipath Transports (multipathPolicy)
typedef enum {
  MULTIPATH_POLICY_HANDOVER = 0,
  MULTIPATH_POLICY_INTERACTIVE,
  MULTIPATH_POLICY_AGGREGATE
} MultipathPolicyEnum;

typedef union {
  uint32_t uint32_val;
  uint64_t uint64_val;
  bool bool_val;
  int enum_val;
} ConnectionPropertyValue;

typedef struct ConnectionProperty {
  char* name;
  bool read_only;
  ConnectionPropertyValue value;
} ConnectionProperty;


// clang-format off
// Done this way to avoid having to keep a full list of names several places for enums, structs, defaults, etc.
#define get_writable_connection_property_list(f)                                                                    \
f(RECV_CHECKSUM_LEN,          "recvChecksumLen",          uint32_t,                CONN_CHECKSUM_FULL_COVERAGE)        \
f(CONN_PRIORITY,              "connPriority",             uint32_t,                100)                                \
f(CONN_TIMEOUT,               "connTimeout",              uint32_t,                CONN_TIMEOUT_DISABLED)              \
f(KEEP_ALIVE_TIMEOUT,         "keepAliveTimeout",         uint32_t,                CONN_TIMEOUT_DISABLED)              \
f(CONN_SCHEDULER,             "connScheduler",            ConnectionSchedulerEnum, CONN_SCHEDULER_WEIGHTED_FAIR_QUEUEING) \
f(CONN_CAPACITY_PROFILE,      "connCapacityProfile",      CapacityProfileEnum,     CAPACITY_PROFILE_DEFAULT)           \
f(MULTIPATH_POLICY,           "multipathPolicy",          MultipathPolicyEnum,     MULTIPATH_POLICY_HANDOVER)          \
f(MIN_SEND_RATE,              "minSendRate",              uint64_t,                CONN_RATE_UNLIMITED)                \
f(MIN_RECV_RATE,              "minRecvRate",              uint64_t,                CONN_RATE_UNLIMITED)                \
f(MAX_SEND_RATE,              "maxSendRate",              uint64_t,                CONN_RATE_UNLIMITED)                \
f(MAX_RECV_RATE,              "maxRecvRate",              uint64_t,                CONN_RATE_UNLIMITED)                \
f(GROUP_CONN_LIMIT,           "groupConnLimit",           uint64_t,                CONN_RATE_UNLIMITED)                \
f(ISOLATE_SESSION,            "isolateSession",           bool,                    false)

#define get_read_only_connection_properties(f)                                                                \
f(STATE,                               "state",                               ConnectionStateEnum, 0)        \
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
} ConnectionPropertyEnum;

typedef struct {
  ConnectionProperty list[CONNECTION_PROPERTY_END];
} ConnectionProperties;

#define create_con_property_initializer(enum_name, string_name, property_type, default_value) \
  [enum_name] = {                                                          \
    .name = string_name,                                                   \
    .value = { (uint32_t)default_value }                     \
},

static ConnectionProperty DEFAULT_CONNECTION_PROPERTIES[] = {
    get_writable_connection_property_list(create_con_property_initializer)
    get_read_only_connection_properties(create_con_property_initializer)
    get_tcp_connection_properties(create_con_property_initializer)
};

void connection_properties_build(ConnectionProperties* properties);

int cp_set_prop_uint32(ConnectionProperties* props, ConnectionPropertyEnum prop_enum, uint32_t val);
int cp_set_prop_uint64(ConnectionProperties* props, ConnectionPropertyEnum prop_enum, uint64_t val);
int cp_set_prop_bool(ConnectionProperties* props, ConnectionPropertyEnum prop_enum, bool val);
int cp_set_prop_enum(ConnectionProperties* props, ConnectionPropertyEnum prop_enum, int val);



#endif  // CONNECTION_PROPERTIES_H
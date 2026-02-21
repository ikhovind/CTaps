#include "ctaps.h"
#include "ctaps_internal.h"

#include <logging/log.h>
#include <stdint.h>
#include <string.h>

const ct_connection_property_t DEFAULT_CONNECTION_PROPERTIES[] = {
    get_writable_connection_property_list(create_con_property_initializer)
    get_read_only_connection_properties(create_con_property_initializer)
    get_tcp_connection_properties(create_con_property_initializer)
};


void ct_connection_properties_build(ct_connection_properties_t* properties) {
  memcpy(properties, &DEFAULT_CONNECTION_PROPERTIES, sizeof(ct_connection_properties_t));
}

int ct_cp_set_prop_uint32(ct_connection_properties_t* props, const ct_connection_property_enum_t prop_enum, const uint32_t val) {
  if (props->list[prop_enum].read_only) {
    log_warn("Attempt to set read-only property: %s", props->list[prop_enum].name);
    return -EINVAL;
  }
  props->list[prop_enum].value.uint32_val = val;
  return 0;
}
int ct_cp_set_prop_uint64(ct_connection_properties_t* props, const ct_connection_property_enum_t prop_enum, const uint64_t val) {
  if (props->list[prop_enum].read_only) {
    log_warn("Attempt to set read-only property: %s", props->list[prop_enum].name);
    return -EINVAL;
  }
  props->list[prop_enum].value.uint64_val = val;
  return 0;
}
int ct_cp_set_prop_bool(ct_connection_properties_t* props, const ct_connection_property_enum_t prop_enum, const bool val) {
  if (props->list[prop_enum].read_only) {
    log_warn("Attempt to set read-only property: %s", props->list[prop_enum].name);
    return -EINVAL;
  }
  props->list[prop_enum].value.bool_val = val;
  return 0;
}
int ct_cp_set_prop_enum(ct_connection_properties_t* props, const ct_connection_property_enum_t prop_enum, const int val) {
  if (props->list[prop_enum].read_only) {
    log_warn("Attempt to set read-only property: %s", props->list[prop_enum].name);
    return -EINVAL;
  }
  props->list[prop_enum].value.enum_val = val;
  return 0;
}

uint64_t ct_connection_properties_get_recv_checksum_len(ct_connection_properties_t* conn_props) {
  if (!conn_props) {
    log_warn("Null pointer passed to get_recv_checksum_len");
    return 0;
  }
  return conn_props->list[RECV_CHECKSUM_LEN].value.uint64_val;
}

uint32_t ct_connection_properties_get_conn_priority(ct_connection_properties_t* conn_props) {
  if (!conn_props) {
    log_warn("Null pointer passed to get_conn_priority");
    return 0;
  }
  return conn_props->list[CONN_PRIORITY].value.uint32_val;
}

uint32_t ct_connection_properties_get_conn_timeout(ct_connection_properties_t* conn_props) {
  if (!conn_props) {
    log_warn("Null pointer passed to get_conn_timeout");
    return 0;
  }
  return conn_props->list[CONN_TIMEOUT].value.uint32_val;
}

uint32_t ct_connection_properties_get_keep_alive_timeout(ct_connection_properties_t* conn_props) {
  if (!conn_props) {
    log_warn("Null pointer passed to get_keep_alive_timeout");
    return 0;
  }
  return conn_props->list[KEEP_ALIVE_TIMEOUT].value.uint32_val;
}

ct_connection_scheduler_enum_t ct_connection_properties_get_conn_scheduler(ct_connection_properties_t* conn_props) {
  if (!conn_props) {
    log_warn("Null pointer passed to get_conn_scheduler");
    return CONN_SCHEDULER_WEIGHTED_FAIR_QUEUEING;
  }
  return (ct_connection_scheduler_enum_t)conn_props->list[CONN_SCHEDULER].value.enum_val;
}

ct_capacity_profile_enum_t ct_connection_properties_get_conn_capacity_profile(ct_connection_properties_t* conn_props) {
  if (!conn_props) {
    log_warn("Null pointer passed to get_conn_capacity_profile");
    return CAPACITY_PROFILE_BEST_EFFORT;
  }
  return (ct_capacity_profile_enum_t)conn_props->list[CONN_CAPACITY_PROFILE].value.enum_val;
}

ct_multipath_policy_enum_t ct_connection_properties_get_multipath_policy(ct_connection_properties_t* conn_props) {
  if (!conn_props) {
    log_warn("Null pointer passed to get_multipath_policy");
    return MULTIPATH_POLICY_HANDOVER;
  }
  return (ct_multipath_policy_enum_t)conn_props->list[MULTIPATH_POLICY].value.enum_val;
}

uint64_t ct_connection_properties_get_min_send_rate(ct_connection_properties_t* conn_props) {
  if (!conn_props) {
    log_warn("Null pointer passed to get_min_send_rate");
    return 0;
  }
  return conn_props->list[MIN_SEND_RATE].value.uint64_val;
}

uint64_t ct_connection_properties_get_min_recv_rate(ct_connection_properties_t* conn_props) {
  if (!conn_props) {
    log_warn("Null pointer passed to get_min_recv_rate");
    return 0;
  }
  return conn_props->list[MIN_RECV_RATE].value.uint64_val;
}

uint64_t ct_connection_properties_get_max_send_rate(ct_connection_properties_t* conn_props) {
  if (!conn_props) {
    log_warn("Null pointer passed to get_max_send_rate");
    return 0;
  }
  return conn_props->list[MAX_SEND_RATE].value.uint64_val;
}

uint64_t ct_connection_properties_get_max_recv_rate(ct_connection_properties_t* conn_props) {
  if (!conn_props) {
    log_warn("Null pointer passed to get_max_recv_rate");
    return 0;
  }
  return conn_props->list[MAX_RECV_RATE].value.uint64_val;
}

uint64_t ct_connection_properties_get_group_conn_limit(ct_connection_properties_t* conn_props) {
  if (!conn_props) {
    log_warn("Null pointer passed to get_group_conn_limit");
    return 0;
  }
  return conn_props->list[GROUP_CONN_LIMIT].value.uint64_val;
}

bool ct_connection_properties_get_isolate_session(ct_connection_properties_t* conn_props) {
  if (!conn_props) {
    log_warn("Null pointer passed to get_isolate_session");
    return false;
  }
  return conn_props->list[ISOLATE_SESSION].value.bool_val;
}

ct_connection_state_enum_t ct_connection_properties_get_state(ct_connection_properties_t* conn_props) {
  if (!conn_props) {
    log_warn("Null pointer passed to get_state");
    return CONN_STATE_CLOSED;
  }
  return (ct_connection_state_enum_t)conn_props->list[STATE].value.enum_val;
}

bool ct_connection_properties_get_can_send(ct_connection_properties_t* conn_props) {
  if (!conn_props) {
    log_warn("Null pointer passed to get_can_send");
    return false;
  }
  return conn_props->list[CAN_SEND].value.bool_val;
}

bool ct_connection_properties_get_can_receive(ct_connection_properties_t* conn_props) {
  if (!conn_props) {
    log_warn("Null pointer passed to get_can_receive");
    return false;
  }
  return conn_props->list[CAN_RECEIVE].value.bool_val;
}

uint64_t ct_connection_properties_get_singular_transmission_msg_max_len(ct_connection_properties_t* conn_props) {
  if (!conn_props) {
    log_warn("Null pointer passed to get_singular_transmission_msg_max_len");
    return 0;
  }
  return conn_props->list[SINGULAR_TRANSMISSION_MSG_MAX_LEN].value.uint64_val;
}

uint64_t ct_connection_properties_get_send_message_max_len(ct_connection_properties_t* conn_props) {
  if (!conn_props) {
    log_warn("Null pointer passed to get_send_message_max_len");
    return 0;
  }
  return conn_props->list[SEND_MESSAGE_MAX_LEN].value.uint64_val;
}

uint64_t ct_connection_properties_get_recv_message_max_len(ct_connection_properties_t* conn_props) {
  if (!conn_props) {
    log_warn("Null pointer passed to get_recv_message_max_len");
    return 0;
  }
  return conn_props->list[RECV_MESSAGE_MAX_LEN].value.uint64_val;
}

uint32_t ct_connection_properties_get_user_timeout_value_ms(ct_connection_properties_t* conn_props) {
  if (!conn_props) {
    log_warn("Null pointer passed to get_user_timeout_value_ms");
    return 0;
  }
  return conn_props->list[USER_TIMEOUT_VALUE_MS].value.uint32_val;
}

bool ct_connection_properties_get_user_timeout_enabled(ct_connection_properties_t* conn_props) {
  if (!conn_props) {
    log_warn("Null pointer passed to get_user_timeout_enabled");
    return false;
  }
  return conn_props->list[USER_TIMEOUT_ENABLED].value.bool_val;
}

bool ct_connection_properties_get_user_timeout_changeable(ct_connection_properties_t* conn_props) {
  if (!conn_props) {
    log_warn("Null pointer passed to get_user_timeout_changeable");
    return false;
  }
  return conn_props->list[USER_TIMEOUT_CHANGEABLE].value.bool_val;
}

// Writable connection property setters

void ct_connection_properties_set_recv_checksum_len(ct_connection_properties_t* conn_props, uint32_t recv_checksum_len) {
  if (!conn_props) {
    log_warn("Null pointer passed to set_recv_checksum_len");
    return;
  }
  conn_props->list[RECV_CHECKSUM_LEN].value.uint32_val = recv_checksum_len;
}

void ct_connection_properties_set_conn_priority(ct_connection_properties_t* conn_props, uint32_t conn_priority) {
  if (!conn_props) {
    log_warn("Null pointer passed to set_conn_priority");
    return;
  }
  conn_props->list[CONN_PRIORITY].value.uint32_val = conn_priority;
}

void ct_connection_properties_set_conn_timeout(ct_connection_properties_t* conn_props, uint32_t conn_timeout) {
  if (!conn_props) {
    log_warn("Null pointer passed to set_conn_timeout");
    return;
  }
  conn_props->list[CONN_TIMEOUT].value.uint32_val = conn_timeout;
}

void ct_connection_properties_set_keep_alive_timeout(ct_connection_properties_t* conn_props, uint32_t keep_alive_timeout) {
  if (!conn_props) {
    log_warn("Null pointer passed to set_keep_alive_timeout");
    return;
  }
  conn_props->list[KEEP_ALIVE_TIMEOUT].value.uint32_val = keep_alive_timeout;
}

void ct_connection_properties_set_conn_scheduler(ct_connection_properties_t* conn_props, ct_connection_scheduler_enum_t conn_scheduler) {
  if (!conn_props) {
    log_warn("Null pointer passed to set_conn_scheduler");
    return;
  }
  conn_props->list[CONN_SCHEDULER].value.enum_val = (int)conn_scheduler;
}

void ct_connection_properties_set_conn_capacity_profile(ct_connection_properties_t* conn_props, ct_capacity_profile_enum_t conn_capacity_profile) {
  if (!conn_props) {
    log_warn("Null pointer passed to set_conn_capacity_profile");
    return;
  }
  conn_props->list[CONN_CAPACITY_PROFILE].value.enum_val = (int)conn_capacity_profile;
}

void ct_connection_properties_set_multipath_policy(ct_connection_properties_t* conn_props, ct_multipath_policy_enum_t multipath_policy) {
  if (!conn_props) {
    log_warn("Null pointer passed to set_multipath_policy");
    return;
  }
  conn_props->list[MULTIPATH_POLICY].value.enum_val = (int)multipath_policy;
}

void ct_connection_properties_set_min_send_rate(ct_connection_properties_t* conn_props, uint64_t min_send_rate) {
  if (!conn_props) {
    log_warn("Null pointer passed to set_min_send_rate");
    return;
  }
  conn_props->list[MIN_SEND_RATE].value.uint64_val = min_send_rate;
}

void ct_connection_properties_set_min_recv_rate(ct_connection_properties_t* conn_props, uint64_t min_recv_rate) {
  if (!conn_props) {
    log_warn("Null pointer passed to set_min_recv_rate");
    return;
  }
  conn_props->list[MIN_RECV_RATE].value.uint64_val = min_recv_rate;
}

void ct_connection_properties_set_max_send_rate(ct_connection_properties_t* conn_props, uint64_t max_send_rate) {
  if (!conn_props) {
    log_warn("Null pointer passed to set_max_send_rate");
    return;
  }
  conn_props->list[MAX_SEND_RATE].value.uint64_val = max_send_rate;
}

void ct_connection_properties_set_max_recv_rate(ct_connection_properties_t* conn_props, uint64_t max_recv_rate) {
  if (!conn_props) {
    log_warn("Null pointer passed to set_max_recv_rate");
    return;
  }
  conn_props->list[MAX_RECV_RATE].value.uint64_val = max_recv_rate;
}

void ct_connection_properties_set_group_conn_limit(ct_connection_properties_t* conn_props, uint64_t group_conn_limit) {
  if (!conn_props) {
    log_warn("Null pointer passed to set_group_conn_limit");
    return;
  }
  conn_props->list[GROUP_CONN_LIMIT].value.uint64_val = group_conn_limit;
}

void ct_connection_properties_set_isolate_session(ct_connection_properties_t* conn_props, bool isolate_session) {
  if (!conn_props) {
    log_warn("Null pointer passed to set_isolate_session");
    return;
  }
  conn_props->list[ISOLATE_SESSION].value.bool_val = isolate_session;
}

void ct_connection_properties_set_user_timeout_value_ms(ct_connection_properties_t* conn_props, uint32_t user_timeout_value_ms) {
  if (!conn_props) {
    log_warn("Null pointer passed to set_user_timeout_value_ms");
    return;
  }
  conn_props->list[USER_TIMEOUT_VALUE_MS].value.uint32_val = user_timeout_value_ms;
}

void ct_connection_properties_set_user_timeout_enabled(ct_connection_properties_t* conn_props, bool user_timeout_enabled) {
  if (!conn_props) {
    log_warn("Null pointer passed to set_user_timeout_enabled");
    return;
  }
  conn_props->list[USER_TIMEOUT_ENABLED].value.bool_val = user_timeout_enabled;
}

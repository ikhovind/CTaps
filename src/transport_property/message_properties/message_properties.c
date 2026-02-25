#include "ctaps.h"
#include "ctaps_internal.h"

#include <logging/log.h>
#include <stdlib.h>
#include <string.h>

ct_message_properties_t* ct_message_properties_new(void) {
  ct_message_properties_t* props = malloc(sizeof(ct_message_properties_t));
  if (!props) {
    return NULL;
  }

  // Initialize with default values
  memcpy(props, &DEFAULT_MESSAGE_PROPERTIES, sizeof(ct_message_properties_t));
  return props;
}

ct_message_properties_t* ct_message_properties_deep_copy(const ct_message_properties_t* source) {
  if (!source) {
    return NULL;
  }

  ct_message_properties_t* copy = malloc(sizeof(ct_message_properties_t));
  if (!copy) {
    return NULL;
  }

  // Deep copy all properties
  memcpy(copy, source, sizeof(ct_message_properties_t));

  return copy;
}

void ct_message_properties_free(ct_message_properties_t* message_properties) {
  if (!message_properties) {
    return;
  }
  free(message_properties);
}

ct_capacity_profile_enum_t ct_message_properties_get_capacity_profile(const ct_message_properties_t* message_properties) {
  if (!message_properties) {
    return CAPACITY_PROFILE_BEST_EFFORT;
  }
  return message_properties->message_property[MSG_CAPACITY_PROFILE].value.capacity_profile_enum_value;
}

bool ct_message_properties_get_final(const ct_message_properties_t* message_properties) {
  if (!message_properties) {
    return false;
  }
  return message_properties->message_property[FINAL].value.boolean_value;
}

bool ct_message_properties_get_safely_replayable(const ct_message_properties_t* message_properties) {
  if (!message_properties) {
    log_error("Null pointer passed to ct_message_properties_get_safely_replayable");
    return false;
  }
  return message_properties->message_property[MSG_SAFELY_REPLAYABLE].value.boolean_value;
}

uint32_t ct_message_properties_get_priority(const ct_message_properties_t* msg_props) {
  if (!msg_props) {
    log_warn("Tried to set FINAL property on NULL message properties");
    return 0;
  }
  return msg_props->message_property[MSG_PRIORITY].value.uint32_value;
}

void ct_message_properties_set_safely_replayable(ct_message_properties_t* message_properties, bool value) {
  if (!message_properties) {
    log_error("Null pointer passed to ct_message_properties_set_safely_replayable");
    return;
  }
  message_properties->message_property[MSG_SAFELY_REPLAYABLE].value.boolean_value = value;
}

void ct_message_properties_set_final(ct_message_properties_t* message_properties, bool value) {
  if (!message_properties) {
    log_warn("Tried to set FINAL property on NULL message properties");
    return;
  }
  message_properties->message_property[FINAL].value.boolean_value = value;
}

bool ct_message_properties_get_reliable(const ct_message_properties_t* message_properties) {
  if (!message_properties) {
    log_warn("Tried to get RELIABLE property on NULL message properties");
    return false;
  }
  return message_properties->message_property[MSG_RELIABLE].value.boolean_value;
}

void ct_message_properties_set_checksum_len(ct_message_properties_t* msg_props, uint32_t checksum_len) {
  if (!msg_props) {
    log_warn("Tried to set MSG_CHECKSUM_LEN property on NULL message properties");
    return;
  }
  msg_props->message_property[MSG_CHECKSUM_LEN].value.uint32_value = checksum_len;
}

void ct_message_properties_set_lifetime(ct_message_properties_t* msg_props, uint64_t lifetime) {
  if (!msg_props) {
    log_warn("Tried to set MSG_LIFETIME property on NULL message properties");
    return;
  }
  msg_props->message_property[MSG_LIFETIME].value.uint64_value = lifetime;
}

void ct_message_properties_set_priority(ct_message_properties_t* msg_props, uint32_t priority) {
  if (!msg_props) {
    log_warn("Tried to set MSG_PRIORITY property on NULL message properties");
    return;
  }
  msg_props->message_property[MSG_PRIORITY].value.uint32_value = priority;
}

void ct_message_properties_set_ordered(ct_message_properties_t* msg_props, bool ordered) {
  if (!msg_props) {
    log_warn("Tried to set MSG_ORDERED property on NULL message properties");
    return;
  }
  msg_props->message_property[MSG_ORDERED].value.boolean_value = ordered;
}

void ct_message_properties_set_reliable(ct_message_properties_t* msg_props, bool reliable) {
  if (!msg_props) {
    log_warn("Tried to set MSG_RELIABLE property on NULL message properties");
    return;
  }
  msg_props->message_property[MSG_RELIABLE].value.boolean_value = reliable;
}

void ct_message_properties_set_no_fragmentation(ct_message_properties_t* msg_props, bool no_fragmentation) {
  if (!msg_props) {
    log_warn("Tried to set NO_FRAGMENTATION property on NULL message properties");
    return;
  }
  msg_props->message_property[NO_FRAGMENTATION].value.boolean_value = no_fragmentation;
}

void ct_message_properties_set_no_segmentation(ct_message_properties_t* msg_props, bool no_segmentation) {
  if (!msg_props) {
    log_warn("Tried to set NO_SEGMENTATION property on NULL message properties");
    return;
  }
  msg_props->message_property[NO_SEGMENTATION].value.boolean_value = no_segmentation;
}

void ct_message_properties_set_capacity_profile(ct_message_properties_t* msg_props, ct_capacity_profile_enum_t capacity_profile) {
  if (!msg_props) {
    log_warn("Tried to set MSG_CAPACITY_PROFILE property on NULL message properties");
    return;
  }
  msg_props->message_property[MSG_CAPACITY_PROFILE].value.capacity_profile_enum_value = capacity_profile;
}

bool ct_message_properties_get_ordered(const ct_message_properties_t* message_properties) {
  if (!message_properties) {
    log_warn("Tried to get MSG_ORDERED property on NULL message properties");
    return false;
  }
  return message_properties->message_property[MSG_ORDERED].value.boolean_value;
}

uint64_t ct_message_properties_get_lifetime(const ct_message_properties_t* message_properties) {
  if (!message_properties) {
    log_warn("Tried to get MSG_LIFETIME property on NULL message properties");
    return 0;
  }
  return message_properties->message_property[MSG_LIFETIME].value.uint64_value;
}

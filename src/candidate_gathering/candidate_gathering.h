#ifndef CANDIDATE_GATHERING_H
#define CANDIDATE_GATHERING_H

#include <glib.h>

#include "ctaps.h"
#include "ctaps_internal.h"

// Enum to specify what type of node we are.
typedef enum {
  NODE_TYPE_ROOT = 0,
  NODE_TYPE_PATH,
  NODE_TYPE_PROTOCOL,
  NODE_TYPE_ENDPOINT,
} ct_node_type_t;

typedef struct ct_protocol_impl_array_s {
  const ct_protocol_impl_t** protocols;
  size_t count;
} ct_protocol_impl_array_t;

typedef struct ct_protocol_options_s {
  ct_protocol_impl_array_t* protocol_impls;
  ct_string_array_value_t* alpns; // e.g., ALPN strings
} ct_protocol_options_t;

typedef struct ct_protocol_candidate_s {
  const ct_protocol_impl_t* protocol_impl;
  char* alpn;
} ct_protocol_candidate_t;

typedef struct ct_candidate_node_t {
  ct_node_type_t type;
  int score;

  ct_local_endpoint_t* local_endpoint;
  ct_remote_endpoint_t* remote_endpoint;

  ct_protocol_candidate_t* protocol_candidate;

  const ct_transport_properties_t* transport_properties;
} ct_candidate_node_t;

GArray* get_ordered_candidate_nodes(const ct_preconnection_t* precon);

ct_protocol_options_t* ct_protocol_options_new(const ct_preconnection_t* precon);

ct_protocol_candidate_t* ct_protocol_candidate_new(const ct_protocol_impl_t* protocol_impl, const char* alpn);

void ct_protocol_options_free(ct_protocol_options_t* protocol_options);

void free_candidate_array(GArray* candidate_array);

ct_protocol_impl_array_t* ct_protocol_impl_array_new(void);

#endif //CANDIDATE_GATHERING_H

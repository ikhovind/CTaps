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

/**
  * @brief A single combination of options from the ct_protocol_options_t struct
  *
  * A single PROTOCOL node contains a single one of these.
  */
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

typedef struct ct_candidate_gathering_callbacks_s {
    void (*candidate_node_array_ready_cb)(GArray* candidate_array, void* context);
    void* context;
} ct_candidate_gathering_callbacks_t;

typedef struct ct_gather_context_s {
    GNode* root_node;
    const ct_preconnection_t* preconnection;
    size_t pending_resolutions;
    size_t num_in_flight;
    ct_candidate_gathering_callbacks_t gathering_callbacks;
    bool failed;
} ct_gather_context_t;

typedef struct ct_remote_resolve_call_context_s {
    GNode* parent_node;
    ct_gather_context_t* gather_context;
    int32_t assigned_port;
} ct_remote_resolve_call_context_t;

void ct_remote_endpoint_resolve_cb(ct_remote_endpoint_t* remote_endpoint, size_t out_count,
                                   ct_remote_resolve_call_context_t* context);

/**
  * @brief Main entry point for candidate gathering. Builds the candidate tree and returns an ordered array of candidate nodes through the callback.
  *
  * @param precon The preconnection containing all necessary information for candidate gathering.
  * @param callback The callback to be called when the candidate array is ready.
  * @return Negative value on asynchronous error, 0 on success. The caller is responsible for freeing the candidate array and its contents.
  */
int get_ordered_candidate_nodes(const ct_preconnection_t* precon,
                                ct_candidate_gathering_callbacks_t callbacks);

void free_candidate_array(GArray* candidate_array);

void ct_protocol_candidate_free(ct_protocol_candidate_t* protocol_candidate);

#endif //CANDIDATE_GATHERING_H

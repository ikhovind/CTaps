
#include "connection/socket_manager/socket_manager.h"
#include "connection/listener.h"
#include "ctaps.h"
#include "ctaps_internal.h"
#include "message/message.h"
#include "message/framer.h"
#include "message/message_context.h"
#include "preconnection.h"
#include "transport_property/selection_properties/selection_properties.h"
#include <candidate_gathering/candidate_gathering.h>
#include <candidate_gathering/candidate_racing.h>
#include <endpoint/local_endpoint.h>
#include <endpoint/remote_endpoint.h>
#include <errno.h>
#include <glib.h>
#include <logging/log.h>
#include <security_parameter/security_parameters.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define INT_TO_STRING(x) #x

#define MAX_PORT_STR_LENGTH sizeof(INT_TO_STRING(UINT16_MAX))

int copy_remote_endpoints(ct_preconnection_t* preconnection,
                          const ct_remote_endpoint_t* remote_endpoints,
                          const size_t num_remote_endpoints) {
    preconnection->num_remote_endpoints = num_remote_endpoints;
    preconnection->remote_endpoints = malloc(num_remote_endpoints * sizeof(ct_remote_endpoint_t));
    if (!preconnection->remote_endpoints) {
        log_error("Could not allocate memory for remote endpoints: %s");
        return errno;
    }
    memset(preconnection->remote_endpoints, 0, num_remote_endpoints * sizeof(ct_remote_endpoint_t));
    // Deep copy each remote endpoint (copies all strings)
    for (size_t i = 0; i < num_remote_endpoints; i++) {
        int rc = ct_remote_endpoint_copy_content(&remote_endpoints[i],
                                                 &preconnection->remote_endpoints[i]);
        if (rc != 0) {
            log_error("Failed to copy remote endpoint content for index %zu: %s", i, strerror(-rc));
            // Free any previously copied remote endpoints
            for (size_t j = 0; j < i; j++) {
                ct_remote_endpoint_free_content(&preconnection->remote_endpoints[j]);
            }
            free(preconnection->remote_endpoints);
            preconnection->remote_endpoints = NULL;
            preconnection->num_remote_endpoints = 0;
            return rc;
        }
    }
    return 0;
}

ct_preconnection_t* ct_preconnection_new(const ct_local_endpoint_t* local_endpoints,
                                         size_t num_local_endpoints,
                                         const ct_remote_endpoint_t* remote_endpoints,
                                         const size_t num_remote_endpoints,
                                         const ct_transport_properties_t* transport_properties,
                                         const ct_security_parameters_t* security_parameters) {

    ct_preconnection_t* precon = malloc(sizeof(ct_preconnection_t));
    if (!precon) {
        log_error("Failed to allocate memory for preconnection");
        return NULL;
    }

    memset(precon, 0, sizeof(ct_preconnection_t));

    if (transport_properties) {
        log_debug("Copying transport properties into preconnection");
        ct_selection_properties_deep_copy(&precon->transport_properties.selection_properties,
                                          &transport_properties->selection_properties);
        precon->transport_properties.connection_properties =
            transport_properties->connection_properties;
    } else {
        log_debug("No transport properties provided, initializing with defaults");
        // Initialize with default values
        memcpy(&precon->transport_properties.selection_properties, &DEFAULT_SELECTION_PROPERTIES,
               sizeof(ct_selection_properties_t));
        memcpy(&precon->transport_properties.connection_properties, &DEFAULT_CONNECTION_PROPERTIES,
               sizeof(ct_connection_properties_t));
    }

    // Deep copy security parameters so preconnection owns its own copy
    precon->security_parameters = ct_security_parameters_deep_copy(security_parameters);

    if (num_local_endpoints > 0) {
        if (!local_endpoints) {
            log_error("num_local_endpoints is greater than 0 but local_endpoints is NULL");
            free(precon);
            return NULL;
        }
        precon->num_local_endpoints = num_local_endpoints;
        precon->local_endpoints =
            ct_local_endpoints_deep_copy(local_endpoints, num_local_endpoints);
    }

    // Copy remote endpoints if provided
    if (remote_endpoints && num_remote_endpoints > 0) {
        int ret = copy_remote_endpoints(precon, remote_endpoints, num_remote_endpoints);
        if (ret != 0) {
            free(precon);
            return NULL;
        }
    }

    return precon;
}

typedef struct listener_candidate_node_array_ready_context_s {
    const ct_preconnection_t* preconnection;
    ct_listener_callbacks_t listener_callbacks;
    ct_connection_callbacks_t connection_callbacks;
} listener_candidate_node_array_ready_context_t;

void ct_listener_candidate_node_array_ready_cb(GArray* candidate_nodes, void* context) {
    log_info("Candidate gathering complete for listener, processing candidate nodes");
    listener_candidate_node_array_ready_context_t* listener_candidate_node_array_ready_context =
        (listener_candidate_node_array_ready_context_t*)context;
    const ct_preconnection_t* preconnection =
        listener_candidate_node_array_ready_context->preconnection;
    ct_listener_callbacks_t listener_callbacks =
        listener_candidate_node_array_ready_context->listener_callbacks;
    if (!candidate_nodes || candidate_nodes->len == 0) {
        log_error("No candidate nodes were gathered for listener");
        free(listener_candidate_node_array_ready_context);
        if (listener_callbacks.establishment_error) {
            listener_callbacks.establishment_error(NULL, -ENOENT);
        }
        return;
    }
    const ct_candidate_node_t first_node = g_array_index(candidate_nodes, ct_candidate_node_t, 0);

    ct_listener_t* listener = ct_listener_new(
        ct_preconnection_get_transport_properties(preconnection), first_node.local_endpoint,
        &listener_candidate_node_array_ready_context->listener_callbacks,
        &listener_candidate_node_array_ready_context->connection_callbacks,
        ct_preconnection_get_security_parameters(preconnection),
        first_node.protocol_candidate->protocol_impl);

    if (!listener) {
        log_error("Failed to create listener listener");
        free_candidate_array(candidate_nodes);
        free(listener_candidate_node_array_ready_context);
        if (listener_callbacks.establishment_error) {
            listener_callbacks.establishment_error(NULL, -1);
        }
        return;
    }

    log_info("Starting to listen on port: %d with protocol: %s", first_node.local_endpoint->port,
             listener->socket_manager->protocol_impl->name);
    free_candidate_array(candidate_nodes);
    free(listener_candidate_node_array_ready_context);

    ct_socket_manager_listen(listener);
}

int ct_preconnection_listen(const ct_preconnection_t* preconnection,
                            const ct_listener_callbacks_t* listener_callbacks, // TODO Make pointer
                            const ct_connection_callbacks_t* connection_callbacks) {
    if (!preconnection || !listener_callbacks) {
        log_error("Preconnection or listener callbacks is NULL in ct_preconnection_listen");
        return -EINVAL;
    }
    if (preconnection->num_local_endpoints == 0) {
        log_error("Preconnection must have at least one local endpoint to listen");
        return -EINVAL;
    }
    log_info("Starting Listener from preconnection");
    listener_candidate_node_array_ready_context_t* cb_context =
        calloc(1, sizeof(listener_candidate_node_array_ready_context_t));
    if (!cb_context) {
        log_error("Failed to allocate memory for listener candidate node array ready context");
        return -ENOMEM;
    }

    cb_context->preconnection = preconnection;
    cb_context->listener_callbacks = *listener_callbacks;
    if (connection_callbacks) {
        cb_context->connection_callbacks = *connection_callbacks;
    }

    ct_candidate_gathering_callbacks_t callbacks = {
        .candidate_node_array_ready_cb = ct_listener_candidate_node_array_ready_cb,
        .context = cb_context,
    };

    // TODO - we currently just discard the remote endpoints, but should in
    // the future gather candidates for them as well and pass them to the listener
    // for remote filtering
    int rc = ct_get_ordered_local_candidate_nodes(preconnection, callbacks);
    if (rc < 0) {
        log_error("Failed to get ordered local candidate nodes for listener");
        free(cb_context);
        return rc;
    }
    return 0;
}

int ct_preconnection_initiate(const ct_preconnection_t* preconnection,
                              const ct_connection_callbacks_t* connection_callbacks) {
    log_info("Initiating connection from preconnection with candidate racing");
    if (!preconnection || !connection_callbacks) {
        log_error("Preconnection or callbacks is NULL in ct_preconnection_initiate");
        return -EINVAL;
    }
    if (preconnection->num_remote_endpoints == 0) {
        log_error("Preconnection must have at least one remote endpoint to initiate connection");
        return -EINVAL;
    }

    // The winning connection will be passed to the ready()
    return preconnection_race(preconnection, *connection_callbacks);
}

int ct_preconnection_initiate_with_send(const ct_preconnection_t* preconnection,
                                        const ct_connection_callbacks_t* connection_callbacks,
                                        const ct_message_t* message,
                                        const ct_message_context_t* message_context) {
    log_info("Initiating connection from preconnection with send");
    if (!preconnection || !connection_callbacks) {
        log_error("Preconnection or callbacks is NULL in ct_preconnection_initiate_with_send");
        return -EINVAL;
    }
    if (preconnection->num_remote_endpoints == 0) {
        log_error("Preconnection must have at least one remote endpoint to initiate connection");
        return -EINVAL;
    }
    ct_message_t* msg_copy = NULL;
    if (message) {
        msg_copy = ct_message_deep_copy(message);
        if (!msg_copy) {
            log_error("Failed to deep copy message for preconnection initiate with send");
            return -ENOMEM;
        }
    }
    ct_message_context_t* message_context_copy = NULL;
    if (message_context) {
        message_context_copy = ct_message_context_deep_copy(message_context);
        if (!message_context_copy) {
            log_error("Failed to deep copy message context for preconnection initiate with send");
            ct_message_free(msg_copy);
            return -ENOMEM;
        }
    }

    if (message_context && ct_message_context_get_safely_replayable(message_context)) {
        log_info("Initiating connection from preconnection with candidate racing and early data");
        return preconnection_race_with_early_data(preconnection, *connection_callbacks, msg_copy,
                                                  message_context_copy);
    }
    log_info("Initiating connection from preconnection with candidate racing and send after ready");
    return preconnection_race_with_send_after_ready(preconnection, *connection_callbacks, msg_copy,
                                                    message_context_copy);
}

void ct_preconnection_free(ct_preconnection_t* preconnection) {
    log_trace("Freeing preconnection");
    if (!preconnection) {
        return;
    }

    // Free remote endpoint strings and array
    if (preconnection->remote_endpoints != NULL) {
        for (size_t i = 0; i < preconnection->num_remote_endpoints; i++) {
            ct_remote_endpoint_free_content(&preconnection->remote_endpoints[i]);
        }
        free(preconnection->remote_endpoints);
        preconnection->remote_endpoints = NULL;
    }

    if (preconnection->local_endpoints) {
        ct_local_endpoints_free(preconnection->local_endpoints, preconnection->num_local_endpoints);
    }

    ct_framer_impl_free(preconnection->framer_impl);

    ct_selection_properties_cleanup(&preconnection->transport_properties.selection_properties);

    if (preconnection->security_parameters) {
        ct_security_parameters_free(preconnection->security_parameters);
    }

    free(preconnection);
}

int ct_preconnection_set_framer(ct_preconnection_t* preconnection, const ct_framer_impl_t* framer_impl) {
    if (!preconnection) {
        return -EINVAL;
    }
    if (preconnection->framer_impl) {
        log_debug("Replacing existing framer implementation in preconnection");
        ct_framer_impl_free(preconnection->framer_impl);
        preconnection->framer_impl = NULL;
    }
    if (!framer_impl) {
        log_debug("Setting framer implementation to NULL in preconnection");
        return 0;
    }
    preconnection->framer_impl = ct_framer_impl_deep_copy(framer_impl);
    if (!preconnection->framer_impl) {
        log_error("Failed to deep copy framer implementation for preconnection");
        return -ENOMEM;
    }
    return 0;
}

const ct_local_endpoint_t*
ct_preconnection_get_local_endpoints(const ct_preconnection_t* preconnection, size_t* out_count) {
    if (!out_count) {
        return NULL;
    }
    if (!preconnection) {
        *out_count = 0;
        return NULL;
    }

    *out_count = preconnection->num_local_endpoints;
    return preconnection->local_endpoints;
}

const ct_remote_endpoint_t* ct_preconnection_get_remote_endpoints(const ct_preconnection_t* preconnection, size_t* out_count) {
    if (!preconnection) {
        return NULL;
    }
    if (out_count) {
        *out_count = preconnection->num_remote_endpoints;
    }
    return preconnection->remote_endpoints;
}

const ct_transport_properties_t*
ct_preconnection_get_transport_properties(const ct_preconnection_t* preconnection) {
    if (!preconnection) {
        return NULL;
    }
    return &preconnection->transport_properties;
}

const ct_security_parameters_t*
ct_preconnection_get_security_parameters(const ct_preconnection_t* preconnection) {
    if (!preconnection) {
        return NULL;
    }
    return preconnection->security_parameters;
}

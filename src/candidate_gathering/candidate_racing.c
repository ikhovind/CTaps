#include "candidate_racing.h"

#include "candidate_gathering/candidate_gathering.h"
#include "connection/connection.h"
#include "ctaps.h"
#include <endpoint/local_endpoint.h>
#include <endpoint/remote_endpoint.h>
#include <security_parameter/security_parameters.h>
#include <errno.h>
#include <glib.h>
#include <logging/log.h>
#include <stdlib.h>
#include <string.h>
#include <uv.h>

// Forward declarations
static void initiate_next_attempt(ct_racing_context_t* context);
static void on_stagger_timer(uv_timer_t* handle);
static void racing_on_attempt_ready(struct ct_connection_s* connection);
static void on_attempt_establishment_error(ct_connection_t* connection);
static void cancel_all_other_attempts(ct_racing_context_t* context, size_t winning_index);
static int start_connection_attempt(ct_racing_context_t* context, ct_racing_attempt_t* attempt);
void handle_concluded_attempt(ct_racing_context_t* context);

void register_failed_attempt(ct_racing_context_t* context, ct_racing_attempt_t* attempt) {
    attempt->state = ATTEMPT_STATE_FAILED;
    context->completed_attempts++;
    handle_concluded_attempt(context);
}

void register_canceled_attempt(ct_racing_context_t* context, ct_racing_attempt_t* attempt) {
    attempt->state = ATTEMPT_STATE_CANCELED;
    context->completed_attempts++;
    handle_concluded_attempt(context);
}

void register_succesful_attempt(ct_racing_context_t* context, ct_racing_attempt_t* attempt) {
    attempt->state = ATTEMPT_STATE_SUCCEEDED;
    context->completed_attempts++;
    context->winning_attempt_index = attempt->attempt_index;
    context->race_complete = true;
    handle_concluded_attempt(context);
}

void raced_connection_close_cb(ct_connection_t* connection) {
    log_debug("Freeing failed connection: %s created in candidate racing", connection->uuid);
    ct_racing_attempt_t* attempt =
        (ct_racing_attempt_t*)connection->connection_callbacks.user_connection_context;
    register_canceled_attempt(attempt->context, attempt);
}

bool all_attempts_failed(ct_racing_context_t* context) {
    for (size_t i = 0; i < context->num_attempts; i++) {
        if (context->attempts[i].state != ATTEMPT_STATE_FAILED &&
            context->attempts[i].state != ATTEMPT_STATE_CANCELED) {
            return false;
        }
    }
    return true;
}

bool all_attempts_concluded(ct_racing_context_t* context) {
    for (size_t i = 0; i < context->num_attempts; i++) {
        if (context->attempts[i].state != ATTEMPT_STATE_FAILED &&
            context->attempts[i].state != ATTEMPT_STATE_CANCELED &&
            context->attempts[i].state != ATTEMPT_STATE_SUCCEEDED) {
            return false;
        }
    }
    return true;
}

static void on_timer_close_free_context(uv_handle_t* handle) {
    ct_racing_context_t* context = (ct_racing_context_t*)handle->data;
    free(handle);

    if (!context->should_try_early_data && context->initial_message) {
        log_debug("Freeing initial message and context for racing context with early data");
        ct_message_free(context->initial_message);
        ct_message_context_free(context->initial_message_context);
    }

    if (context != NULL) {
        log_debug("Timer closed, now freeing racing context");

        // Free all connection attempts
        if (context->attempts != NULL) {
            for (size_t i = 0; i < context->num_attempts; i++) {
                ct_racing_attempt_t* attempt = &context->attempts[i];
                ct_protocol_candidate_free(attempt->candidate.protocol_candidate);

                // Free candidate endpoints (they were copied)
                ct_local_endpoint_free(attempt->candidate.local_endpoint);
                ct_remote_endpoint_free(attempt->candidate.remote_endpoint);

                if (attempt->state != ATTEMPT_STATE_SUCCEEDED && attempt->connection) {
                    ct_connection_free(attempt->connection);
                }
            }
            free(context->attempts);
        }
        free(context);
    }
}

void initiate_context_close(ct_racing_context_t* context) {
    log_debug("Initiating racing context close");

    // Pass context via timer->data so close callback can free it
    context->stagger_timer->data = context;
    uv_timer_stop(context->stagger_timer);
    uv_close((uv_handle_t*)context->stagger_timer, on_timer_close_free_context);
}

void handle_all_attempts_failed(ct_racing_context_t* context) {
    log_error("All connection attempts have failed");
    context->race_complete = true;
    if (context->user_callbacks.establishment_error) {
        log_debug("Notifying user of establishment error via establishment_error callback");
        context->user_callbacks.establishment_error(NULL);
    }
    initiate_context_close(context);
}

void handle_concluded_attempt(ct_racing_context_t* context) {
    if (all_attempts_failed(context)) {
        handle_all_attempts_failed(context);
    } else if (all_attempts_concluded(context)) {
        initiate_context_close(context);
    }
}

static void racing_context_initialize_attempt_array(ct_racing_context_t* context,
                                                    GArray* candidate_nodes) {
    context->num_attempts = candidate_nodes->len;
    // Allocate attempts array
    context->attempts = calloc(context->num_attempts, sizeof(ct_racing_attempt_t));
    if (!context->attempts) {
        log_error("Failed to allocate attempts array");
        racing_context_free(context);
        return;
    }

    // Initialize each attempt
    for (size_t i = 0; i < context->num_attempts; i++) {
        memset(&context->attempts[i], 0, sizeof(ct_racing_attempt_t));
        context->attempts[i].candidate = g_array_index(candidate_nodes, ct_candidate_node_t, i);
        context->attempts[i].state = ATTEMPT_STATE_PENDING;
        context->attempts[i].attempt_index = i;
        context->attempts[i].connection = NULL;
        context->attempts[i].context = context;
    }
}

/**
 * @brief Creates a new racing context from candidate nodes.
 */
static ct_racing_context_t* racing_context_create(ct_connection_callbacks_t user_callbacks,
                                                  const ct_preconnection_t* preconnection,
                                                  ct_message_t* initial_message,
                                                  ct_message_context_t* initial_message_context,
                                                  bool should_try_early_data) {
    log_info("Creating racing context");

    ct_racing_context_t* context = malloc(sizeof(ct_racing_context_t));
    if (!context) {
        log_error("Failed to allocate racing context");
        return NULL;
    }
    memset(context, 0, sizeof(ct_racing_context_t));

    context->user_callbacks = user_callbacks;
    context->preconnection = preconnection;
    context->race_complete = false;
    context->winning_attempt_index = -1;
    context->next_attempt_index = 0;
    context->connection_attempt_delay_ms = DEFAULT_CONNECTION_ATTEMPT_DELAY_MS;
    context->initial_message = initial_message;
    context->initial_message_context = initial_message_context;
    context->should_try_early_data = should_try_early_data;

    context->completed_attempts = 0;

    // Allocate timer for staggered attempts
    context->stagger_timer = malloc(sizeof(uv_timer_t));
    if (!context->stagger_timer) {
        log_error("Failed to allocate stagger timer");
        free(context->attempts);
        free(context);
        return NULL;
    }

    uv_timer_init(event_loop, context->stagger_timer);
    context->stagger_timer->data = context;

    return context;
}

static guint sockaddr_storage_hash(gconstpointer key) {
    log_trace("Hashing sockaddr_storage for hash table");
    const struct sockaddr_storage* addr = key;
    // Hash only the relevant bytes based on address family
    if (addr->ss_family == AF_INET) {
        const struct sockaddr_in* a = (const struct sockaddr_in*)addr;
        guint h = 0;
        h ^= g_int_hash(&a->sin_port);
        guint word;
        memcpy(&word, &a->sin_addr, sizeof(word));
        h ^= g_int_hash((const gint*)&word);
        return h;
    } else if (addr->ss_family == AF_INET6) {
        const struct sockaddr_in6* a = (const struct sockaddr_in6*)addr;
        guint h = 0;
        h ^= g_int_hash((const gint*)&a->sin6_port);
        h ^= g_int_hash((const gint*)&a->sin6_scope_id);
        h ^= g_int_hash((const gint*)&a->sin6_flowinfo);
        // Fold 16-byte address as four 32-bit words
        for (int k = 0; k < 4; k++) {
            guint word;
            memcpy(&word, &a->sin6_addr.s6_addr[k * 4], sizeof(word));
            h ^= g_int_hash((const gint*)&word);
        }
        return h;
    }
    // fallback
    return g_str_hash("unknown");
}

static gboolean sockaddr_storage_equal(gconstpointer a, gconstpointer b) {
    const struct sockaddr_storage* sa = a;
    const struct sockaddr_storage* sb = b;
    if (sa->ss_family != sb->ss_family)
        return FALSE;
    if (sa->ss_family == AF_INET)
        return memcmp(a, b, sizeof(struct sockaddr_in)) == 0;
    if (sa->ss_family == AF_INET6)
        return memcmp(a, b, sizeof(struct sockaddr_in6)) == 0;
    return memcmp(a, b, sizeof(struct sockaddr_storage)) == 0;
}

/**
 * @brief Starts a single connection attempt.
 */
static int start_connection_attempt(ct_racing_context_t* context, ct_racing_attempt_t* attempt) {
    attempt->state = ATTEMPT_STATE_CONNECTING;
    ct_candidate_node_t* candidate = &attempt->candidate;

    log_debug("Attempting connection with protocol: %s",
              candidate->protocol_candidate->protocol_impl->name);

    // Wrap callbacks so that we can intercept the succesful/failing attempts
    ct_connection_callbacks_t attempt_callbacks = {
        .ready = racing_on_attempt_ready,
        .establishment_error = on_attempt_establishment_error,
        .user_connection_context = attempt,
    };

    log_debug("Removing duplicate remote endpoints for connection attempt");
    ct_remote_endpoint_t* remote_endpoints = NULL;
    size_t remote_counter = 0;
    GHashTable* remote_hash = g_hash_table_new(sockaddr_storage_hash, sockaddr_storage_equal);
    for (size_t i = 0; i < context->num_attempts; i++) {
        if (!g_hash_table_contains(
                remote_hash,
                &context->attempts[i].candidate.remote_endpoint->data.resolved_address)) {
            ct_remote_endpoint_t* tmp =
                realloc(remote_endpoints, sizeof(ct_remote_endpoint_t) * (remote_counter + 1));
            if (!tmp) {
                log_error("Failed to allocate remote endpoints array for connection attempt");
                for (size_t j = 0; j < remote_counter; j++) {
                    ct_remote_endpoint_free_content(&remote_endpoints[j]);
                }
                free(remote_endpoints);
                g_hash_table_destroy(remote_hash);
                return -ENOMEM;
            }
            remote_endpoints = tmp;
            g_hash_table_add(
                remote_hash,
                &context->attempts[i].candidate.remote_endpoint->data.resolved_address);
            int rc = ct_remote_endpoint_copy_content(context->attempts[i].candidate.remote_endpoint,
                                                     &remote_endpoints[remote_counter]);
            if (rc != 0) {
                log_error("Failed to copy remote endpoint for attempt %zu: %s", i, strerror(-rc));
                for (size_t j = 0; j < remote_counter; j++) {
                    ct_remote_endpoint_free_content(&remote_endpoints[j]);
                }
                free(remote_endpoints);
                g_hash_table_destroy(remote_hash);
                return rc;
            }
            remote_counter++;
        }
    }
    g_hash_table_destroy(remote_hash);

    log_debug("Removing duplicate local endpoints for connection attempt");
    ct_local_endpoint_t* local_endpoints = NULL;
    size_t local_endpoint_counter = 0;
    GHashTable* local_hash = g_hash_table_new(sockaddr_storage_hash, sockaddr_storage_equal);
    for (size_t i = 0; i < context->num_attempts; i++) {
        if (!g_hash_table_contains(
                local_hash,
                &context->attempts[i].candidate.local_endpoint->data.resolved_address)) {
            ct_local_endpoint_t* tmp = realloc(local_endpoints, sizeof(ct_local_endpoint_t) *
                                                                    (local_endpoint_counter + 1));
            if (!tmp) {
                log_error("Failed to allocate local endpoints array for connection attempt");
                for (size_t j = 0; j < local_endpoint_counter; j++) {
                    ct_local_endpoint_free_content(&local_endpoints[j]);
                }
                free(local_endpoints);
                g_hash_table_destroy(local_hash);
                return -ENOMEM;
            }
            local_endpoints = tmp;
            log_trace(
                "Adding local endpoint for attempt %zu to hash table and local endpoints array", i);
            g_hash_table_add(local_hash,
                             &context->attempts[i].candidate.local_endpoint->data.resolved_address);
            int rc = ct_local_endpoint_copy_content(context->attempts[i].candidate.local_endpoint,
                                                    &local_endpoints[local_endpoint_counter]);
            if (rc != 0) {
                log_error("Failed to copy local endpoint for attempt %zu: %s", i, strerror(-rc));
                for (size_t j = 0; j < local_endpoint_counter; j++) {
                    ct_local_endpoint_free_content(&local_endpoints[j]);
                }
                free(local_endpoints);
                g_hash_table_destroy(local_hash);
                return rc;
            }
            local_endpoint_counter++;
        }
    }
    g_hash_table_destroy(local_hash);
    size_t active_remote_index = 0;
    for (size_t i = 0; i < remote_counter; i++) {
        if (memcmp(&remote_endpoints[i].data.resolved_address,
                   &attempt->candidate.remote_endpoint->data.resolved_address,
                   sizeof(struct sockaddr_storage)) == 0) {
            active_remote_index = i;
            break;
        }
    }

    size_t active_local_index = 0;
    for (size_t i = 0; i < local_endpoint_counter; i++) {
        if (memcmp(&local_endpoints[i].data.resolved_address,
                   &attempt->candidate.local_endpoint->data.resolved_address,
                   sizeof(struct sockaddr_storage)) == 0) {
            active_local_index = i;
            break;
        }
    }

    // Allocate connection for this attempt
    attempt->connection = ct_connection_create_client(
        candidate->protocol_candidate->protocol_impl, local_endpoints, local_endpoint_counter,
        active_local_index, remote_endpoints, remote_counter, active_remote_index,
        context->preconnection->security_parameters, &attempt_callbacks,
        context->preconnection->framer_impl);

    if (!attempt->connection) {
        log_error("Failed to allocate connection for connection attempt");
        ct_local_endpoints_free(local_endpoints, local_endpoint_counter);
        ct_remote_endpoints_free(remote_endpoints, remote_counter);
        return -ENOMEM;
    }

    if (context->preconnection->security_parameters) {
        // When branching we assign a single ALPN to each node (if the protocol supports ALPN)
        // However the security parameters contain all the original ALPNs from the preconnection
        // So to make sure that this connection attempt uses the ALPN from the candidate node
        // we just overwrite this (deeply copied) ALPN value
        // The old ALPN value is freed in the setter.
        ct_security_parameters_clear_alpn(attempt->connection->security_parameters);
        ct_security_parameters_add_alpn(attempt->connection->security_parameters,
                                        attempt->candidate.protocol_candidate->alpn);
    }

    int rc = 0;
    if (context->should_try_early_data) {
        log_debug("Initiating racing connection attempt with early data");
        rc = attempt->connection->socket_manager->protocol_impl->init_with_send(
            attempt->connection, context->initial_message, context->initial_message_context);
    } else {
        log_debug("Initiating racing connection attempt without early data");
        rc = attempt->connection->socket_manager->protocol_impl->init(attempt->connection);
    }
    if (rc != 0) {
        log_error("Failed to initiate connection attempt: %d", rc);
        return rc;
    }

    return 0;
}

/**
 * @brief ct_callback_t when a connection attempt succeeds.
 */
void racing_on_attempt_ready(ct_connection_t* connection) {
    ct_racing_attempt_t* attempt =
        (ct_racing_attempt_t*)connection->connection_callbacks.user_connection_context;
    ct_racing_context_t* context = attempt->context;

    log_info("ct_connection_t attempt %zu succeeded!", attempt->attempt_index);
    log_info("connection ptr: %p", (void*)connection);

    // Check if race is already complete (another attempt won)
    if (context->race_complete) {
        // If we reach this, we have already called ct_connection_close on this succesfull
        // connection, so do nothing here since we are waiting for the closed() callback
        log_debug("Race already complete, ignoring success of attempt %zu", attempt->attempt_index);
        return;
    }

    // Mark the race as complete
    register_succesful_attempt(context, attempt);

    cancel_all_other_attempts(context, attempt->attempt_index);

    // Restore the user's original callbacks (connection has the wrapped racing callbacks)
    connection->connection_callbacks = context->user_callbacks;

    // If we didn't send the initial message as early data, send it now
    if (!context->should_try_early_data && context->initial_message) {
        log_debug("Sending initial message on winning connection");
        // Because this is a part of the public API, it takes a deep copy of the message and message context.
        // Our copies are freed in racing_context_free-flow
        int rc = ct_send_message_full(connection, context->initial_message,
                                      context->initial_message_context);
        if (rc != 0) {
            log_error("Failed to send initial message on winning connection: %d", rc);
            ct_socket_manager_t* socket_manager = connection->socket_manager;
            socket_manager->callbacks.message_send_error(connection,
                                                         context->initial_message_context, rc);
        }
    }

    if (ct_connection_sent_early_data(connection)) {
        connection->socket_manager->callbacks.message_sent(connection,
                                                           context->initial_message_context);
    }

    // Call the user's ready callback with the winning connection
    if (connection->connection_callbacks.ready) {
        log_info("Notifying user of successful connection via ready callback");
        connection->connection_callbacks.ready(connection);
    }
    log_warn("User connection ready callback is NULL, cannot notify of successful connection");
}

/**
 * @brief ct_callback_t when a connection attempt fails.
 */
static void on_attempt_establishment_error(ct_connection_t* connection) {
    log_debug("Received establishment error for connection: %s", connection->uuid);
    ct_racing_attempt_t* attempt =
        (ct_racing_attempt_t*)connection->connection_callbacks.user_connection_context;
    ct_racing_context_t* context = attempt->context;

    log_info("ct_connection_t attempt %zu failed", attempt->attempt_index);

    // Check if race is already complete (another attempt won)
    if (context->race_complete) {
        log_debug("Race already complete, ignoring this failure");
        return;
    }

    register_failed_attempt(context, attempt);
}

/**
 * @brief Cancels all connection attempts except the winning one.
 */
static void cancel_all_other_attempts(ct_racing_context_t* context, size_t winning_index) {
    log_info("Canceling all attempts except winner (attempt %zu)", winning_index);

    for (size_t i = 0; i < context->num_attempts; i++) {
        if (i == winning_index) {
            continue;
        }

        ct_racing_attempt_t* attempt = &context->attempts[i];
        if (attempt->state == ATTEMPT_STATE_CONNECTING) {
            log_debug("Canceling attempt %zu", i);
            attempt->state = ATTEMPT_STATE_CLOSING;
            attempt->connection->connection_callbacks.closed = raced_connection_close_cb;
            ct_connection_close_group(attempt->connection);
        } else if (attempt->state == ATTEMPT_STATE_PENDING) {
            // This attempt hasn't started yet, so just mark it as canceled
            log_debug("Marking pending attempt %zu as canceled", i);
            register_canceled_attempt(context, attempt);
        } else {
            log_debug("Not handling attempt %zu with state %d in cancel_all_other_attempts", i,
                      attempt->state);
        }
    }
}

/**
 * @brief Timer callback for initiating the next staggered attempt.
 */
static void on_stagger_timer(uv_timer_t* handle) {
    ct_racing_context_t* context = (ct_racing_context_t*)handle->data;

    log_debug("Stagger timer fired, initiating next attempt");

    if (context->race_complete) {
        log_debug("Race already complete, not starting new attempt");
        return;
    }

    initiate_next_attempt(context);
}

/**
 * @brief Initiates the next pending connection attempt.
 */
static void initiate_next_attempt(ct_racing_context_t* context) {
    if (context->race_complete) {
        log_debug("Candidate Racing complete, ignoring attempts to initiate new attempts");
        return;
    }

    if (context->next_attempt_index >= context->num_attempts) {
        log_debug("All connection attempts have been initiated");
        return;
    }

    log_info("Starting connection attempt %zu/%zu", context->next_attempt_index + 1,
             context->num_attempts);

    ct_racing_attempt_t* attempt = &context->attempts[context->next_attempt_index];

    int rc = start_connection_attempt(context, attempt);
    if (rc != 0) {
        log_warn("Failed to start attempt %zu/%zu, error code: %d", context->next_attempt_index + 1,
                 context->num_attempts, rc);
        register_failed_attempt(context, attempt);
        if (all_attempts_failed(context)) {
            handle_all_attempts_failed(context);
            return;
        }
    }

    context->next_attempt_index++;

    // Schedule next attempt if there are more candidates
    if (context->next_attempt_index < context->num_attempts) {
        log_debug("Scheduling next attempt in %lu ms", context->connection_attempt_delay_ms);
        uv_timer_start(context->stagger_timer, on_stagger_timer,
                       context->connection_attempt_delay_ms, 0);
        log_debug("Stagger timer started");
    }
}

void start_candidate_racing_on_nodes_ready(GArray* candidate_nodes, void* context) {
    ct_racing_context_t* racing_context = (ct_racing_context_t*)context;
    ct_connection_callbacks_t connection_callbacks = racing_context->user_callbacks;

    if (!candidate_nodes) {
        log_error("Could not allocate memory for candidate nodes");
        if (connection_callbacks.establishment_error) {
            connection_callbacks.establishment_error(NULL);
        }
        racing_context_free(racing_context);
        return;
    }
    if (candidate_nodes->len == 0) {
        log_error("No compatible candidates found for racing");
        free_candidate_array(candidate_nodes);
        if (connection_callbacks.establishment_error) {
            log_debug("Notifying user of establishment error via establishment_error callback");
            connection_callbacks.establishment_error(NULL);
        } else {
            log_debug("No establishment_error callback provided by user");
        }
        racing_context_free(racing_context);
        return;
    }

    racing_context_initialize_attempt_array(racing_context, candidate_nodes);

    log_info("Racing with %d candidates", candidate_nodes->len);

    // Start the first attempt immediately
    initiate_next_attempt(context);

    // The racing context manages the rest asynchronously via the event loop
    // The user will get notified via the ready/establishment_error callbacks
    // Note: We don't free the context here - it needs to live until racing completes
    g_array_free(candidate_nodes, true);
}

/**
 * @brief Main entry point for initiating connection with racing.
 */
int start_candidate_racing(ct_preconnection_t* preconnection,
                           ct_connection_callbacks_t connection_callbacks,
                           ct_message_t* initial_message,
                           ct_message_context_t* initial_message_context,
                           bool should_try_early_data) {

    ct_racing_context_t* context =
        racing_context_create(connection_callbacks, preconnection, initial_message,
                              initial_message_context, should_try_early_data);

    if (!context) {
        log_error("Failed to create racing context");
        return -ENOMEM;
    }

    ct_candidate_gathering_callbacks_t gathering_callbacks = {
        .candidate_node_array_ready_cb = start_candidate_racing_on_nodes_ready,
        .context = context,
    };

    // Get ordered candidate nodes
    int rc = ct_get_ordered_candidate_nodes(preconnection, gathering_callbacks);
    if (rc != 0) {
        log_error("Synchronous error in getting ordered candidate nodes: %d", rc);
        return rc;
    }
    return 0;
}

int preconnection_race_with_early_data(ct_preconnection_t* preconnection,
                                       ct_connection_callbacks_t connection_callbacks,
                                       ct_message_t* initial_message,
                                       ct_message_context_t* initial_message_context) {
    return start_candidate_racing(preconnection, connection_callbacks, initial_message,
                                  initial_message_context, true);
}

int preconnection_race_with_send_after_ready(ct_preconnection_t* preconnection,
                                             ct_connection_callbacks_t connection_callbacks,
                                             ct_message_t* initial_message,
                                             ct_message_context_t* initial_message_context) {
    return start_candidate_racing(preconnection, connection_callbacks, initial_message,
                                  initial_message_context, false);
}

int preconnection_race(ct_preconnection_t* preconnection,
                       ct_connection_callbacks_t connection_callbacks) {
    return start_candidate_racing(preconnection, connection_callbacks, NULL, NULL, false);
}

/**
 * @brief Frees a racing context and all associated resources.
 *
 * Does not free the user connection.
 * Note: This function initiates async cleanup. The context will be freed
 * when all async operations (timer close) complete.
 */
void racing_context_free(ct_racing_context_t* context) {
    log_debug("Initiating racing context cleanup");

    // Pass context via timer->data so close callback can free it
    context->stagger_timer->data = context;
    // Timer has already been stopped in the cancel function
    uv_close((uv_handle_t*)context->stagger_timer, on_timer_close_free_context);
}

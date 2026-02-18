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
static int racing_on_attempt_ready(struct ct_connection_s* connection);
static int on_attempt_establishment_error(ct_connection_t* connection);
static void cancel_all_other_attempts(ct_racing_context_t* context, size_t winning_index);
static int start_connection_attempt(ct_racing_context_t* context, ct_racing_attempt_t* attempt);




int free_connection_on_close_cb(ct_connection_t* connection) {
  log_trace("Freeing failed connection: %s created in candidate racing", connection->uuid);
  ct_connection_free(connection);
  return 0;
}

bool all_attempts_failed(ct_racing_context_t* context) {
  for (size_t i = 0; i < context->num_attempts; i++) {
    if (context->attempts[i].state != ATTEMPT_STATE_FAILED) {
      return false;
    }
  }
  return true;
}

static void on_timer_close_free_context(uv_handle_t* handle) {
  ct_racing_context_t* context = (ct_racing_context_t*)handle->data;
  free(handle);

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

void register_failed_attempt(ct_racing_context_t* context, ct_racing_attempt_t* attempt) {
  attempt->state = ATTEMPT_STATE_FAILED;
  context->completed_attempts++;
}



/**
 * @brief Creates a new racing context from candidate nodes.
 */
static ct_racing_context_t* racing_context_create(GArray* candidate_nodes,
                                            ct_connection_callbacks_t user_callbacks,
                                            const ct_preconnection_t* preconnection,
                                            ct_message_t* initial_message,
                                            ct_message_context_t* initial_message_context,
                                            bool should_try_early_data 
                                            ) {
  log_info("Creating racing context with %d candidates", candidate_nodes->len);

  ct_racing_context_t* context = malloc(sizeof(ct_racing_context_t));
  if (!context) {
    log_error("Failed to allocate racing context");
    return NULL;
  }
  memset(context, 0, sizeof(ct_racing_context_t));

  context->num_attempts = candidate_nodes->len;
  context->user_callbacks = user_callbacks;
  context->preconnection = preconnection;
  context->race_complete = false;
  context->winning_attempt_index = -1;
  context->next_attempt_index = 0;
  context->connection_attempt_delay_ms = DEFAULT_CONNECTION_ATTEMPT_DELAY_MS;
  context->initial_message = initial_message;
  context->initial_message_context = initial_message_context;
  context->should_try_early_data = should_try_early_data;


  // Allocate attempts array
  context->attempts = calloc(context->num_attempts, sizeof(ct_racing_attempt_t));
  if (!context->attempts) {
    log_error("Failed to allocate attempts array");
    free(context);
    return NULL;
  }

  // Initialize each attempt
  for (size_t i = 0; i < context->num_attempts; i++) {
    context->attempts[i].candidate = g_array_index(candidate_nodes, ct_candidate_node_t, i);
    context->attempts[i].state = ATTEMPT_STATE_PENDING;
    context->attempts[i].attempt_index = i;
    context->attempts[i].connection = NULL;
    context->attempts[i].context = context;  // Set back-pointer
  }

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

/**
 * @brief Starts a single connection attempt.
 */
static int start_connection_attempt(ct_racing_context_t* context, ct_racing_attempt_t* attempt) {
  attempt->state = ATTEMPT_STATE_CONNECTING;
  ct_candidate_node_t* candidate = &attempt->candidate;

  log_debug("Attempting connection with protocol: %s", candidate->protocol_candidate->protocol_impl->name);

  // Wrap callbacks so that we can intercept the succesful/failing attempts
  ct_connection_callbacks_t attempt_callbacks = {
    .ready = racing_on_attempt_ready,
    .establishment_error = on_attempt_establishment_error,
    .user_connection_context = attempt,
  };

  // Allocate connection for this attempt
  attempt->connection = ct_connection_create_client(
    candidate->protocol_candidate->protocol_impl,
    candidate->local_endpoint,
    candidate->remote_endpoint,
    &context->preconnection->transport_properties,
    context->preconnection->security_parameters,
    &attempt_callbacks,
    context->preconnection->framer_impl
  );

  if (!attempt->connection) {
    log_error("Failed to allocate connection for connection attempt");
    return -ENOMEM;
  }

  if (context->preconnection->security_parameters) {
    // When branching we assign a single ALPN to each node (if the protocol supports ALPN)
    // However the security parameters contain all the original ALPNs from the preconnection
    // So to make sure that this connection attempt uses the ALPN from the candidate node
    // we just overwrite this (deeply copied) ALPN value
    // The old ALPN value is freed in the setter.
    ct_sec_param_set_property_string_array(attempt->connection->security_parameters, ALPN, (const char**)&attempt->candidate.protocol_candidate->alpn, 1);
  }

  int rc = 0;
  if (context->should_try_early_data) {
    log_debug("Initiating racing connection attempt with early data");
    rc = attempt->connection->socket_manager->protocol_impl->init_with_send(attempt->connection, &attempt->connection->connection_callbacks, context->initial_message, context->initial_message_context);
  }
  else {
    log_debug("Initiating racing connection attempt without early data");
    rc = attempt->connection->socket_manager->protocol_impl->init(attempt->connection, &attempt->connection->connection_callbacks);
  }
  if (rc != 0) {
    log_error("Failed to initiate connection attempt: %d", rc);
    ct_connection_free(attempt->connection);
    return rc;
  }

  return 0;
}

/**
 * @brief ct_callback_t when a connection attempt succeeds.
 */
int racing_on_attempt_ready(ct_connection_t* connection) {
  ct_racing_attempt_t* attempt = (ct_racing_attempt_t*)connection->connection_callbacks.user_connection_context;
  ct_racing_context_t* context = attempt->context;

  log_info("ct_connection_t attempt %zu succeeded!", attempt->attempt_index);
  log_info("connection ptr: %p", (void*)connection);

  // Check if race is already complete (another attempt won)
  if (context->race_complete) {
    log_debug("Race already complete, ignoring this success");
    return 0;
  }

  // Mark the race as complete
  context->race_complete = true;
  context->winning_attempt_index = (int)attempt->attempt_index;
  attempt->state = ATTEMPT_STATE_SUCCEEDED;

  cancel_all_other_attempts(context, attempt->attempt_index);

  // Restore the user's original callbacks (connection has the wrapped racing callbacks)
  connection->connection_callbacks = context->user_callbacks;

  // If we didn't send the initial message as early data, send it now
  if (!context->should_try_early_data && context->initial_message) {
    log_debug("Sending initial message on winning connection");
    // This takes deep copy of message and context, so freeing our copies is fine in racing_context_free
    int rc = ct_send_message_full(connection, context->initial_message, context->initial_message_context);
    if (rc != 0) {
      log_error("Failed to send initial message on winning connection: %d", rc);
      if (connection->connection_callbacks.send_error) {
        connection->connection_callbacks.send_error(connection);
      }
    }
  }

  log_debug("Freeing racing context after having found successful candidate");
  racing_context_free(context);

  // Call the user's ready callback with the winning connection
  if (connection->connection_callbacks.ready) {
    log_info("Notifying user of successful connection via ready callback");
    return connection->connection_callbacks.ready(connection);
  }
  log_warn("User connection ready callback is NULL, cannot notify of successful connection");

  return 0;
}

/**
 * @brief ct_callback_t when a connection attempt fails.
 */
static int on_attempt_establishment_error(ct_connection_t* connection) {
  ct_racing_attempt_t* attempt = (ct_racing_attempt_t*)connection->connection_callbacks.user_connection_context;
  ct_racing_context_t* context = attempt->context;

  log_info("ct_connection_t attempt %zu failed", attempt->attempt_index);

  // Check if race is already complete (another attempt won)
  if (context->race_complete) {
    log_debug("Race already complete, ignoring this failure");
    return 0;
  }

  register_failed_attempt(context, attempt);
  ct_connection_free(connection);
  if (all_attempts_failed(context)) {
    log_error("All connection attempts have failed");
    handle_all_attempts_failed(context);
  }
  return 0;
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
      attempt->state = ATTEMPT_STATE_CANCELED;

      if (attempt->connection) {
        attempt->connection->connection_callbacks.closed = free_connection_on_close_cb;
        ct_connection_close(attempt->connection);
      }
    }
  }

  // Stop the stagger timer
  if (context->stagger_timer != NULL) {
    uv_timer_stop(context->stagger_timer);
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

  log_info("Starting connection attempt %zu/%zu", context->next_attempt_index + 1, context->num_attempts);

  ct_racing_attempt_t* attempt = &context->attempts[context->next_attempt_index];

  int rc = start_connection_attempt(context, attempt);
  if (rc != 0) {
    log_warn("Failed to start attempt %zu: %d", context->next_attempt_index, rc);
    register_failed_attempt(context, attempt);
    if (all_attempts_failed(context)) {
      log_error("All connection attempts have failed");
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

/**
 * @brief Main entry point for initiating connection with racing.
 */
int start_candidate_racing(ct_preconnection_t* preconnection,
                                       ct_connection_callbacks_t connection_callbacks,
                                       ct_message_t* initial_message,
                                       ct_message_context_t* initial_message_context,
                                       bool should_try_early_data
                                       ) {
  // Get ordered candidate nodes
  GArray* candidate_nodes = get_ordered_candidate_nodes(preconnection);
  if (!candidate_nodes) {
    log_error("Could not allocate memory for candidate nodes");
    if (connection_callbacks.establishment_error) {
      connection_callbacks.establishment_error(NULL);
    }
    return -ENOMEM;
  }
  if (candidate_nodes->len == 0) {
    log_error("No compatible candidates found for racing");
    free_candidate_array(candidate_nodes);
    if (connection_callbacks.establishment_error) {
      connection_callbacks.establishment_error(NULL);
    }
    return -EINVAL;
  }

  log_info("Racing with %d candidates", candidate_nodes->len);

  ct_racing_context_t* context = racing_context_create(candidate_nodes,
                                                       connection_callbacks,
                                                       preconnection,
                                                       initial_message,
                                                       initial_message_context,
                                                       should_try_early_data
                                                       );

  if (!context) {
    log_error("Failed to create racing context");
    free_candidate_array(candidate_nodes);
    if (connection_callbacks.establishment_error) {
      connection_callbacks.establishment_error(NULL);
    }
    return -ENOMEM;
  }

  // Free the GArray structure but not the ct_candidate_node_t data (endpoints are already copied)
  // The racing context now owns the ct_candidate_node_t data
  g_array_free(candidate_nodes, true);

  // Start the first attempt immediately
  initiate_next_attempt(context);

  // The racing context manages the rest asynchronously via the event loop
  // The user will get notified via the ready/establishment_error callbacks
  // Note: We don't free the context here - it needs to live until racing completes

  return 0;
}

int preconnection_race_with_early_data(ct_preconnection_t* preconnection,
                                       ct_connection_callbacks_t connection_callbacks,
                                       ct_message_t* initial_message,
                                       ct_message_context_t* initial_message_context) {
  return start_candidate_racing(preconnection, connection_callbacks, initial_message, initial_message_context, true);
}

int preconnection_race_with_send_after_ready(ct_preconnection_t* preconnection,
                                       ct_connection_callbacks_t connection_callbacks,
                                       ct_message_t* initial_message,
                                       ct_message_context_t* initial_message_context) {
  return start_candidate_racing(preconnection, connection_callbacks, initial_message, initial_message_context, false);
}

int preconnection_race(ct_preconnection_t* preconnection, ct_connection_callbacks_t connection_callbacks) {
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

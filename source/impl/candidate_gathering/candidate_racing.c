#include "candidate_racing.h"

#include <stdlib.h>
#include <string.h>
#include <logging/log.h>
#include <ctaps.h>
#include <connections/connection/connection_callbacks.h>

// Forward declarations
static void initiate_next_attempt(RacingContext* context);
static void on_stagger_timer(uv_timer_t* handle);
static int racing_on_attempt_ready(struct Connection* connection, void* udata);
static int on_attempt_establishment_error(Connection* connection, void* udata);
static void cancel_all_other_attempts(RacingContext* context, int winning_index);
static int start_connection_attempt(RacingContext* context, int attempt_index);

static void on_handle_close(uv_handle_t* handle) {
  free(handle);
}

/**
 * @brief Creates a new racing context from candidate nodes.
 */
static RacingContext* racing_context_create(GArray* candidate_nodes,
                                            Connection* user_connection,
                                            ConnectionCallbacks user_callbacks,
                                            const Preconnection* preconnection) {
  log_info("Creating racing context with %d candidates", candidate_nodes->len);

  RacingContext* context = malloc(sizeof(RacingContext));
  if (context == NULL) {
    log_error("Failed to allocate racing context");
    return NULL;
  }
  memset(context, 0, sizeof(RacingContext));

  context->num_attempts = candidate_nodes->len;
  context->user_callbacks = user_callbacks;
  context->user_connection = user_connection;
  context->preconnection = preconnection;
  context->race_complete = false;
  context->winning_attempt_index = -1;
  context->next_attempt_index = 0;
  context->connection_attempt_delay_ms = DEFAULT_CONNECTION_ATTEMPT_DELAY_MS;

  // Allocate attempts array
  context->attempts = calloc(context->num_attempts, sizeof(RacingAttempt));
  if (context->attempts == NULL) {
    log_error("Failed to allocate attempts array");
    free(context);
    return NULL;
  }

  // Initialize each attempt
  for (size_t i = 0; i < context->num_attempts; i++) {
    context->attempts[i].candidate = g_array_index(candidate_nodes, CandidateNode, i);
    context->attempts[i].state = ATTEMPT_STATE_PENDING;
    context->attempts[i].attempt_index = i;
    context->attempts[i].connection = NULL;
    context->attempts[i].context = context;  // Set back-pointer
  }

  context->completed_attempts = 0;

  // Allocate timer for staggered attempts
  context->stagger_timer = malloc(sizeof(uv_timer_t));
  if (context->stagger_timer == NULL) {
    log_error("Failed to allocate stagger timer");
    free(context->attempts);
    free(context);
    return NULL;
  }

  uv_timer_init(ctaps_event_loop, context->stagger_timer);
  context->stagger_timer->data = context;

  return context;
}

/**
 * @brief Starts a single connection attempt.
 */
static int start_connection_attempt(RacingContext* context, int attempt_index) {
  log_info("Starting connection attempt %d/%zu", attempt_index + 1, context->num_attempts);

  if (attempt_index >= context->num_attempts) {
    log_error("Invalid attempt index: %d", attempt_index);
    return -EINVAL;
  }

  RacingAttempt* attempt = &context->attempts[attempt_index];
  CandidateNode* candidate = &attempt->candidate;

  log_debug("Attempting connection with protocol: %s", candidate->protocol->name);

  // Allocate connection for this attempt
  attempt->connection = malloc(sizeof(Connection));
  if (attempt->connection == NULL) {
    log_error("Failed to allocate connection for attempt %d", attempt_index);
    attempt->state = ATTEMPT_STATE_FAILED;
    return -ENOMEM;
  }
  memset(attempt->connection, 0, sizeof(Connection));

  // Setup connection with candidate parameters
  attempt->connection->protocol = *candidate->protocol;
  attempt->connection->remote_endpoint = *candidate->remote_endpoint;
  attempt->connection->local_endpoint = *candidate->local_endpoint;
  attempt->connection->open_type = CONNECTION_TYPE_STANDALONE;
  attempt->connection->security_parameters = context->preconnection->security_parameters;
  attempt->connection->received_messages = g_queue_new();
  attempt->connection->received_callbacks = g_queue_new();

  // Setup wrapped callbacks that point back to this attempt
  ConnectionCallbacks attempt_callbacks = {
    .ready = racing_on_attempt_ready,
    .establishment_error = on_attempt_establishment_error,
    .user_data = attempt,
  };

  attempt->connection->connection_callbacks = attempt_callbacks;
  attempt->state = ATTEMPT_STATE_CONNECTING;

  // Initiate the connection using the protocol's init function
  int rc = attempt->connection->protocol.init(attempt->connection, &attempt->connection->connection_callbacks);
  if (rc != 0) {
    log_error("Failed to initiate connection attempt %d: %d", attempt_index, rc);
    attempt->state = ATTEMPT_STATE_FAILED;
    g_queue_free(attempt->connection->received_messages);
    g_queue_free(attempt->connection->received_callbacks);
    free(attempt->connection);
    attempt->connection = NULL;
    return rc;
  }

  return 0;
}

/**
 * @brief Callback when a connection attempt succeeds.
 */
int racing_on_attempt_ready(Connection* connection, void* udata) {
  RacingAttempt* attempt = (RacingAttempt*)udata;
  RacingContext* context = attempt->context;

  log_info("Connection attempt %d succeeded!", attempt->attempt_index);
  log_info("connection ptr: %p", (void*)connection);

  // Check if race is already complete (another attempt won)
  if (context->race_complete) {
    log_debug("Race already complete, ignoring this success");
    return 0;
  }

  // Mark the race as complete
  context->race_complete = true;
  context->winning_attempt_index = attempt->attempt_index;
  attempt->state = ATTEMPT_STATE_SUCCEEDED;

  // Cancel all other attempts
  cancel_all_other_attempts(context, attempt->attempt_index);

  // Copy the winning connection to the user's connection object
  // Preserve the user connection's queues (may contain early receive_message() calls)
  GQueue* saved_messages = context->user_connection->received_messages;
  GQueue* saved_callbacks = context->user_connection->received_callbacks;

  ConnectionCallbacks saved_callbacks_struct = context->user_connection->connection_callbacks;

  memcpy(context->user_connection, connection, sizeof(Connection));

  // Restore the user connection's original callbacks, 'connection' has the wrapped ones
  context->user_connection->connection_callbacks = saved_callbacks_struct;

  context->user_connection->received_messages = saved_messages;
  context->user_connection->received_callbacks = saved_callbacks;

  // Update protocol internal pointers to reference the user connection
  // This is protocol-specific (TCP/UDP update handle->data, QUIC also updates picoquic callback context)
  if (context->user_connection->protocol.retarget_protocol_connection) {
    context->user_connection->protocol.retarget_protocol_connection(
      connection,  // from_connection (whose protocol_state we're using)
      context->user_connection  // to_connection (the new target for callbacks)
    );
  }
  else {
    log_warn("Retargeting function not implemented for protocol: %s,"
             " this may lead to callbacks not having the correct context",
             context->user_connection->protocol.name);
  }

  Connection* user_connection = context->user_connection;

  log_debug("Freeing racing context after having found successfull candidate");
  racing_context_free(context);

  // Call the user's ready callback with the winning connection
  if (user_connection->connection_callbacks.ready) {
    log_info("Notifying user of successful connection via ready callback");
    return user_connection->connection_callbacks.ready(user_connection,
                                                       user_connection->connection_callbacks.user_data);
  }
  else {
    log_warn("User connection ready callback is NULL, cannot notify of successful connection");
  }

  return 0;
}

/**
 * @brief Callback when a connection attempt fails.
 */
static int on_attempt_establishment_error(Connection* connection, void* udata) {
  RacingAttempt* attempt = (RacingAttempt*)udata;
  RacingContext* context = attempt->context;

  log_info("Connection attempt %d failed", attempt->attempt_index);

  // set connection state to CLOSED
  log_debug("Setting connection state to CLOSED for failed attempt %d", attempt->attempt_index);
  connection->transport_properties.connection_properties.list[STATE].value.enum_val = CONN_STATE_CLOSED;

  // Check if race is already complete (another attempt won)
  if (context->race_complete) {
    log_debug("Race already complete, ignoring this failure");
    return 0;
  }

  attempt->state = ATTEMPT_STATE_FAILED;
  context->completed_attempts++;

  // Check if all attempts have failed
  size_t failed_count = 0;
  for (size_t i = 0; i < context->num_attempts; i++) {
    if (context->attempts[i].state >= ATTEMPT_STATE_FAILED) {
      failed_count++;
    }
    else { // There's at least one attempt still pending or connecting
      return 0; 
    }
  }

  int rc = 0;
  if (failed_count == context->num_attempts) {
    log_error("All connection attempts failed");
    context->race_complete = true;

    // Stop the timer
    if (context->stagger_timer != NULL) {
      uv_timer_stop(context->stagger_timer);
    }

    // Mark the user connection as closed since all attempts failed
    log_debug("Setting user connection state to CLOSED after all attempts failed");
    context->user_connection->transport_properties.connection_properties.list[STATE].value.enum_val = CONN_STATE_CLOSED;

    // Call the user's establishment_error callback
    if (context->user_callbacks.establishment_error) {
      rc = context->user_callbacks.establishment_error(context->user_connection,
                                                          context->user_callbacks.user_data);
    }
    log_debug("Freeing race context from failure callback");
    racing_context_free(context);
  }

  return rc;
}

/**
 * @brief Cancels all connection attempts except the winning one.
 */
static void cancel_all_other_attempts(RacingContext* context, int winning_index) {
  log_info("Canceling all attempts except winner (attempt %d)", winning_index);

  for (size_t i = 0; i < context->num_attempts; i++) {
    if (i == winning_index) {
      continue;
    }

    RacingAttempt* attempt = &context->attempts[i];
    if (attempt->state == ATTEMPT_STATE_CONNECTING) {
      log_debug("Canceling attempt %zu", i);
      attempt->state = ATTEMPT_STATE_CANCELED;

      if (attempt->connection != NULL) {
        connection_close(attempt->connection);
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
  RacingContext* context = (RacingContext*)handle->data;

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
static void initiate_next_attempt(RacingContext* context) {
  if (context->race_complete) {
    log_debug("Candidate Racing complete, ignoring attempts to initiate new attempts");
    return;
  }

  if (context->next_attempt_index >= context->num_attempts) {
    log_debug("All connection attempts have been initiated");
    return;
  }

  int rc = start_connection_attempt(context, context->next_attempt_index);
  if (rc != 0) {
    log_warn("Failed to start attempt %zu: %d", context->next_attempt_index, rc);
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
int preconnection_initiate_with_racing(Preconnection* preconnection,
                                       Connection* user_connection,
                                       ConnectionCallbacks connection_callbacks) {
  // Initialize user connection immediately so it's usable (e.g., for early receive_message() calls)
  log_trace("About to build user connection from preconnection");
  preconnection_build_user_connection(user_connection, preconnection, connection_callbacks);

  // Get ordered candidate nodes
  GArray* candidate_nodes = get_ordered_candidate_nodes(preconnection);
  // TODO - NULL vs 0 len are different, handle differently
  if (candidate_nodes == NULL || candidate_nodes->len == 0) {
    log_error("No candidates available for racing");
    if (candidate_nodes != NULL) {
      free_candidate_array(candidate_nodes);
    }
    return -EINVAL;
  }

  log_info("Racing with %d candidates", candidate_nodes->len);

  // If only one candidate, don't bother with racing
  if (candidate_nodes->len == 1) {
    log_debug("Only one candidate, initiating directly without racing");
    CandidateNode first_node = g_array_index(candidate_nodes, CandidateNode, 0);

    // Set protocol and endpoints from the candidate
    user_connection->protocol = *first_node.protocol;
    user_connection->remote_endpoint = *first_node.remote_endpoint;
    user_connection->local_endpoint = *first_node.local_endpoint;
    user_connection->connection_callbacks = connection_callbacks;

    int rc = user_connection->protocol.init(user_connection, &user_connection->connection_callbacks);
    free_candidate_array(candidate_nodes);
    return rc;
  }

  log_info("User connection ready pointer before creating context: %p",
           user_connection->connection_callbacks.ready);
  RacingContext* context = racing_context_create(candidate_nodes, user_connection,
                                                 connection_callbacks, preconnection);
  if (context == NULL) {
    log_error("Failed to create racing context");
    free_candidate_array(candidate_nodes);
    return -ENOMEM;
  }

  // Free the GArray structure but not the CandidateNode data (endpoints are already copied)
  // The racing context now owns the CandidateNode data
  g_array_free(candidate_nodes, FALSE);

  // Start the first attempt immediately
  initiate_next_attempt(context);

  // The racing context manages the rest asynchronously via the event loop
  // The user will get notified via the ready/establishment_error callbacks
  // Note: We don't free the context here - it needs to live until racing completes
  // TODO: Implement proper cleanup mechanism (perhaps via a close handle callback)

  return 0;
}

/**
 * @brief Frees a racing context and all associated resources.
 *
 * Does not free the user connection
 */
void racing_context_free(RacingContext* context) {
  if (context == NULL) {
    return;
  }

  log_debug("Freeing racing context");

  // Stop and free timer
  if (context->stagger_timer != NULL) {
    uv_timer_stop(context->stagger_timer);
    uv_close((uv_handle_t*)context->stagger_timer, on_handle_close);
  }

  // Free all connection attempts
  if (context->attempts != NULL) {
    for (size_t i = 0; i < context->num_attempts; i++) {
      RacingAttempt* attempt = &context->attempts[i];

      // Free candidate endpoints (they were copied)
      free_local_endpoint(attempt->candidate.local_endpoint);
      free_remote_endpoint(attempt->candidate.remote_endpoint);

      // Free connection if it wasn't the winner
      if (attempt->connection != NULL && i != context->winning_attempt_index) {
        if (attempt->connection->received_messages) {
          g_queue_free(attempt->connection->received_messages);
        }
        if (attempt->connection->received_callbacks) {
          g_queue_free(attempt->connection->received_callbacks);
        }
        free(attempt->connection);
      }
    }
    free(context->attempts);
  }

  free(context);
}

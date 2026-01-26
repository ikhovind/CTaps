#ifndef CANDIDATE_RACING_H
#define CANDIDATE_RACING_H

#include <glib.h>
#include <uv.h>

#include "ctaps.h"
#include "ctaps_internal.h"
#include "candidate_gathering.h"

// Default connection attempt delay in milliseconds (per Happy Eyeballs RFC 8305)
#define DEFAULT_CONNECTION_ATTEMPT_DELAY_MS 250

// Represents the state of a single racing attempt
typedef enum {
  ATTEMPT_STATE_PENDING,      // Not yet started
  ATTEMPT_STATE_CONNECTING,   // ct_connection_t attempt in progress
  ATTEMPT_STATE_SUCCEEDED,    // ct_connection_t established successfully
  ATTEMPT_STATE_FAILED,       // ct_connection_t attempt failed
  ATTEMPT_STATE_CANCELED,     // Canceled due to another attempt succeeding
} ct_attempt_state_t;

// Forward declaration
typedef struct ct_racing_context_t ct_racing_context_t;

// Tracks a single connection attempt in the race
typedef struct ct_racing_attempt_t {
  ct_connection_t* connection;
  ct_candidate_node_t candidate;
  ct_attempt_state_t state;
  size_t attempt_index;
  ct_racing_context_t* context;  // Back-pointer to parent racing context
} ct_racing_attempt_t;

// Context for managing the racing process
struct ct_racing_context_t {
  // Array of all racing attempts
  ct_racing_attempt_t* attempts;
  size_t num_attempts;
  size_t next_attempt_index;  // Index of next attempt to initiate

  bool should_try_early_data; // This decision is made by the preconnection
  ct_message_t* initial_message; // not null if this racing was initiated with a send
  ct_message_context_t* initial_message_context;

  // User's original callbacks and the connection they provided
  ct_connection_callbacks_t user_callbacks;

  // Racing state
  bool race_complete;
  int winning_attempt_index;

  // Timer for staggered initiation
  uv_timer_t* stagger_timer;
  uint64_t connection_attempt_delay_ms;

  // ct_preconnection_t reference (for cleanup)
  const ct_preconnection_t* preconnection;

  // Count of attempts that have completed (success or failure)
  size_t completed_attempts;
};

/**
 * @brief Initiates connection with candidate racing.
 *
 * This function implements staggered racing as described in RFC 9623.
 * It starts connection attempts with delays, and when one succeeds,
 * cancels all other attempts.
 *
 * @param preconnection The preconnection object
 * @param connection Output connection object (will be populated with winning connection)
 * @param connection_callbacks User's connection callbacks
 * @return 0 on success, negative error code on failure
 */
int preconnection_race_with_early_data(ct_preconnection_t* preconnection,
                                       ct_connection_callbacks_t connection_callbacks,
                                       ct_message_t* initial_message,
                                       ct_message_context_t* initial_message_context);

int preconnection_race_with_send_after_ready(ct_preconnection_t* preconnection,
                                       ct_connection_callbacks_t connection_callbacks,
                                       ct_message_t* initial_message,
                                       ct_message_context_t* initial_message_context);

int preconnection_race(ct_preconnection_t* preconnection, ct_connection_callbacks_t connection_callbacks);

/**
 * @brief Frees a racing context and all associated resources.
 *
 * @param context The racing context to free
 */
void racing_context_free(ct_racing_context_t* context);

#endif // CANDIDATE_RACING_H

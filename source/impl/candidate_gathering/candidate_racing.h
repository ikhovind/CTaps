#ifndef CANDIDATE_RACING_H
#define CANDIDATE_RACING_H

#include <glib.h>
#include <uv.h>

#include "connections/connection/connection.h"
#include "connections/preconnection/preconnection.h"
#include "candidate_gathering.h"

// Default connection attempt delay in milliseconds (per Happy Eyeballs RFC 8305)
#define DEFAULT_CONNECTION_ATTEMPT_DELAY_MS 250

// Represents the state of a single racing attempt
typedef enum {
  ATTEMPT_STATE_PENDING,      // Not yet started
  ATTEMPT_STATE_CONNECTING,   // Connection attempt in progress
  ATTEMPT_STATE_SUCCEEDED,    // Connection established successfully
  ATTEMPT_STATE_FAILED,       // Connection attempt failed
  ATTEMPT_STATE_CANCELED,     // Canceled due to another attempt succeeding
} AttemptState;

// Forward declaration
typedef struct RacingContext RacingContext;

// Tracks a single connection attempt in the race
typedef struct RacingAttempt {
  Connection* connection;
  CandidateNode candidate;
  AttemptState state;
  int attempt_index;
  RacingContext* context;  // Back-pointer to parent racing context
} RacingAttempt;

// Context for managing the racing process
struct RacingContext {
  // Array of all racing attempts
  RacingAttempt* attempts;
  size_t num_attempts;
  size_t next_attempt_index;  // Index of next attempt to initiate

  // User's original callbacks and the connection they provided
  ConnectionCallbacks user_callbacks;
  Connection* user_connection;

  // Racing state
  bool race_complete;
  int winning_attempt_index;

  // Timer for staggered initiation
  uv_timer_t* stagger_timer;
  uint64_t connection_attempt_delay_ms;

  // Preconnection reference (for cleanup)
  const Preconnection* preconnection;

  // Count of attempts that have completed (success or failure)
  size_t completed_attempts;
};

/**
 * @brief Initiates connection with candidate racing.
 *
 * This function implements staggered racing as described in RFC9623.
 * It starts connection attempts with delays, and when one succeeds,
 * cancels all other attempts.
 *
 * @param preconnection The preconnection object
 * @param connection Output connection object (will be populated with winning connection)
 * @param connection_callbacks User's connection callbacks
 * @return 0 on success, negative error code on failure
 */
int preconnection_initiate_with_racing(Preconnection* preconnection,
                                       Connection* connection,
                                       ConnectionCallbacks connection_callbacks);

/**
 * @brief Frees a racing context and all associated resources.
 *
 * @param context The racing context to free
 */
void racing_context_free(RacingContext* context);

#endif // CANDIDATE_RACING_H

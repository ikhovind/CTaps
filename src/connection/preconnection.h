#ifndef CT_PRECONNECTION
#define CT_PRECONNECTION
#include "ctaps.h"


// Preconnection
/**
 * @brief Initialize a connection from preconnection configuration (internal helper).
 * @param[out] connection Connection structure to initialize
 * @param[in] preconnection Source preconnection configuration
 * @param[in] connection_callbacks Callbacks for connection events
 *
 * @return 0 on success, non-zero on error
 */
int ct_preconnection_build_user_connection(ct_connection_t* connection, const ct_preconnection_t* preconnection, ct_connection_callbacks_t connection_callbacks);
#endif

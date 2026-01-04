#ifndef CT_SECURITY_PARAMETERS_H
#define CT_SECURITY_PARAMETERS_H

#include "ctaps.h"

/**
 * @brief Deep copy security parameters.
 *
 * Creates a new security parameters object with copies of all data.
 *
 * @param source Source security parameters to copy
 * @return Newly allocated copy, or NULL on failure
 */
ct_security_parameters_t* ct_security_parameters_deep_copy(const ct_security_parameters_t* source);

#endif // CT_SECURITY_PARAMETERS_H

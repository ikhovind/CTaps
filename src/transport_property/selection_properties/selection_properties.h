#ifndef CT_SELECTION_PROPERTIES_H
#define CT_SELECTION_PROPERTIES_H
#include <ctaps.h>

/**
 * @brief Initialize selection properties with default values.
 *
 * @param[out] selection_properties Pointer to selection properties structure to initialize.
 *                                  Must be allocated by the caller.
 *
 * @see DEFAULT_SELECTION_PROPERTIES for default values
 */
void ct_selection_properties_build(ct_selection_properties_t* selection_properties);
#endif // CT_SELECTION_PROPERTIES_H

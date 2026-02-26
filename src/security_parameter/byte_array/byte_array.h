#ifndef CT_BYTE_ARRAY_H
#define CT_BYTE_ARRAY_H

#include "ctaps_internal.h"

ct_byte_array_t* ct_byte_array_copy(const ct_byte_array_t* source);

void ct_byte_array_free(ct_byte_array_t byte_array);

#endif  // CT_BYTE_ARRAY_H


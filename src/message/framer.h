#ifndef CT_MESSAGE_FRAMER_H
#define CT_MESSAGE_FRAMER_H
#include "ctaps.h"

ct_framer_impl_t* ct_framer_impl_deep_copy(const ct_framer_impl_t* source);

void ct_framer_impl_free(ct_framer_impl_t* framer);

#endif // CT_MESSAGE_FRAMER_H


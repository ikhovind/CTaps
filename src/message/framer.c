#include "logging/log.h"
#include "framer.h"
#include <stdlib.h>
#include <string.h>

ct_framer_impl_t* ct_framer_impl_deep_copy(const ct_framer_impl_t* source) {
    if (!source) {
        return NULL;
    }
    ct_framer_impl_t* copy = malloc(sizeof(ct_framer_impl_t));
    if (!copy) {
        log_error("Failed to allocate memory for framer implementation copy");
        return NULL;
    }
    memcpy(copy, source, sizeof(ct_framer_impl_t));
    return copy;
}

void ct_framer_impl_free(ct_framer_impl_t* framer) {
    if (!framer) {
        return;
    }
    free(framer);
}

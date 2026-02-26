#ifndef CT_CERTIFICATE_BUNDLES_H
#define CT_CERTIFICATE_BUNDLES_H
#include "ctaps_internal.h"

// Certificate bundles

ct_certificate_bundles_t* ct_certificate_bundles_new(void);

int ct_certificate_bundles_add_cert(ct_certificate_bundles_t* bundles, const char* cert_file_path, const char* key_file_path);

void ct_certificate_bundles_free(ct_certificate_bundles_t bundles);

int ct_certificate_bundles_deep_copy(ct_certificate_bundles_t src, ct_certificate_bundles_t* dest);

#endif // CT_CERTIFICATE_BUNDLES_H

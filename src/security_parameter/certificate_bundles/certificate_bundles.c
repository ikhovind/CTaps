#include "ctaps.h"
#include "ctaps_internal.h"
#include "logging/log.h"

ct_certificate_bundles_t* ct_certificate_bundles_new(void) {
    ct_certificate_bundles_t* bundles = malloc(sizeof(ct_certificate_bundles_t));
    if (!bundles) {
        return NULL;
    }
    memset(bundles, 0, sizeof(ct_certificate_bundles_t));
    return bundles;
}

int ct_certificate_bundles_add_cert(ct_certificate_bundles_t* bundles, const char* cert_file_path,
                                    const char* key_file_path) {
    if (!bundles || !cert_file_path || !key_file_path) {
        log_error("Cannot add certificate bundle, invalid arguments");
        log_debug("bundles: %p, cert_file_path: %p, key_file_path: %p", (void*)bundles,
                  (void*)cert_file_path, (void*)key_file_path);
        return -EINVAL;
    }
    if (bundles->num_bundles != 0) {
        log_error("More than a single bundle is not currently supported");
        return -ENOSYS;
    }
    ct_certificate_bundle_t* tmp = realloc(
        bundles->certificate_bundles, sizeof(ct_certificate_bundle_t) * (bundles->num_bundles + 1));
    if (!bundles->certificate_bundles) {
        return -ENOMEM;
    }
    bundles->certificate_bundles = tmp;
    bundles->certificate_bundles[bundles->num_bundles].certificate_file_name =
        strdup(cert_file_path);
    if (!bundles->certificate_bundles[bundles->num_bundles].certificate_file_name) {
        return -ENOMEM;
    }
    bundles->certificate_bundles[bundles->num_bundles].private_key_file_name =
        strdup(key_file_path);
    if (!bundles->certificate_bundles[bundles->num_bundles].private_key_file_name) {
        free(bundles->certificate_bundles[bundles->num_bundles].certificate_file_name);
        return -ENOMEM;
    }
    bundles->num_bundles += 1;
    return 0;
}

void ct_certificate_bundles_free(ct_certificate_bundles_t bundles) {
    for (size_t i = 0; i < bundles.num_bundles; i++) {
        free(bundles.certificate_bundles[i].certificate_file_name);
        free(bundles.certificate_bundles[i].private_key_file_name);
    }
    free(bundles.certificate_bundles);
}

int ct_certificate_bundles_deep_copy(ct_certificate_bundles_t src, ct_certificate_bundles_t* dest) {
    memset(dest, 0, sizeof(ct_certificate_bundles_t));
    size_t num_bundles = src.num_bundles;
    if (num_bundles == 0) {
        return 0;
    }
    dest->certificate_bundles = malloc(sizeof(ct_certificate_bundle_t) * num_bundles);
    if (!dest->certificate_bundles) {
        return -ENOMEM;
    }

    for (size_t i = 0; i < num_bundles; i++) {
        dest->certificate_bundles[i].certificate_file_name =
            strdup(src.certificate_bundles[i].certificate_file_name);
        if (!dest->certificate_bundles[i].certificate_file_name) {
            for (size_t j = 0; j < i; j++) {
                free(dest->certificate_bundles[j].certificate_file_name);
                free(dest->certificate_bundles[j].private_key_file_name);
            }
            return -ENOMEM;
        }
        dest->certificate_bundles[i].private_key_file_name =
            strdup(src.certificate_bundles[i].private_key_file_name);
        if (!dest->certificate_bundles[i].private_key_file_name) {
            // Free previously allocated entries
            free(dest->certificate_bundles[i].certificate_file_name);
            for (size_t j = 0; j < i; j++) {
                free(dest->certificate_bundles[j].certificate_file_name);
                free(dest->certificate_bundles[j].private_key_file_name);
            }
            return -ENOMEM;
        }
        dest->num_bundles++;
    }
    return 0;
}

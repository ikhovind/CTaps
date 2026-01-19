#include "ctaps.h"
#include "ctaps_internal.h"
#include "logging/log.h"

ct_certificate_bundles_t* ct_certificate_bundles_new(void) {
  ct_certificate_bundles_t* bundles = malloc(sizeof(ct_certificate_bundles_t));
  memset(bundles, 0, sizeof(ct_certificate_bundles_t));
  return bundles;
}

int ct_certificate_bundles_add_cert(ct_certificate_bundles_t* bundles, const char* cert_file_path, const char* key_file_path) {
  if (bundles->num_bundles != 0) {
    log_error("More than a single bundle is not currently supported");
    return -ENOSYS;
  }
  bundles->certificate_bundles = realloc(bundles->certificate_bundles, sizeof(ct_certificate_bundle_t) * (bundles->num_bundles + 1));
  if (!bundles->certificate_bundles) {
    return -ENOMEM;
  }
  bundles->certificate_bundles[bundles->num_bundles].certificate_file_name = strdup(cert_file_path);
  if (!bundles->certificate_bundles[bundles->num_bundles].certificate_file_name) {
    return -ENOMEM;
  }
  bundles->certificate_bundles[bundles->num_bundles].private_key_file_name = strdup(key_file_path);
  if (!bundles->certificate_bundles[bundles->num_bundles].private_key_file_name) {
    free(bundles->certificate_bundles[bundles->num_bundles].certificate_file_name);
    return -ENOMEM;
  }
  bundles->num_bundles += 1;
  return 0;
}

void ct_certificate_bundles_free(ct_certificate_bundles_t* bundles) {
  for (size_t i = 0; i < bundles->num_bundles; i++) {
    free(bundles->certificate_bundles[i].certificate_file_name);
    free(bundles->certificate_bundles[i].private_key_file_name);
  }
  free(bundles->certificate_bundles);
}

ct_certificate_bundles_t ct_certificate_bundles_copy_content(const ct_certificate_bundles_t* bundles) {
  ct_certificate_bundles_t copy = {0};
  size_t num_bundles = bundles->num_bundles;
  copy.certificate_bundles = malloc(sizeof(ct_certificate_bundle_t) * num_bundles);
  if (!copy.certificate_bundles) {
    return copy;
  }
  copy.num_bundles = num_bundles;

  for (size_t i = 0; i < num_bundles; i++) {
    copy.certificate_bundles[i].certificate_file_name = strdup(bundles->certificate_bundles[i].certificate_file_name);
    copy.certificate_bundles[i].private_key_file_name = strdup(bundles->certificate_bundles[i].private_key_file_name);
  }
  return copy;
}

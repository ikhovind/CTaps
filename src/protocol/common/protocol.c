#include "ctaps_internal.h"

bool ct_protocol_supports_alpn(const ct_protocol_impl_t* protocol_impl) {
  return protocol_impl->supports_alpn;
}

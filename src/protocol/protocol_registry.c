#include "ctaps_internal.h"
#include "protocol/quic/quic.h"
#include "protocol/tcp/tcp.h"
#include "protocol/udp/udp.h"

#include <stddef.h>

const ct_protocol_impl_t* const ct_supported_protocols[] = {
    &udp_protocol_interface,
    &tcp_protocol_interface,
    &quic_protocol_interface,
};

const size_t ct_num_protocols = sizeof(ct_supported_protocols) / sizeof(ct_supported_protocols[0]);

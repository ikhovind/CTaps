#pragma once
#include "ctaps.h"
#include <uv.h>

uv_udp_t* create_udp_listening_on_local(ct_local_endpoint_t* local_endpoint, uv_alloc_cb alloc_cb, uv_udp_recv_cb on_read_cb);

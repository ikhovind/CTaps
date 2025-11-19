/*
 * This file is separated from the normal util.h file to allow for
 * easier mocking in tests, since this mocking happens at link time.
 */


#ifndef PORT_UTIL_H
#define PORT_UTIL_H

#include <uv.h>
#include "ctaps.h"

int32_t get_service_port_local(const ct_local_endpoint_t* local_endpoint);

int32_t get_service_port_remote(const ct_remote_endpoint_t* remote_endpoint);

#endif //PORT_UTIL_H

/*
 * This file is separated from the normal util.h file to allow for
 * easier mocking in tests, since this mocking happens at link time.
 */


#ifndef PORT_UTIL_H
#define PORT_UTIL_H

#include <uv.h>
#include <endpoints/local/local_endpoint.h>
#include <endpoints/remote/remote_endpoint.h>

int32_t get_service_port_local(LocalEndpoint* local_endpoint);

int32_t get_service_port_remote(RemoteEndpoint* remote_endpoint);

#endif //PORT_UTIL_H

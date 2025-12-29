/*
 * This file is separated from the normal util.h file to allow for
 * easier mocking in tests, since this mocking happens at link time.
 */


#ifndef PORT_UTIL_H
#define PORT_UTIL_H

#include <uv.h>
#include "ctaps.h"

int32_t get_service_port(const char* service, int family);

#endif //PORT_UTIL_H

#include "local_endpoint.h"

void local_endpoint_with_port(LocalEndpoint* remote_endpoint, int port) {
    remote_endpoint->addr.ipv4_addr.sin_port = htons(port);
    remote_endpoint->initialized = true;
}

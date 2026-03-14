#include "socket_utils.h"
#include "connection/connection.h"
#include "endpoint/local_endpoint.h"

#include "ctaps.h"
#include "protocol/quic/quic.h"
#include <assert.h>
#include <logging/log.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>
#include <uv.h>

uv_udp_t* create_udp_listening_on_local(const ct_local_endpoint_t* local_endpoint,
                                        uv_alloc_cb alloc_cb, uv_udp_recv_cb on_read_cb) {
    bool is_ephemeral = ct_local_endpoint_get_resolved_port(local_endpoint) == 0;
    if (!is_ephemeral) {
        log_debug("Creating UDP socket for set local endpoint");
        if (ct_local_endpoint_get_address_family(local_endpoint) == AF_INET) {
            log_trace("Creating UDP socket listening on IPv4 on port %d",
                      ct_local_endpoint_get_resolved_port(local_endpoint));
        } else if (ct_local_endpoint_get_address_family(local_endpoint) == AF_INET6) {
            log_trace("Creating UDP socket listening on IPv6 on port %d",
                      ct_local_endpoint_get_resolved_port(local_endpoint));
        } else {
            log_error("Local endpoint is not of type IPv4 or IPv6");
            return NULL;
        }
    } else {
        log_debug("Local endpoint is not set, creating UDP socket for ephemeral port");
    }
    uv_udp_t* new_udp_handle = malloc(sizeof(uv_udp_t));
    if (!new_udp_handle) {
        log_error("Failed to allocate memory for UDP handle");
        return NULL;
    }

    int rc = uv_udp_init(event_loop, new_udp_handle);
    if (rc < 0) {
        log_error("Error initializing udp handle: %s", uv_strerror(rc));
        free(new_udp_handle);
        return NULL;
    }

    if (is_ephemeral) {
        log_debug("Binding UDP socket to ephemeral port");
        struct sockaddr_in ephemeral_addr = {0};
        uv_ip4_addr("0.0.0.0", 0, &ephemeral_addr);
        rc = uv_udp_bind(new_udp_handle, (const struct sockaddr*)&ephemeral_addr, 0);
    } else {
        log_debug("Binding UDP socket to specified local endpoint");
        rc = uv_udp_bind(
            new_udp_handle,
            (const struct sockaddr*)ct_local_endpoint_get_resolved_address(local_endpoint), 0);
    }
    if (rc < 0) {
        log_error("Problem with auto-binding: %s", uv_strerror(rc));
        free(new_udp_handle);
        return NULL;
    }

    // TODO - filter on remote endpoint
    rc = uv_udp_recv_start(new_udp_handle, alloc_cb, on_read_cb);
    if (rc < 0) {
        log_error("Error starting UDP receive: %s", uv_strerror(rc));
        free(new_udp_handle);
        return NULL;
    }
    return new_udp_handle;
}

static void poll_recv_cb(uv_poll_t* handle, int status, int events) {
    if (status < 0 || !(events & UV_READABLE))
        return;

    ct_udp_poll_handle_t* wrapper = (ct_udp_poll_handle_t*)handle;

    uint8_t buf[MAX_QUIC_PACKET_SIZE];
    struct sockaddr_storage addr_from = {0};
    char cmsg_buf[CMSG_SPACE(sizeof(struct in6_pktinfo))] = {0};

    struct iovec iov = {.iov_base = buf, .iov_len = sizeof(buf)};
    struct msghdr msg = {
        .msg_name = &addr_from,
        .msg_namelen = sizeof(addr_from),
        .msg_iov = &iov,
        .msg_iovlen = 1,
        .msg_control = cmsg_buf,
        .msg_controllen = sizeof(cmsg_buf),
    };

    // uv_poll_t is level-triggered so do not need to loop here
    ssize_t nread = recvmsg(wrapper->fd, &msg, 0);
    if (nread < 0) {
        log_error("recvmsg failed: %s", strerror(errno));
        return;
    }

    uint16_t local_port = wrapper->local_port;
    if (local_port == 0) {
        // If local_port is not set, get it from the socket
        struct sockaddr_storage bound = {0};
        socklen_t len = sizeof(bound);
        getsockname(wrapper->fd, (struct sockaddr*)&bound, &len);
        local_port = (bound.ss_family == AF_INET)
                         ? ntohs(((struct sockaddr_in*)&bound)->sin_port)
                         : ntohs(((struct sockaddr_in6*)&bound)->sin6_port);
    }
    struct sockaddr_storage addr_to = {0};
    for (struct cmsghdr* cm = CMSG_FIRSTHDR(&msg); cm; cm = CMSG_NXTHDR(&msg, cm)) {
        if (cm->cmsg_level == IPPROTO_IP && cm->cmsg_type == IP_PKTINFO) {
            struct in_pktinfo* pktinfo = (struct in_pktinfo*)CMSG_DATA(cm);
            struct sockaddr_in* dst = (struct sockaddr_in*)&addr_to;
            dst->sin_family = AF_INET;
            dst->sin_addr = pktinfo->ipi_addr;
            dst->sin_port = local_port;
        } else if (cm->cmsg_level == IPPROTO_IPV6 && cm->cmsg_type == IPV6_PKTINFO) {
            struct in6_pktinfo* pktinfo = (struct in6_pktinfo*)CMSG_DATA(cm);
            struct sockaddr_in6* dst = (struct sockaddr_in6*)&addr_to;
            dst->sin6_family = AF_INET6;
            dst->sin6_addr = pktinfo->ipi6_addr;
            dst->sin6_port = local_port;
        }
    }

    ct_socket_manager_t* socket_manager = (ct_socket_manager_t*)handle->data;
    on_quic_poll_read(socket_manager, buf, nread, (struct sockaddr*)&addr_from,
                      (struct sockaddr*)&addr_to);
}

// We have to have direct access to the FD for recvmsg to get the destination address
// This is needed because QUIC needs to know the destionation address for migration
ct_udp_poll_handle_t* create_udp_poll_on_local(const ct_local_endpoint_t* local_endpoint) {
    ct_udp_poll_handle_t* wrapper = malloc(sizeof(ct_udp_poll_handle_t));
    if (!wrapper) {
        log_error("Failed to allocate UDP poll handle wrapper");
        return NULL;
    }
    memset(wrapper, 0, sizeof(ct_udp_poll_handle_t));

    int family = ct_local_endpoint_get_address_family(local_endpoint);
    int fd = socket(family, SOCK_DGRAM | SOCK_NONBLOCK, 0);
    if (fd < 0) {
        log_error("Failed to create UDP socket: %s", strerror(errno));
        free(wrapper);
        return NULL;
    }

    // TODO - is this the correct way of detecting ephemeral?
    bool is_ephemeral = ct_local_endpoint_get_resolved_port(local_endpoint) == 0;
    int rc;

    if (is_ephemeral) {
        if (family == AF_INET6) {
            log_debug("Binding to ephemeral UDP socket on IPv6");
            struct sockaddr_in6 ephemeral_addr = {0};
            uv_ip6_addr("::", 0, &ephemeral_addr);
            rc = bind(fd, (const struct sockaddr*)&ephemeral_addr, sizeof(ephemeral_addr));
        } else {
            log_debug("Binding to ephemeral UDP socket on IPv4");
            struct sockaddr_in ephemeral_addr = {0};
            uv_ip4_addr("0.0.0.0", 0, &ephemeral_addr);
            rc = bind(fd, (const struct sockaddr*)&ephemeral_addr, sizeof(ephemeral_addr));
        }
    } else {
        log_debug("Binding UDP socket to specified local endpoint");
        const struct sockaddr_storage* addr = ct_local_endpoint_get_resolved_address(local_endpoint);
        socklen_t len =
            (family == AF_INET6) ? sizeof(struct sockaddr_in6) : sizeof(struct sockaddr_in);
        rc = bind(fd, (const struct sockaddr*)addr, len);
    }
    if (rc < 0) {
        log_error("Failed to bind UDP socket: %s", strerror(errno));
        close(fd);
        free(wrapper);
        return NULL;
    }

    // Enable pktinfo so recvmsg gives us the actual destination address
    int one = 1;
    int sockopt = (family == AF_INET6) ? IPV6_RECVPKTINFO : IP_PKTINFO;
    int level = (family == AF_INET6) ? IPPROTO_IPV6 : IPPROTO_IP;
    if (setsockopt(fd, level, sockopt, &one, sizeof(one)) < 0) {
        log_error("Failed to set IP_RECVPKTINFO: %s", strerror(errno));
        close(fd);
        free(wrapper);
        return NULL;
    }

    wrapper->fd = fd;

    rc = uv_poll_init(event_loop, &wrapper->poll, fd);
    if (rc < 0) {
        log_error("Failed to init uv_poll_t: %s", uv_strerror(rc));
        close(fd);
        free(wrapper);
        return NULL;
    }

    rc = uv_poll_start(&wrapper->poll, UV_READABLE, poll_recv_cb);
    if (rc < 0) {
        log_error("Failed to start uv_poll_t: %s", uv_strerror(rc));
        close(fd);
        free(wrapper);
        return NULL;
    }

    return wrapper;
}

uv_udp_t* create_udp_listening_on_ephemeral(uv_alloc_cb alloc_cb, uv_udp_recv_cb on_read_cb) {
    return create_udp_listening_on_local(NULL, alloc_cb, on_read_cb);
}

int get_sockaddr_from_handle(const uv_handle_t* handle, struct sockaddr_storage* addr) {
    int namelen = sizeof(struct sockaddr_storage);
    assert(handle->type == UV_UDP || handle->type == UV_TCP);

    if (handle->type == UV_UDP) {
        log_trace("Resolving local endpoint from UDP handle");
        uv_udp_t* udp_handle = (uv_udp_t*)handle;
        int rc = uv_udp_getsockname(udp_handle, (struct sockaddr*)addr, &namelen);
        if (rc < 0) {
            log_error("Failed to get UDP socket name: %s", uv_strerror(rc));
            return rc;
        }
    } else {
        log_trace("Resolving local endpoint from TCP handle");
        uv_tcp_t* tcp_handle = (uv_tcp_t*)handle;
        int rc = uv_tcp_getsockname(tcp_handle, (struct sockaddr*)addr, &namelen);
        if (rc < 0) {
            log_error("Failed to get TCP socket name: %s", uv_strerror(rc));
            return rc;
        }
    }
    return 0;
}

int resolve_local_endpoint_from_poll(ct_udp_poll_handle_t* handle, ct_connection_t* connection) {

    struct sockaddr_storage addr = {0};
    socklen_t len = sizeof(addr);
    int rc = getsockname(handle->fd, (struct sockaddr*)&addr, &len);
    if (rc < 0) {
        log_error("getsockname failed in resolve_local_endpoint_from_poll: %s", strerror(errno));
        return -errno;
    }

    ct_local_endpoint_t local_endpoint = {0};
    rc = ct_local_endpoint_from_sockaddr(&local_endpoint, &addr);
    if (rc != 0) {
        log_error("Failed to create local endpoint from sockaddr: %d", rc);
        return rc;
    }

    if (!ct_address_is_unspecified(&addr)) {
        log_debug("Setting active local endpoint on connection to resolved local endpoint");
        rc = ct_connection_set_active_local_endpoint(connection, &local_endpoint);
        if (rc != 0) {
            log_error("Failed to set active local endpoint on connection: %d", rc);
            return rc;
        }
    } else {
        log_debug("Local address is wildcard, not setting active local endpoint on connection");
    }

    log_debug("Setting all local port on connection to %d",
              ct_local_endpoint_get_resolved_port(&local_endpoint));
    ct_connection_set_all_local_port(connection, ct_local_endpoint_get_resolved_port(&local_endpoint));

    return 0;
}

int resolve_local_endpoint_from_handle(uv_handle_t* handle, ct_connection_t* connection) {
    ct_local_endpoint_t local_endpoint = {0};
    struct sockaddr_storage addr = {0};
    ;
    int rc = get_sockaddr_from_handle(handle, &addr);
    if (rc != 0) {
        log_error("Failed to get socket address from handle: %d", rc);
        return rc;
    }

    rc = ct_local_endpoint_from_sockaddr(&local_endpoint, &addr);
    if (rc != 0) {
        log_error("Failed to create local endpoint from sockaddr: %d", rc);
        return rc;
    }

    if (!ct_address_is_unspecified(&addr)) {
        log_debug("Setting active local endpoint on connection to resolved local endpoint");
        rc = ct_connection_set_active_local_endpoint(connection, &local_endpoint);
        if (rc != 0) {
            log_error("Failed to set active local endpoint on connection: %d", rc);
            return rc;
        }
    } else {
        log_debug("Local address is wildcard, not setting active local endpoint on connection");
    }

    log_debug("Setting all local port on connection to %d",
              ct_local_endpoint_get_resolved_port(&local_endpoint));

    ct_connection_set_all_local_port(connection, ct_local_endpoint_get_resolved_port(&local_endpoint));
    return 0;
}

bool ct_address_is_unspecified(const struct sockaddr_storage* addr) {
    log_trace("Checking if address is wildcard");
    if (addr->ss_family == AF_INET) {
        struct sockaddr_in* in_addr = (struct sockaddr_in*)addr;
        return in_addr->sin_addr.s_addr == INADDR_ANY;
    } else if (addr->ss_family == AF_INET6) {
        struct sockaddr_in6* in6_addr = (struct sockaddr_in6*)addr;
        return IN6_IS_ADDR_UNSPECIFIED(&in6_addr->sin6_addr);
    }
    return addr->ss_family == AF_UNSPEC;
}

void ct_get_addr_string(const struct sockaddr_storage* addr, char* buffer, size_t buffer_len,
                        uint16_t* port) {
    if (buffer_len != INET6_ADDRSTRLEN) {
        log_error("Buffer length for address string is incorrect, expected %d but got %zu",
                  INET6_ADDRSTRLEN, buffer_len);
        return;
    }
    if (addr->ss_family == AF_INET) {
        struct sockaddr_in* ipv4_addr = (struct sockaddr_in*)addr;
        inet_ntop(AF_INET, &ipv4_addr->sin_addr, buffer, INET6_ADDRSTRLEN);
        *port = ntohs(ipv4_addr->sin_port);
    } else if (addr->ss_family == AF_INET6) {
        struct sockaddr_in6* ipv6_addr = (struct sockaddr_in6*)addr;
        inet_ntop(AF_INET6, &ipv6_addr->sin6_addr, buffer, INET6_ADDRSTRLEN);
        *port = ntohs(ipv6_addr->sin6_port);
    } else {
        snprintf(buffer, INET6_ADDRSTRLEN, "<unknown family %d>", addr->ss_family);
        *port = 0;
    }
}

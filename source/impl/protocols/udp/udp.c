#include "udp.h"

#include <unistd.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <fcntl.h>

#include "protocols/registry/protocol_registry.h"

#define BUFFER_SIZE 1024
#define MAX_EVENTS 1024

struct epoll_event event;
struct epoll_event events[MAX_EVENTS];
char buffer[BUFFER_SIZE];
int udp_fd;

int udp_init() {
    printf("udp_init()\n");

    udp_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_fd < 0) {
        perror( "socket failed" );
        return 1;
    }

    int flags = fcntl(udp_fd, F_GETFL, 0);
    if (flags == -1) {
        perror("fcntl F_GETFL failed");
        return 1;
    }


    if (fcntl(udp_fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        perror("fcntl F_SETFL failed");
        return 1;
    }

    struct sockaddr_in serveraddr;
    memset( &serveraddr, 0, sizeof(serveraddr) );
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_port = htons( 50037 );
    serveraddr.sin_addr.s_addr = htonl( INADDR_ANY );

    if ( bind(udp_fd, (struct sockaddr *)&serveraddr, sizeof(serveraddr)) < 0 ) {
        perror( "bind failed" );
        return 1;
    }
}

int udp_close() {
    close(udp_fd);
}

void register_udp_support() {
    register_protocol(&udp_protocol_interface);
}

int udp_send(struct Connection* connection, Message* message) {
    return 0;
}

int udp_receive(struct Connection* connection, Message* message) {
    return 0;
}

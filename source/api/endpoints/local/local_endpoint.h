#ifndef LOCAL_ENDPOINT_H
#define LOCAL_ENDPOINT_H
#include <arpa/inet.h>  // or <winsock2.h> for Windows
#include <stdbool.h>

/* TODO:
 *   - with service?
 *   - multicast
 *   - with protocol
 */

typedef struct {
  uint16_t port;
  char* interface_name;
  char* service;
  union {
    struct sockaddr_storage address;
  } data;
} ct_local_endpoint_t;

void ct_local_endpoint_build(ct_local_endpoint_t* local_endpoint);
void ct_local_endpoint_with_port(ct_local_endpoint_t* local_endpoint, int port);
int ct_local_endpoint_with_interface(ct_local_endpoint_t* local_endpoint, const char* interface_name);
int ct_local_endpoint_with_service(ct_local_endpoint_t* local_endpoint, char* service);
int ct_local_endpoint_resolve(const ct_local_endpoint_t* local_endpoint, ct_local_endpoint_t** out_list, size_t* out_count);
void ct_free_local_endpoint(ct_local_endpoint_t* local_endpoint);
void ct_free_local_endpoint_strings(ct_local_endpoint_t* local_endpoint);
ct_local_endpoint_t ct_local_endpoint_copy_content(const ct_local_endpoint_t* local_endpoint);
ct_local_endpoint_t* local_endpoint_copy(const ct_local_endpoint_t* local_endpoint);

#endif  // LOCAL_ENDPOINT_H

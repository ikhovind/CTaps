#include <netdb.h>
#include <arpa/inet.h>
#include <gtest/gtest.h>


extern "C" {
  #include <uv.h>
  #include "fff.h"
  #include "ctaps.h"


// --- FFF Fakes ---
DEFINE_FFF_GLOBALS;
FAKE_VALUE_FUNC(int, faked_uv_getaddrinfo, uv_loop_t*, uv_getaddrinfo_t*, uv_getaddrinfo_cb, const char*, const char*, const struct addrinfo*);
FAKE_VALUE_FUNC(int32_t, get_service_port, const char*, int);
}

extern "C" int __wrap_uv_getaddrinfo(uv_loop_t* loop, uv_getaddrinfo_t* req,
                                     uv_getaddrinfo_cb cb, const char* hostname,
                                     const char* service, const struct addrinfo* hints) {
  // Call the fff fake, which allows us to track calls and set return values.
  return faked_uv_getaddrinfo(loop, req, cb, hostname, service, hints);
}

extern "C" int __wrap_uv_freeaddrinfo(struct addrinfo* addrinfo) {
  return 0;
}

extern "C" int32_t __wrap_get_service_port(const char* service, int family) {
  // Call the fff fake function, which tracks calls and uses the return value we set
  return get_service_port(service, family);
}

// --- Test Fixture ---
class RemoteEndpointResolveTest : public ::testing::Test {
protected:
  void SetUp() override {
    FFF_RESET_HISTORY();
  }
};

// --- Custom Fake Implementation for uv_getaddrinfo ---
// We need to build a fake addrinfo linked list to return to the function under test.
struct sockaddr_in fake_ipv4_addr;
struct sockaddr_in6 fake_ipv6_addr;
struct addrinfo fake_addr_list[2];

// This function will be the implementation of our uv_getaddrinfo fake.
int custom_uv_getaddrinfo(uv_loop_t* loop, uv_getaddrinfo_t* req, uv_getaddrinfo_cb cb, 
                          const char* hostname, const char* service, const struct addrinfo* hints) {
    // 1. Set up the fake IPv4 address info
    fake_ipv4_addr.sin_family = AF_INET;
    inet_pton(AF_INET, "93.184.216.34", &fake_ipv4_addr.sin_addr);
    fake_addr_list[0].ai_family = AF_INET;
    fake_addr_list[0].ai_addr = (struct sockaddr*)&fake_ipv4_addr;
    fake_addr_list[0].ai_addrlen = sizeof(fake_ipv4_addr);
    fake_addr_list[0].ai_next = &fake_addr_list[1]; // Link to the next item

    // 2. Set up the fake IPv6 address info
    fake_ipv6_addr.sin6_family = AF_INET6;
    inet_pton(AF_INET6, "2606:2800:220:1:248:1893:25c8:1946", &fake_ipv6_addr.sin6_addr);
    fake_addr_list[1].ai_family = AF_INET6;
    fake_addr_list[1].ai_addr = (struct sockaddr*)&fake_ipv6_addr;
    fake_addr_list[1].ai_addrlen = sizeof(fake_ipv6_addr);
    fake_addr_list[1].ai_next = nullptr; // End of the list

    // 3. Point the request's addrinfo to our fake list
    req->addrinfo = &fake_addr_list[0];
    
    return 0; // Simulate success
}


TEST_F(RemoteEndpointResolveTest, ResolvesHostnameAndAppliesServicePort) {
  // --- ARRANGE ---
  // Set our custom function as the implementation for the uv_getaddrinfo fake
  faked_uv_getaddrinfo_fake.custom_fake = custom_uv_getaddrinfo;

  // Set the fake service lookup to return port 443 (HTTPS)
  get_service_port_fake.return_val = 443;
  
  // Create a remote endpoint specifying a hostname and a service
  ct_remote_endpoint_t endpoint_to_resolve;
  ct_remote_endpoint_build(&endpoint_to_resolve);
  ct_remote_endpoint_with_hostname(&endpoint_to_resolve, "example.com");
  ct_remote_endpoint_with_service(&endpoint_to_resolve, "https");

  // Output variables
  ct_remote_endpoint_t* resolved_list = nullptr;
  size_t resolved_count = 0;

  // --- ACT ---
  // Call the function we want to test
  int result = ct_remote_endpoint_resolve(&endpoint_to_resolve, &resolved_list, &resolved_count);

  // --- ASSERT ---
  // 1. Check that the function succeeded and our fakes were called
  ASSERT_EQ(result, 0);
  ASSERT_EQ(faked_uv_getaddrinfo_fake.call_count, 1);
  ASSERT_EQ(get_service_port_fake.call_count, 1);
  
  // 2. Verify that the output list contains the two endpoints we created
  ASSERT_EQ(resolved_count, 2);
  ASSERT_NE(resolved_list, nullptr);

  // 3. Check the first resolved endpoint (IPv4)
  ct_remote_endpoint_t ipv4_endpoint = resolved_list[0];
  struct sockaddr_in* ipv4_addr = (struct sockaddr_in*)&ipv4_endpoint.data.resolved_address;
  EXPECT_EQ(ipv4_addr->sin_family, AF_INET);
  // Verify the port was correctly set from our fake service lookup
  EXPECT_EQ(ntohs(ipv4_addr->sin_port), 443); 

  // 4. Check the second resolved endpoint (IPv6)
  ct_remote_endpoint_t ipv6_endpoint = resolved_list[1];
  struct sockaddr_in6* ipv6_addr = (struct sockaddr_in6*)&ipv6_endpoint.data.resolved_address;
  EXPECT_EQ(ipv6_addr->sin6_family, AF_INET6);
  // Verify the port was also set here
  EXPECT_EQ(ntohs(ipv6_addr->sin6_port), 443);

  // Clean up the allocated memory
  free(resolved_list);
}

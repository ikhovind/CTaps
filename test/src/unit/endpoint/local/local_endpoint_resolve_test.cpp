#include <gtest/gtest.h>
#include <arpa/inet.h>
#include <netinet/in.h>

// Group C headers and fakes that need C linkage together
extern "C" {
  #include "ctaps.h"
  #include "ctaps_internal.h"
  #include "endpoint/local_endpoint.h"
  #include "endpoint/util.h"
  #include <logging/log.h>
  #include "fff.h"
  DEFINE_FFF_GLOBALS;
  FAKE_VOID_FUNC(__wrap_ct_get_interface_addresses, const char*, int*, struct sockaddr_storage*);
  FAKE_VALUE_FUNC(int32_t, __wrap_ct_get_service_port, const char*, int);
}

// --- Test Fixture ---
class LocalEndpointResolveTest : public ::testing::Test {
protected:
  void SetUp() override {
    // Reset all mock data before each test
    FFF_RESET_HISTORY();
    RESET_FAKE(__wrap_ct_get_interface_addresses);
    RESET_FAKE(__wrap_ct_get_service_port);
  }
};


// --- Custom Fake for when an interface IS found ---
void custom_get_interface_addresses_success(const char* interface_name, int* num_found, struct sockaddr_storage* found_addrs) {
  // Simulate finding one IPv4 interface
  *num_found = 1;

  // Create a fake IPv4 address (192.168.1.101) to "return"
  struct sockaddr_in* ipv4_addr = (struct sockaddr_in*)&found_addrs[0];
  ipv4_addr->sin_family = AF_INET;
  inet_pton(AF_INET, "192.168.1.101", &ipv4_addr->sin_addr);
}

void custom_get_two_interface_addresses_success(const char* interface_name, int* num_found, struct sockaddr_storage* found_addrs) {
  *num_found = 2;

  struct sockaddr_in* ipv4_addr = (struct sockaddr_in*)&found_addrs[0];
  ipv4_addr->sin_family = AF_INET;
  inet_pton(AF_INET, "192.168.1.101", &ipv4_addr->sin_addr);

  struct sockaddr_in* ipv4_addr2 = (struct sockaddr_in*)&found_addrs[1];
  ipv4_addr2->sin_family = AF_INET;
  inet_pton(AF_INET, "192.168.1.201", &ipv4_addr2->sin_addr);
}

// --- Custom Fake for when NO interface is found ---
void custom_get_interface_addresses_fail(const char* interface_name, int* num_found, struct sockaddr_storage* found_addrs) {
  // Simulate finding zero interfaces
  *num_found = 0;
}


TEST_F(LocalEndpointResolveTest, usesInterfaceAddress_whenInterfaceIsSpecified) {
  // --- ARRANGE ---
  __wrap_ct_get_interface_addresses_fake.custom_fake = custom_get_interface_addresses_success;
  __wrap_ct_get_service_port_fake.return_val = 8080; // Simulate resolving "http-alt" to 8080

  ct_local_endpoint_t* input_endpoint = ct_local_endpoint_new();
  ASSERT_NE(input_endpoint, nullptr);
  ct_local_endpoint_with_service(input_endpoint, "http-alt"); // Ensure we take the service path
  ct_local_endpoint_with_interface(input_endpoint, "eth0"); // Provide an interface name

  // --- ACT ---
  ct_local_endpoint_t* out_list = nullptr;
  size_t num_found = 0;
  out_list = ct_local_endpoint_resolve(input_endpoint, &num_found);
  ASSERT_NE(out_list, nullptr);
  ct_local_endpoint_t endpoint = out_list[0];

  // --- ASSERT ---
  ASSERT_EQ(num_found, 1);
  ASSERT_EQ(__wrap_ct_get_interface_addresses_fake.call_count, 1);
  ASSERT_EQ(__wrap_ct_get_service_port_fake.call_count, 1);

  ASSERT_EQ(endpoint.resolved_address.ss_family, AF_INET);

  struct sockaddr_in* final_addr = (struct sockaddr_in*)&endpoint.resolved_address;

  EXPECT_EQ(ntohs(final_addr->sin_port), 8080);

  // Check that the IP from get_interface_addresses was used
  char ip_str[INET_ADDRSTRLEN];
  inet_ntop(AF_INET, &final_addr->sin_addr, ip_str, sizeof(ip_str));
  EXPECT_STREQ(ip_str, "192.168.1.101");

  // Cleanup
  ct_local_endpoints_free(out_list, num_found);
  ct_local_endpoint_free(input_endpoint);
}

TEST_F(LocalEndpointResolveTest, DefaultsToAnyAddress_WhenNoInterfaceIsFound) {
  // --- ARRANGE ---
  // 1. Set the behavior of our mocks
  __wrap_ct_get_interface_addresses_fake.custom_fake = custom_get_interface_addresses_success;
  // We are not calling `with_service`, so get_service_port_local should not be called.

  // 2. Prepare the input
  ct_local_endpoint_t* input_endpoint = ct_local_endpoint_new();
  ASSERT_NE(input_endpoint, nullptr);
  ct_local_endpoint_with_port(input_endpoint, 9090); // Set port directly

  // --- ACT ---
  ct_local_endpoint_t* out_list = nullptr;
  size_t num_found = 0;
  out_list = ct_local_endpoint_resolve(input_endpoint, &num_found);
  ct_local_endpoint_t endpoint = out_list[0];

  // --- ASSERT ---
  ASSERT_EQ(__wrap_ct_get_interface_addresses_fake.call_count, 1);
  ASSERT_STREQ(__wrap_ct_get_interface_addresses_fake.arg0_val, "any");
  ASSERT_EQ(__wrap_ct_get_service_port_fake.call_count, 0); // Verify it was NOT called

  ASSERT_EQ(endpoint.resolved_address.ss_family, AF_INET);

  struct sockaddr_in* final_addr = (struct sockaddr_in*)&endpoint.resolved_address;

  EXPECT_EQ(ntohs(final_addr->sin_port), 9090);

  char ip_str[INET_ADDRSTRLEN];
  inet_ntop(AF_INET, &final_addr->sin_addr, ip_str, sizeof(ip_str));
  EXPECT_STREQ(ip_str, "192.168.1.101");
  ct_local_endpoints_free(out_list, num_found);
  ct_local_endpoint_free(input_endpoint);
}

TEST_F(LocalEndpointResolveTest, resolvesEphemeralLocalEndpoint) {
  // --- ARRANGE ---
  __wrap_ct_get_interface_addresses_fake.custom_fake = custom_get_two_interface_addresses_success;

  ct_local_endpoint_t* input_endpoint = ct_local_endpoint_new();

  // --- ACT ---
  ct_local_endpoint_t* out_list = nullptr;
  size_t num_found = 0;
  out_list = ct_local_endpoint_resolve(input_endpoint, &num_found);
  ct_local_endpoint_t* endpoint = &out_list[0];
  ct_local_endpoint_t* endpoint2 = &out_list[1];

  // --- ASSERT ---
  ASSERT_EQ(num_found, 2);
  ASSERT_EQ(__wrap_ct_get_interface_addresses_fake.call_count, 1);
  ASSERT_EQ(__wrap_ct_get_service_port_fake.call_count, 0);

  ASSERT_EQ(endpoint->resolved_address.ss_family, AF_INET);

  struct sockaddr_in* final_addr = (struct sockaddr_in*)&endpoint->resolved_address;
  char ip_str[INET_ADDRSTRLEN] = {0};
  inet_ntop(AF_INET, &final_addr->sin_addr, ip_str, sizeof(ip_str));
  EXPECT_STREQ(ip_str, "192.168.1.101");

  struct sockaddr_in* final_addr2 = (struct sockaddr_in*)&endpoint2->resolved_address;
  inet_ntop(AF_INET, &final_addr2->sin_addr, ip_str, sizeof(ip_str));
  EXPECT_STREQ(ip_str, "192.168.1.201");

  // Cleanup
  ct_local_endpoints_free(out_list, num_found);
  ct_local_endpoint_free(input_endpoint);
}

TEST_F(LocalEndpointResolveTest, resolveHandlesNullCounter) {
  // --- ARRANGE ---
  ct_local_endpoint_t* input_endpoint = ct_local_endpoint_new();

  // --- ACT ---
  EXPECT_DEATH(ct_local_endpoint_resolve(input_endpoint, NULL), "");
  ct_local_endpoint_free(input_endpoint);
}

TEST_F(LocalEndpointResolveTest, resolveHandlesNullEndpoint) {
  // --- ARRANGE ---
  size_t dummy_count = 1234;

  // --- ACT ---
  EXPECT_DEATH(ct_local_endpoint_resolve(NULL, &dummy_count), "");
}

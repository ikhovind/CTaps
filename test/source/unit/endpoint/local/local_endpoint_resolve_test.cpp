#include <gtest/gtest.h>
#include <arpa/inet.h>
#include <netinet/in.h>

// Group C headers and fakes that need C linkage together
extern "C" {
  #include "endpoints/local/local_endpoint.h"
  #include <logging/log.h>
  #include "fff.h"
  DEFINE_FFF_GLOBALS;
  FAKE_VOID_FUNC(get_interface_addresses, const char*, int*, struct sockaddr_storage*);
  FAKE_VALUE_FUNC(uint16_t, get_service_port_local, LocalEndpoint*);
}

// --- Test Fixture ---
class LocalEndpointResolveTest : public ::testing::Test {
protected:
  void SetUp() override {
    // Reset all mock data before each test
    FFF_RESET_HISTORY();
    RESET_FAKE(get_interface_addresses);
    RESET_FAKE(get_service_port_local);
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

// --- Custom Fake for when NO interface is found ---
void custom_get_interface_addresses_fail(const char* interface_name, int* num_found, struct sockaddr_storage* found_addrs) {
  // Simulate finding zero interfaces
  *num_found = 0;
}


TEST_F(LocalEndpointResolveTest, UsesInterfaceAddress_whenInterfaceIsSpecified) {
  // --- ARRANGE ---
  get_interface_addresses_fake.custom_fake = custom_get_interface_addresses_success;
  get_service_port_local_fake.return_val = 8080; // Simulate resolving "http-alt" to 8080

  LocalEndpoint input_endpoint;
  local_endpoint_build(&input_endpoint);
  local_endpoint_with_service(&input_endpoint, "http-alt"); // Ensure we take the service path
  local_endpoint_with_interface(&input_endpoint, "eth0"); // Provide an interface name

  // --- ACT ---
  LocalEndpoint* out_list = nullptr;
  size_t num_found = 0;
  int result = local_endpoint_resolve(&input_endpoint, &out_list, &num_found);
  LocalEndpoint endpoint = out_list[0];

  // --- ASSERT ---
  ASSERT_EQ(num_found, 1);
  ASSERT_EQ(result, 0);
  ASSERT_EQ(get_interface_addresses_fake.call_count, 1);
  ASSERT_EQ(get_service_port_local_fake.call_count, 1);
  
  ASSERT_EQ(endpoint.data.address.ss_family, AF_INET);

  struct sockaddr_in* final_addr = (struct sockaddr_in*)&endpoint.data.address;

  EXPECT_EQ(ntohs(final_addr->sin_port), 8080);

  // Check that the IP from get_interface_addresses was used
  char ip_str[INET_ADDRSTRLEN];
  inet_ntop(AF_INET, &final_addr->sin_addr, ip_str, sizeof(ip_str));
  EXPECT_STREQ(ip_str, "192.168.1.101");

  // Cleanup
  free(endpoint.interface_name);
  free(endpoint.service);
  free(out_list);
}

TEST_F(LocalEndpointResolveTest, DefaultsToAnyAddress_WhenNoInterfaceIsFound) {
  // --- ARRANGE ---
  // 1. Set the behavior of our mocks
  get_interface_addresses_fake.custom_fake = custom_get_interface_addresses_success;
  // We are not calling `with_service`, so get_service_port_local should not be called.

  // 2. Prepare the input
  LocalEndpoint input_endpoint;
  local_endpoint_build(&input_endpoint);
  local_endpoint_with_port(&input_endpoint, 9090); // Set port directly

  // --- ACT ---
  LocalEndpoint* out_list = nullptr;
  size_t num_found = 0;
  int result = local_endpoint_resolve(&input_endpoint, &out_list, &num_found);
  LocalEndpoint endpoint = out_list[0];

  // --- ASSERT ---
  // 1. Verify behavior
  ASSERT_EQ(result, 0);
  ASSERT_EQ(get_interface_addresses_fake.call_count, 1);
  ASSERT_STREQ(get_interface_addresses_fake.arg0_val, "any");
  ASSERT_EQ(get_service_port_local_fake.call_count, 0); // Verify it was NOT called

  // 2. Inspect the modified LocalEndpoint struct
  ASSERT_EQ(endpoint.data.address.ss_family, AF_INET);

  struct sockaddr_in* final_addr = (struct sockaddr_in*)&endpoint.data.address;

  // Check that the port was correctly applied
  EXPECT_EQ(ntohs(final_addr->sin_port), 9090);
  
  // Check that the IP defaulted to 0.0.0.0 (INADDR_ANY)
  char ip_str[INET_ADDRSTRLEN];
  inet_ntop(AF_INET, &final_addr->sin_addr, ip_str, sizeof(ip_str));
  EXPECT_STREQ(ip_str, "192.168.1.101");
  free(out_list);
}
#include "gtest/gtest.h"
#include <arpa/inet.h>
#include <netinet/in.h>

extern "C" {
  #include "fff.h"
  #include "ctaps.h"
  #include "ctaps_internal.h"
  #include "endpoint/remote_endpoint.h"
  #include "candidate_gathering/candidate_gathering.h"
}

DEFINE_FFF_GLOBALS;

// The two external calls made by ct_remote_endpoint_resolve that we want to intercept.
// ct_remote_endpoint_resolve_cb is defined in candidate_gathering.c — wrap it.
// perform_dns_lookup is defined in the DNS module — wrap it.
FAKE_VOID_FUNC(faked_ct_remote_endpoint_resolve_cb, ct_remote_endpoint_t*, size_t, ct_remote_resolve_call_context_t*);
FAKE_VOID_FUNC(faked_perform_dns_lookup, const char*, const char*, ct_remote_resolve_call_context_t*);

extern "C" void __wrap_ct_remote_endpoint_resolve_cb(ct_remote_endpoint_t* ep, size_t count, ct_remote_resolve_call_context_t* ctx) {
    faked_ct_remote_endpoint_resolve_cb(ep, count, ctx);
}

extern "C" void __wrap_perform_dns_lookup(const char* hostname, const char* service, ct_remote_resolve_call_context_t* ctx) {
    faked_perform_dns_lookup(hostname, service, ctx);
}

class RemoteEndpointResolveTest : public ::testing::Test {
protected:
    ct_remote_endpoint_t* remote_endpoint = nullptr;
    // A minimal context — the fakes don't dereference parent_node or gather_context,
    // so nulls are fine here.
    ct_remote_resolve_call_context_t context = {};

    void SetUp() override {
        FFF_RESET_HISTORY();
        RESET_FAKE(faked_ct_remote_endpoint_resolve_cb);
        RESET_FAKE(faked_perform_dns_lookup);
        remote_endpoint = ct_remote_endpoint_new();
        ASSERT_NE(remote_endpoint, nullptr);
    }

    void TearDown() override {
        ct_remote_endpoint_free(remote_endpoint);
    }
};

// --- IPv4 ---

TEST_F(RemoteEndpointResolveTest, IPv4CallsResolveCbWithOneResult) {
    ct_remote_endpoint_with_ipv4(remote_endpoint, inet_addr("1.2.3.4"));

    int rc = ct_remote_endpoint_resolve(remote_endpoint, &context);

    EXPECT_EQ(rc, 0);
    EXPECT_EQ(faked_ct_remote_endpoint_resolve_cb_fake.call_count, 1);
    EXPECT_EQ(faked_perform_dns_lookup_fake.call_count, 0);
    EXPECT_EQ(faked_ct_remote_endpoint_resolve_cb_fake.arg1_val, 1u);
    EXPECT_EQ(faked_ct_remote_endpoint_resolve_cb_fake.arg2_val, &context);
}

TEST_F(RemoteEndpointResolveTest, IPv4PreservesAddress) {
    in_addr_t expected = inet_addr("1.2.3.4");
    ct_remote_endpoint_with_ipv4(remote_endpoint, expected);

    ct_remote_endpoint_resolve(remote_endpoint, &context);

    ct_remote_endpoint_t* out = faked_ct_remote_endpoint_resolve_cb_fake.arg0_val;
    ASSERT_NE(out, nullptr);
    const struct sockaddr_in* addr = (const struct sockaddr_in*)&out->data.resolved_address;
    EXPECT_EQ(addr->sin_family, AF_INET);
    EXPECT_EQ(addr->sin_addr.s_addr, expected);
    // ct_remote_endpoint_resolve_cb is faked, so we own the allocation
    free(out);
}

TEST_F(RemoteEndpointResolveTest, IPv4SetsPortInResolvedAddress) {
    ct_remote_endpoint_with_ipv4(remote_endpoint, inet_addr("1.2.3.4"));
    ct_remote_endpoint_with_port(remote_endpoint, 8080);

    ct_remote_endpoint_resolve(remote_endpoint, &context);

    ct_remote_endpoint_t* out = faked_ct_remote_endpoint_resolve_cb_fake.arg0_val;
    ASSERT_NE(out, nullptr);
    const struct sockaddr_in* addr = (const struct sockaddr_in*)&out->data.resolved_address;
    EXPECT_EQ(ntohs(addr->sin_port), 8080);
    EXPECT_EQ(context.assigned_port, 8080);
    free(out);
}

// --- IPv6 ---

TEST_F(RemoteEndpointResolveTest, IPv6CallsResolveCbWithOneResult) {
    struct in6_addr ipv6 = { .__in6_u = { .__u6_addr8 = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1} } };
    ct_remote_endpoint_with_ipv6(remote_endpoint, ipv6);

    int rc = ct_remote_endpoint_resolve(remote_endpoint, &context);

    EXPECT_EQ(rc, 0);
    EXPECT_EQ(faked_ct_remote_endpoint_resolve_cb_fake.call_count, 1);
    EXPECT_EQ(faked_ct_remote_endpoint_resolve_cb_fake.arg1_val, 1u);
}

TEST_F(RemoteEndpointResolveTest, IPv6SetsPortInResolvedAddress) {
    struct in6_addr ipv6 = { .__in6_u = { .__u6_addr8 = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1} } };
    ct_remote_endpoint_with_ipv6(remote_endpoint, ipv6);
    ct_remote_endpoint_with_port(remote_endpoint, 443);

    ct_remote_endpoint_resolve(remote_endpoint, &context);

    ct_remote_endpoint_t* out = faked_ct_remote_endpoint_resolve_cb_fake.arg0_val;
    ASSERT_NE(out, nullptr);
    const struct sockaddr_in6* addr = (const struct sockaddr_in6*)&out->data.resolved_address;
    EXPECT_EQ(ntohs(addr->sin6_port), 443);
    EXPECT_EQ(context.assigned_port, 443);
    free(out);
}

// --- Hostname (async path) ---

TEST_F(RemoteEndpointResolveTest, HostnameTriggersDnsLookup) {
    ct_remote_endpoint_with_hostname(remote_endpoint, "example.com");

    int rc = ct_remote_endpoint_resolve(remote_endpoint, &context);

    EXPECT_EQ(rc, 0);
    EXPECT_EQ(faked_perform_dns_lookup_fake.call_count, 1);
    EXPECT_STREQ(faked_perform_dns_lookup_fake.arg0_val, "example.com");
    EXPECT_EQ(faked_perform_dns_lookup_fake.arg2_val, &context);
    // resolve_cb must NOT be called synchronously for the hostname path
    EXPECT_EQ(faked_ct_remote_endpoint_resolve_cb_fake.call_count, 0);
}

TEST_F(RemoteEndpointResolveTest, HostnamePassesServiceToDnsLookup) {
    ct_remote_endpoint_with_hostname(remote_endpoint, "example.com");
    ct_remote_endpoint_with_service(remote_endpoint, "https");

    ct_remote_endpoint_resolve(remote_endpoint, &context);

    EXPECT_STREQ(faked_perform_dns_lookup_fake.arg1_val, "https");
}

TEST_F(RemoteEndpointResolveTest, HostnameWithNullServicePassesNullToDnsLookup) {
    ct_remote_endpoint_with_hostname(remote_endpoint, "example.com");
    // No service set

    ct_remote_endpoint_resolve(remote_endpoint, &context);

    EXPECT_EQ(faked_perform_dns_lookup_fake.arg1_val, nullptr);
}

// --- Unspecified endpoint ---

TEST_F(RemoteEndpointResolveTest, UnspecifiedEndpointReturnsError) {
    // Neither hostname nor IP set
    int rc = ct_remote_endpoint_resolve(remote_endpoint, &context);

    EXPECT_EQ(rc, -EINVAL);
}

TEST_F(RemoteEndpointResolveTest, UnspecifiedEndpointCallsResolveCbWithNullAndZero) {
    ct_remote_endpoint_resolve(remote_endpoint, &context);

    EXPECT_EQ(faked_ct_remote_endpoint_resolve_cb_fake.call_count, 1);
    EXPECT_EQ(faked_ct_remote_endpoint_resolve_cb_fake.arg0_val, nullptr);
    EXPECT_EQ(faked_ct_remote_endpoint_resolve_cb_fake.arg1_val, 0u);
}

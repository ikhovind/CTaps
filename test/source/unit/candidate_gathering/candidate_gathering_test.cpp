#include "gtest/gtest.h"
#include <arpa/inet.h>
#include <netinet/in.h>
#include <glib.h>
#include <logging/log.h>

extern "C" {
  #include "fff.h"
  #include "endpoints/local/local_endpoint.h"
  #include "endpoints/remote/remote_endpoint.h"
  #include "connections/preconnection/preconnection.h"
  #include "transport_properties/transport_properties.h"
  #include "protocols/protocol_interface.h"
  #include "protocols/registry/protocol_registry.h"
  #include "impl/candidate_gathering/candidate_gathering.h"
}

DEFINE_FFF_GLOBALS;
FAKE_VALUE_FUNC(int, faked_local_endpoint_resolve, const LocalEndpoint*, LocalEndpoint**, size_t*);
FAKE_VALUE_FUNC(int, faked_remote_endpoint_resolve, const RemoteEndpoint*, RemoteEndpoint**, size_t*);
FAKE_VALUE_FUNC(const ProtocolImplementation**, faked_get_supported_protocols);


extern "C" int __wrap_local_endpoint_resolve(const LocalEndpoint* local_endpoint, LocalEndpoint** out_list, size_t* out_count) {
    return faked_local_endpoint_resolve(local_endpoint, out_list, out_count);
}

extern "C" int __wrap_remote_endpoint_resolve(RemoteEndpoint* remote_endpoint, RemoteEndpoint** out_list, size_t* out_count) {
    return faked_remote_endpoint_resolve(remote_endpoint, out_list, out_count);
}

extern "C" const ProtocolImplementation** __wrap_get_supported_protocols() {
    return faked_get_supported_protocols();
}

extern "C" size_t __wrap_get_num_protocols() {
    return 2;
}

static LocalEndpoint* fake_local_endpoint_list;
int local_endpoint_resolve_fake_custom(const LocalEndpoint* local_endpoint, LocalEndpoint** out_list, size_t* out_count) {
    fake_local_endpoint_list = (LocalEndpoint*)malloc(sizeof(LocalEndpoint) * 2);
    // We expect the function to be called with a placeholder local_endpoint.
    // We allocate and populate the output list with our mock data.

    // First fake endpoint
    local_endpoint_build(&fake_local_endpoint_list[0]);
    local_endpoint_with_interface(&fake_local_endpoint_list[0], "lo");
    local_endpoint_with_port(&fake_local_endpoint_list[0], 8080);

    // Second fake endpoint
    local_endpoint_build(&fake_local_endpoint_list[1]);
    local_endpoint_with_interface(&fake_local_endpoint_list[1], "en0");
    local_endpoint_with_port(&fake_local_endpoint_list[1], 8081);

    *out_list = fake_local_endpoint_list;
    *out_count = 2;
    return 0;
}

// Fake data for get_supported_protocols
static ProtocolImplementation mock_proto_1 = {
    .name = "MockProto1",
    .selection_properties = {
      .selection_property = {
        [RELIABILITY] = {.value = {.simple_preference = PROHIBIT}},
      }
    }
};
static ProtocolImplementation mock_proto_2 = {
    .name = "MockProto2",
    .selection_properties = {
      .selection_property = {
        [RELIABILITY] = {.value = {.simple_preference = REQUIRE}},
      }
    }
};
static const ProtocolImplementation* fake_protocol_list[] = {&mock_proto_1, &mock_proto_2, nullptr};
const ProtocolImplementation** get_supported_protocols_fake_custom() {
    return fake_protocol_list;
}

// Fake data for remote_endpoint_resolve
static RemoteEndpoint* fake_remote_endpoint_list;
int remote_endpoint_resolve_fake_custom(const RemoteEndpoint* remote_endpoint, RemoteEndpoint** out_list, size_t* out_count) {
    fake_remote_endpoint_list = (RemoteEndpoint*)malloc(sizeof(RemoteEndpoint) * 1);
    remote_endpoint_build(&fake_remote_endpoint_list[0]);
    remote_endpoint_with_ipv4(&fake_remote_endpoint_list[0], inet_addr("1.2.3.4"));
    remote_endpoint_with_port(&fake_remote_endpoint_list[0], 80);

    *out_list = fake_remote_endpoint_list;
    *out_count = 1;
    return 0;
}

// Helper function to free the GNode tree
void free_candidate_tree(GArray* candidate_list) {
    //g_node_destroy_and_unref(node);
}

// --- Test Fixture ---
class CandidateTreeTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Reset all mock data before each test
        FFF_RESET_HISTORY();
        RESET_FAKE(faked_local_endpoint_resolve);
        RESET_FAKE(faked_remote_endpoint_resolve);
        RESET_FAKE(faked_get_supported_protocols);
        
        faked_local_endpoint_resolve_fake.custom_fake = local_endpoint_resolve_fake_custom;
        faked_get_supported_protocols_fake.custom_fake = get_supported_protocols_fake_custom;
        faked_remote_endpoint_resolve_fake.custom_fake = remote_endpoint_resolve_fake_custom;
    }

    void TearDown() override {
        // Since the fakes point to static memory, we don't need to free the
        // output pointers in the teardown, as they are not dynamically allocated
        // on a per-call basis.
    }
};

// --- Test Case ---
TEST_F(CandidateTreeTest, CreatesAndResolvesFullTree) {
    // --- ARRANGE ---
    // 1. Create a minimal preconnection object
    Preconnection preconnection;
    TransportProperties props;
    transport_properties_build(&props);
    // need to overwrite the default to allow both protocols
    tp_set_sel_prop_preference(&props, RELIABILITY, NO_PREFERENCE);

    RemoteEndpoint remote_endpoint;
    remote_endpoint_build(&remote_endpoint);
    remote_endpoint_with_hostname(&remote_endpoint, "test.com");
    preconnection_build(&preconnection, props, &remote_endpoint, 1);
    
    // 2. Mock behavior of internal functions
    faked_local_endpoint_resolve_fake.return_val = 0;
    faked_remote_endpoint_resolve_fake.return_val = 0;
    faked_get_supported_protocols_fake.return_val = fake_protocol_list;

    // --- ACT ---
    GArray* root = get_ordered_candidate_nodes(&preconnection);

    // --- ASSERT ---
    // 1. Verify the root node
    ASSERT_NE(root, nullptr);

    // Check that the tree was built
    ASSERT_EQ(root->len, 2 * 2 * 1); // 2 local endpoints, 2 protocols, 1 remote endpoint each

    // 2. Verify the calls to mocked functions
    ASSERT_EQ(faked_local_endpoint_resolve_fake.call_count, 1);
    ASSERT_EQ(faked_get_supported_protocols_fake.call_count, 2); // Called for each path child
    ASSERT_EQ(faked_remote_endpoint_resolve_fake.call_count, 4); // Called for each protocol leaf

    // 3. Verify data in a leaf node
    CandidateNode first_node = g_array_index(root, CandidateNode, 0);

    ASSERT_STREQ(first_node.protocol->name, "MockProto1");
    ASSERT_EQ(first_node.type, NODE_TYPE_ENDPOINT);
    ASSERT_EQ(memcmp(&first_node.local_endpoint->data, &fake_local_endpoint_list[0].data, sizeof(struct sockaddr_storage)), 0);
    ASSERT_EQ(first_node.protocol, &mock_proto_1);
    ASSERT_EQ(memcmp(&first_node.remote_endpoint->data, &fake_remote_endpoint_list[0].data, sizeof(struct sockaddr_storage)), 0);

    // --- CLEANUP ---
    g_array_free(root, true);
}

TEST_F(CandidateTreeTest, PrunesPathAndProtocol) {
    // --- ARRANGE ---
    // 1. Create a minimal preconnection object
    Preconnection preconnection;

    TransportProperties props;
    transport_properties_build(&props);
    tp_set_sel_prop_preference(&props, RELIABILITY, REQUIRE);
    tp_set_sel_prop_interface(&props, "Ethernet", REQUIRE);


    RemoteEndpoint remote_endpoint;
    remote_endpoint_build(&remote_endpoint);
    remote_endpoint_with_hostname(&remote_endpoint, "test.com");
    preconnection_build(&preconnection, props, &remote_endpoint, 1);

    // 2. Mock behavior of internal functions
    faked_local_endpoint_resolve_fake.return_val = 0;
    faked_remote_endpoint_resolve_fake.return_val = 0;
    faked_get_supported_protocols_fake.return_val = fake_protocol_list;

    // --- ACT ---
    GArray* root = get_ordered_candidate_nodes(&preconnection);

    // --- ASSERT ---
    // 1. Verify the root node
    ASSERT_NE(root, nullptr);

    // Check that the tree was built
    ASSERT_EQ(root->len, 1 * 1 * 1); // 1 local endpoint, 1 protocol, 1 remote endpoint each

    // 2. Verify the calls to mocked functions
    ASSERT_EQ(faked_local_endpoint_resolve_fake.call_count, 1);
    ASSERT_EQ(faked_get_supported_protocols_fake.call_count, 2); // Called for each path child
    ASSERT_EQ(faked_remote_endpoint_resolve_fake.call_count, 4); // Called for each protocol leaf

    // 3. Verify data in a leaf node
    CandidateNode leaf_data = g_array_index(root, CandidateNode, 0);

    ASSERT_EQ(leaf_data.type, NODE_TYPE_ENDPOINT);
    // compare memory instead
    ASSERT_EQ(memcmp(&leaf_data.local_endpoint->data, &fake_local_endpoint_list[0].data, sizeof(struct sockaddr_storage)), 0);

    //ASSERT_EQ(leaf_data->local_endpoint, &fake_local_endpoint_list[0]);
    ASSERT_EQ(leaf_data.protocol, &mock_proto_2);
    ASSERT_EQ(memcmp(&leaf_data.remote_endpoint->data, &fake_remote_endpoint_list[0].data, sizeof(struct sockaddr_storage)), 0);

    // --- CLEANUP ---
    free_candidate_tree(root);
}

TEST_F(CandidateTreeTest, SortsWhenAvoidingProperty) {
    // --- ARRANGE ---
    // 1. Create a minimal preconnection object
    Preconnection preconnection;
    TransportProperties props;
    transport_properties_build(&props);
    // need to overwrite the default to allow both protocols
    tp_set_sel_prop_preference(&props, RELIABILITY, PREFER);

    RemoteEndpoint remote_endpoint;
    remote_endpoint_build(&remote_endpoint);
    remote_endpoint_with_hostname(&remote_endpoint, "test.com");
    preconnection_build(&preconnection, props, &remote_endpoint, 1);

    // 2. Mock behavior of internal functions
    faked_local_endpoint_resolve_fake.return_val = 0;
    faked_remote_endpoint_resolve_fake.return_val = 0;
    faked_get_supported_protocols_fake.return_val = fake_protocol_list;

    // --- ACT ---
    GArray* root = get_ordered_candidate_nodes(&preconnection);

    // --- ASSERT ---
    // 1. Verify the root node
    ASSERT_NE(root, nullptr);

    // Check that the tree was built
    ASSERT_EQ(root->len, 2 * 2 * 1); // 2 local endpoint, 2 protocol, 1 remote endpoint each

    // 2. Verify the calls to mocked functions
    ASSERT_EQ(faked_local_endpoint_resolve_fake.call_count, 1);
    ASSERT_EQ(faked_get_supported_protocols_fake.call_count, 2); // Called for each path child
    ASSERT_EQ(faked_remote_endpoint_resolve_fake.call_count, 4); // Called for each protocol leaf

    // 3. Verify data in a leaf node
    CandidateNode leaf_data = g_array_index(root, CandidateNode, 0);

    ASSERT_STREQ(leaf_data.protocol->name, "MockProto2");
    ASSERT_EQ(leaf_data.type, NODE_TYPE_ENDPOINT);
    ASSERT_EQ(memcmp(&leaf_data.local_endpoint->data, &fake_local_endpoint_list[0].data, sizeof(struct sockaddr_storage)), 0);
    ASSERT_EQ(memcmp(&leaf_data.remote_endpoint->data, &fake_remote_endpoint_list[0].data, sizeof(struct sockaddr_storage)), 0);

    // --- CLEANUP ---
    free_candidate_tree(root);

}
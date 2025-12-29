#include "gtest/gtest.h"
#include <arpa/inet.h>
#include <netinet/in.h>
#include <glib.h>

extern "C" {
  #include "fff.h"
  #include <logging/log.h>
#include "ctaps.h"
#include "ctaps_internal.h"
  #include "candidate_gathering/candidate_gathering.h"
}

DEFINE_FFF_GLOBALS;
FAKE_VALUE_FUNC(int, faked_ct_local_endpoint_resolve, const ct_local_endpoint_t*, ct_local_endpoint_t**, size_t*);
FAKE_VALUE_FUNC(int, faked_ct_remote_endpoint_resolve, const ct_remote_endpoint_t*, ct_remote_endpoint_t**, size_t*);
FAKE_VALUE_FUNC(const ct_protocol_impl_t**, faked_ct_get_supported_protocols);


extern "C" int __wrap_ct_local_endpoint_resolve(const ct_local_endpoint_t* local_endpoint, ct_local_endpoint_t** out_list, size_t* out_count) {
    return faked_ct_local_endpoint_resolve(local_endpoint, out_list, out_count);
}

extern "C" int __wrap_ct_remote_endpoint_resolve(ct_remote_endpoint_t* remote_endpoint, ct_remote_endpoint_t** out_list, size_t* out_count) {
    return faked_ct_remote_endpoint_resolve(remote_endpoint, out_list, out_count);
}

extern "C" const ct_protocol_impl_t** __wrap_ct_get_supported_protocols() {
    return faked_ct_get_supported_protocols();
}

extern "C" size_t __wrap_ct_get_num_protocols() {
    return 3;
}

static ct_local_endpoint_t* fake_local_endpoint_list;
int local_endpoint_resolve_fake_custom(const ct_local_endpoint_t* local_endpoint, ct_local_endpoint_t** out_list, size_t* out_count) {
    fake_local_endpoint_list = (ct_local_endpoint_t*)malloc(sizeof(ct_local_endpoint_t) * 2);
    // We expect the function to be called with a placeholder local_endpoint.
    // We allocate and populate the output list with our mock data.

    // First fake endpoint
    ct_local_endpoint_build(&fake_local_endpoint_list[0]);
    ct_local_endpoint_with_interface(&fake_local_endpoint_list[0], "lo");
    ct_local_endpoint_with_port(&fake_local_endpoint_list[0], 8080);

    // Second fake endpoint
    ct_local_endpoint_build(&fake_local_endpoint_list[1]);
    ct_local_endpoint_with_interface(&fake_local_endpoint_list[1], "en0");
    ct_local_endpoint_with_port(&fake_local_endpoint_list[1], 8081);

    *out_list = fake_local_endpoint_list;
    *out_count = 2;
    return 0;
}

// Fake data for get_supported_protocols
static ct_protocol_impl_t mock_proto_1 = {
    .name = "MockProto1",
    .selection_properties = {
      .selection_property = {
        [RELIABILITY] = {.value = {.simple_preference = PROHIBIT}},
        [PRESERVE_MSG_BOUNDARIES] = {.value = {.simple_preference = PROHIBIT}},
        [PER_MSG_RELIABILITY] = {.value = {.simple_preference = PROHIBIT}},
        [PRESERVE_ORDER] = {.value = {.simple_preference = NO_PREFERENCE}},
      }
    }
};
static ct_protocol_impl_t mock_proto_2 = {
    .name = "MockProto2",
    .selection_properties = {
      .selection_property = {
        [RELIABILITY] = {.value = {.simple_preference = REQUIRE}},
        [PRESERVE_MSG_BOUNDARIES] = {.value = {.simple_preference = REQUIRE}},
        [PER_MSG_RELIABILITY] = {.value = {.simple_preference = REQUIRE}},
        [PRESERVE_ORDER] = {.value = {.simple_preference = REQUIRE}},
      }
    }
};
static ct_protocol_impl_t mock_proto_3 = {
    .name = "MockProto3",
    .selection_properties = {
      .selection_property = {
        [RELIABILITY] = {.value = {.simple_preference = REQUIRE}},
        [PRESERVE_MSG_BOUNDARIES] = {.value = {.simple_preference = PROHIBIT}},
        [PER_MSG_RELIABILITY] = {.value = {.simple_preference = PROHIBIT}},
        [PRESERVE_ORDER] = {.value = {.simple_preference = PROHIBIT}},
      }
    }
};


static const ct_protocol_impl_t* fake_protocol_list[] = {&mock_proto_1, &mock_proto_2, &mock_proto_3, nullptr};
const ct_protocol_impl_t** get_supported_protocols_fake_custom() {
    return fake_protocol_list;
}

// Fake data for ct_remote_endpoint_resolve
static ct_remote_endpoint_t** fake_remote_endpoint_list;
int remote_endpoint_resolve_fake_custom(const ct_remote_endpoint_t* remote_endpoint, ct_remote_endpoint_t** out_list, size_t* out_count) {
    fake_remote_endpoint_list = (ct_remote_endpoint_t**)malloc(sizeof(ct_remote_endpoint_t*) * 1);
    ct_remote_endpoint_build(fake_remote_endpoint_list[0]);
    ct_remote_endpoint_with_ipv4(fake_remote_endpoint_list[0], inet_addr("1.2.3.4"));
    ct_remote_endpoint_with_port(fake_remote_endpoint_list[0], 80);

    *out_list = *fake_remote_endpoint_list;
    *out_count = 1;
    return 0;
}

// --- Test Fixture ---
class CandidateTreeTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Reset all mock data before each test
        FFF_RESET_HISTORY();
        RESET_FAKE(faked_ct_local_endpoint_resolve);
        RESET_FAKE(faked_ct_remote_endpoint_resolve);
        RESET_FAKE(faked_ct_get_supported_protocols);
        
        faked_ct_local_endpoint_resolve_fake.custom_fake = local_endpoint_resolve_fake_custom;
        faked_ct_get_supported_protocols_fake.custom_fake = get_supported_protocols_fake_custom;
        faked_ct_remote_endpoint_resolve_fake.custom_fake = remote_endpoint_resolve_fake_custom;
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
    ct_preconnection_t preconnection;
    ct_transport_properties_t props;
    ct_transport_properties_build(&props);
    // need to overwrite the default to allow both protocols
    ct_tp_set_sel_prop_preference(&props, RELIABILITY, NO_PREFERENCE);
    ct_tp_set_sel_prop_preference(&props, PRESERVE_ORDER, NO_PREFERENCE);

    ct_remote_endpoint_t remote_endpoint;
    ct_remote_endpoint_build(&remote_endpoint);
    ct_remote_endpoint_with_hostname(&remote_endpoint, "test.com");
    ct_preconnection_build(&preconnection, props, &remote_endpoint, 1, NULL);
    
    // 2. Mock behavior of internal functions
    faked_ct_local_endpoint_resolve_fake.return_val = 0;
    faked_ct_remote_endpoint_resolve_fake.return_val = 0;
    faked_ct_get_supported_protocols_fake.return_val = fake_protocol_list;

    // --- ACT ---
    GArray* root = get_ordered_candidate_nodes(&preconnection);

    printf("Root length is %d\n", root->len);

    // --- ASSERT ---
    // 1. Verify the root node
    ASSERT_NE(root, nullptr);

    // Check that the tree was built
    ASSERT_EQ(root->len, 2 * 3 * 1); // 2 local endpoints, 3 protocols, 1 remote endpoint each

    // 2. Verify the calls to mocked functions
    ASSERT_EQ(faked_ct_local_endpoint_resolve_fake.call_count, 1);
    ASSERT_EQ(faked_ct_get_supported_protocols_fake.call_count, 2); // Called for each path child
    ASSERT_EQ(faked_ct_remote_endpoint_resolve_fake.call_count, 6); // Called for each protocol leaf

    // 3. Verify data in a leaf node
    ct_candidate_node_t first_node = g_array_index(root, ct_candidate_node_t, 0);

    ASSERT_STREQ(first_node.protocol->name, "MockProto1");
    ASSERT_EQ(first_node.type, NODE_TYPE_ENDPOINT);
    ASSERT_EQ(first_node.protocol, &mock_proto_1);

    // --- CLEANUP ---
    free_candidate_array(root);
    ct_preconnection_free(&preconnection);
    ct_free_remote_endpoint_strings(&remote_endpoint);
}

TEST_F(CandidateTreeTest, PrunesPathAndProtocol) {
    // --- ARRANGE ---
    // 1. Create a minimal preconnection object
    ct_preconnection_t preconnection;

    ct_transport_properties_t props;
    ct_transport_properties_build(&props);
    ct_tp_set_sel_prop_preference(&props, RELIABILITY, REQUIRE);
    ct_tp_set_sel_prop_preference(&props, PRESERVE_ORDER, NO_PREFERENCE);
    ct_tp_set_sel_prop_interface(&props, "Ethernet", REQUIRE);


    ct_remote_endpoint_t remote_endpoint;
    ct_remote_endpoint_build(&remote_endpoint);
    ct_remote_endpoint_with_hostname(&remote_endpoint, "test.com");
    ct_preconnection_build(&preconnection, props, &remote_endpoint, 1, NULL);

    // 2. Mock behavior of internal functions
    faked_ct_local_endpoint_resolve_fake.return_val = 0;
    faked_ct_remote_endpoint_resolve_fake.return_val = 0;
    faked_ct_get_supported_protocols_fake.return_val = fake_protocol_list;

    // --- ACT ---
    GArray* candidates = get_ordered_candidate_nodes(&preconnection);

    // --- ASSERT ---
    // 1. Verify the root node
    ASSERT_NE(candidates, nullptr);

    // Check that the tree was built
    ASSERT_EQ(candidates->len, 1 * 2 * 1); // 1 local endpoint, 2 protocol, 1 remote endpoint each

    // 2. Verify the calls to mocked functions
    ASSERT_EQ(faked_ct_local_endpoint_resolve_fake.call_count, 1);
    ASSERT_EQ(faked_ct_get_supported_protocols_fake.call_count, 2); // Called for each path child
    ASSERT_EQ(faked_ct_remote_endpoint_resolve_fake.call_count, 6); // Called for each protocol leaf

    // 3. Verify data in result list
    ct_candidate_node_t first_candidate = g_array_index(candidates, ct_candidate_node_t, 0);

    ASSERT_STREQ(first_candidate.protocol->name, "MockProto2");
    ASSERT_EQ(first_candidate.type, NODE_TYPE_ENDPOINT);

    ct_candidate_node_t second_candidate = g_array_index(candidates, ct_candidate_node_t, 1);

    ASSERT_STREQ(second_candidate.protocol->name, "MockProto3");
    ASSERT_EQ(second_candidate.type, NODE_TYPE_ENDPOINT);

    // --- CLEANUP ---
    free_candidate_array(candidates);
    ct_preconnection_free(&preconnection);
    ct_free_remote_endpoint_strings(&remote_endpoint);
}

TEST_F(CandidateTreeTest, SortsOnPreferOverAvoid) {
    // --- ARRANGE ---
    // 1. Create a minimal preconnection object
    ct_preconnection_t preconnection;
    ct_transport_properties_t props;
    ct_transport_properties_build(&props);

    // This selects p2 and p3
    ct_tp_set_sel_prop_preference(&props, RELIABILITY, REQUIRE);

    // this prefers p2
    ct_tp_set_sel_prop_preference(&props, PRESERVE_MSG_BOUNDARIES, PREFER);

    // These favor p3, but the one preference should still win
    ct_tp_set_sel_prop_preference(&props, PER_MSG_RELIABILITY, AVOID);
    ct_tp_set_sel_prop_preference(&props, PRESERVE_ORDER, AVOID);

    ct_remote_endpoint_t remote_endpoint;
    ct_remote_endpoint_build(&remote_endpoint);
    ct_remote_endpoint_with_hostname(&remote_endpoint, "test.com");
    ct_preconnection_build(&preconnection, props, &remote_endpoint, 1, NULL);

    // 2. Mock behavior of internal functions
    faked_ct_local_endpoint_resolve_fake.return_val = 0;
    faked_ct_remote_endpoint_resolve_fake.return_val = 0;
    faked_ct_get_supported_protocols_fake.return_val = fake_protocol_list;

    // --- ACT ---
    GArray* root = get_ordered_candidate_nodes(&preconnection);

    // --- ASSERT ---
    // 1. Verify the root node
    ASSERT_NE(root, nullptr);

    // Check that the tree was built
    ASSERT_EQ(root->len, 2 * 2 * 1); // 2 local endpoint, 2 protocol, 1 remote endpoint each

    // 2. Verify the calls to mocked functions
    ASSERT_EQ(faked_ct_local_endpoint_resolve_fake.call_count, 1);
    ASSERT_EQ(faked_ct_get_supported_protocols_fake.call_count, 2); // Called for each path child
    ASSERT_EQ(faked_ct_remote_endpoint_resolve_fake.call_count, 6); // Called for each protocol leaf

    ct_candidate_node_t first_candidate = g_array_index(root, ct_candidate_node_t, 0);

    ASSERT_STREQ(first_candidate.protocol->name, "MockProto2");
    ASSERT_EQ(first_candidate.type, NODE_TYPE_ENDPOINT);

    ct_candidate_node_t second_candidate = g_array_index(root, ct_candidate_node_t, 1);

    ASSERT_STREQ(second_candidate.protocol->name, "MockProto2");
    ASSERT_EQ(second_candidate.type, NODE_TYPE_ENDPOINT);

    ct_candidate_node_t third_candidate = g_array_index(root, ct_candidate_node_t, 2);
    ASSERT_STREQ(third_candidate.protocol->name, "MockProto3");
    ASSERT_EQ(third_candidate.type, NODE_TYPE_ENDPOINT);

    ct_candidate_node_t fourth_candidate = g_array_index(root, ct_candidate_node_t, 3);
    ASSERT_STREQ(fourth_candidate.protocol->name, "MockProto3");
    ASSERT_EQ(fourth_candidate.type, NODE_TYPE_ENDPOINT);

    // --- CLEANUP ---
    free_candidate_array(root);
    ct_preconnection_free(&preconnection);
    ct_free_remote_endpoint_strings(&remote_endpoint);
}

TEST_F(CandidateTreeTest, UsesAvoidAsTieBreaker) {
    // --- ARRANGE ---
    // 1. Create a minimal preconnection object
    ct_preconnection_t preconnection;
    ct_transport_properties_t props;
    ct_transport_properties_build(&props);

    // Override default to get all protocols
    ct_tp_set_sel_prop_preference(&props, RELIABILITY, NO_PREFERENCE);
    ct_tp_set_sel_prop_preference(&props, PRESERVE_ORDER, NO_PREFERENCE);

    // protocol 2 and 3 are preferred
    ct_tp_set_sel_prop_preference(&props, RELIABILITY, PREFER);
    // But 3 should win tiebreaker with avoid
    ct_tp_set_sel_prop_preference(&props, PRESERVE_MSG_BOUNDARIES, AVOID);

    ct_remote_endpoint_t remote_endpoint;
    ct_remote_endpoint_build(&remote_endpoint);
    ct_remote_endpoint_with_hostname(&remote_endpoint, "test.com");
    ct_preconnection_build(&preconnection, props, &remote_endpoint, 1, NULL);

    // 2. Mock behavior of internal functions
    faked_ct_local_endpoint_resolve_fake.return_val = 0;
    faked_ct_remote_endpoint_resolve_fake.return_val = 0;
    faked_ct_get_supported_protocols_fake.return_val = fake_protocol_list;

    // --- ACT ---
    GArray* root = get_ordered_candidate_nodes(&preconnection);

    // --- ASSERT ---
    // 1. Verify the root node
    ASSERT_NE(root, nullptr);

    // Check that the tree was built
    ASSERT_EQ(root->len, 2 * 3 * 1); // 2 local endpoint, 3 protocol, 1 remote endpoint each

    // 2. Verify the calls to mocked functions
    ASSERT_EQ(faked_ct_local_endpoint_resolve_fake.call_count, 1);
    ASSERT_EQ(faked_ct_get_supported_protocols_fake.call_count, 2); // Called for each path child
    ASSERT_EQ(faked_ct_remote_endpoint_resolve_fake.call_count, 6); // Called for each protocol leaf

    ct_candidate_node_t first_candidate = g_array_index(root, ct_candidate_node_t, 0);

    ASSERT_STREQ(first_candidate.protocol->name, "MockProto3");
    ASSERT_EQ(first_candidate.type, NODE_TYPE_ENDPOINT);

    ct_candidate_node_t second_candidate = g_array_index(root, ct_candidate_node_t, 1);

    ASSERT_STREQ(second_candidate.protocol->name, "MockProto3");
    ASSERT_EQ(second_candidate.type, NODE_TYPE_ENDPOINT);

    ct_candidate_node_t third_candidate = g_array_index(root, ct_candidate_node_t, 2);
    ASSERT_STREQ(third_candidate.protocol->name, "MockProto2");
    ASSERT_EQ(third_candidate.type, NODE_TYPE_ENDPOINT);

    ct_candidate_node_t fourth_candidate = g_array_index(root, ct_candidate_node_t, 3);
    ASSERT_STREQ(fourth_candidate.protocol->name, "MockProto2");
    ASSERT_EQ(fourth_candidate.type, NODE_TYPE_ENDPOINT);

    // --- CLEANUP ---
    free_candidate_array(root);
    ct_preconnection_free(&preconnection);
    ct_free_remote_endpoint_strings(&remote_endpoint);
}

TEST_F(CandidateTreeTest, GivesNoCandidateNodesWhenAllProtocolsProhibited) {
    // --- ARRANGE ---
    // 1. Create a minimal preconnection object
    ct_preconnection_t preconnection;
    ct_transport_properties_t props;
    ct_transport_properties_build(&props);
    // need to overwrite the default to allow both protocols
    ct_tp_set_sel_prop_preference(&props, RELIABILITY, PROHIBIT);
    ct_tp_set_sel_prop_preference(&props, PRESERVE_MSG_BOUNDARIES, REQUIRE);

    ct_remote_endpoint_t remote_endpoint;
    ct_remote_endpoint_build(&remote_endpoint);
    ct_remote_endpoint_with_hostname(&remote_endpoint, "test.com");
    ct_preconnection_build(&preconnection, props, &remote_endpoint, 1, NULL);

    // 2. Mock behavior of internal functions
    faked_ct_local_endpoint_resolve_fake.return_val = 0;
    faked_ct_remote_endpoint_resolve_fake.return_val = 0;
    faked_ct_get_supported_protocols_fake.return_val = fake_protocol_list;

    // --- ACT ---
    GArray* candidates = get_ordered_candidate_nodes(&preconnection);

    // --- ASSERT ---
    // 1. Verify the root node
    ASSERT_NE(candidates, nullptr);

    ASSERT_EQ(candidates->len, 0); // nothing should be compatible with our requirements

    // 2. Verify the calls to mocked functions
    ASSERT_EQ(faked_ct_local_endpoint_resolve_fake.call_count, 1);
    ASSERT_EQ(faked_ct_get_supported_protocols_fake.call_count, 2); // Called for each path child
    ASSERT_EQ(faked_ct_remote_endpoint_resolve_fake.call_count, 6); // Called for each protocol leaf

    // --- CLEANUP ---
    free_candidate_array(candidates);
    ct_preconnection_free(&preconnection);
    ct_free_remote_endpoint_strings(&remote_endpoint);
}

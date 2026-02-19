#include "gtest/gtest.h"
#include <arpa/inet.h>
#include <netinet/in.h>
#include <glib.h>
#include <map>
#include <string>

extern "C" {
  #include "fff.h"
  #include <logging/log.h>
  #include "ctaps.h"
  #include "ctaps_internal.h"
  #include "candidate_gathering/candidate_gathering.h"
  #include "endpoint/local_endpoint.h"
  #include "endpoint/remote_endpoint.h"
}

DEFINE_FFF_GLOBALS;
FAKE_VALUE_FUNC(int, faked_ct_local_endpoint_resolve, const ct_local_endpoint_t*, ct_local_endpoint_t**, size_t*);
FAKE_VALUE_FUNC(int, faked_ct_remote_endpoint_resolve, const ct_remote_endpoint_t*, ct_remote_endpoint_t**, size_t*);

extern "C" int __wrap_ct_local_endpoint_resolve(const ct_local_endpoint_t* ep, ct_local_endpoint_t** out, size_t* count) {
    return faked_ct_local_endpoint_resolve(ep, out, count);
}
extern "C" int __wrap_ct_remote_endpoint_resolve(ct_remote_endpoint_t* ep, ct_remote_endpoint_t** out, size_t* count) {
    return faked_ct_remote_endpoint_resolve(ep, out, count);
}

// Custom fakes
int local_endpoint_resolve_fake_custom(const ct_local_endpoint_t*, ct_local_endpoint_t** out, size_t* count) {
    ct_local_endpoint_t* list = (ct_local_endpoint_t*)malloc(sizeof(ct_local_endpoint_t) * 2);
    ct_local_endpoint_build(&list[0]);
    ct_local_endpoint_with_interface(&list[0], "lo");
    ct_local_endpoint_with_port(&list[0], 8080);
    ct_local_endpoint_build(&list[1]);
    ct_local_endpoint_with_interface(&list[1], "en0");
    ct_local_endpoint_with_port(&list[1], 8081);
    *out = list;
    *count = 2;
    return 0;
}

int remote_endpoint_resolve_fake_custom(const ct_remote_endpoint_t*, ct_remote_endpoint_t** out, size_t* count) {
    ct_remote_endpoint_t* list = (ct_remote_endpoint_t*)malloc(sizeof(ct_remote_endpoint_t));
    ct_remote_endpoint_build(&list[0]);
    ct_remote_endpoint_with_ipv4(&list[0], inet_addr("1.2.3.4"));
    ct_remote_endpoint_with_port(&list[0], 80);
    *out = list;
    *count = 1;
    return 0;
}

// --- Fixture ---
class CandidateGatheringTest : public ::testing::Test {
protected:
    ct_transport_properties_t* props = nullptr;
    ct_remote_endpoint_t* remote_endpoint = nullptr;
    ct_preconnection_t* preconnection = nullptr;

    void SetUp() override {
        ct_set_log_level(CT_LOG_TRACE);
        FFF_RESET_HISTORY();
        RESET_FAKE(faked_ct_local_endpoint_resolve);
        RESET_FAKE(faked_ct_remote_endpoint_resolve);
        faked_ct_local_endpoint_resolve_fake.custom_fake = local_endpoint_resolve_fake_custom;
        faked_ct_remote_endpoint_resolve_fake.custom_fake = remote_endpoint_resolve_fake_custom;

        props = ct_transport_properties_new();
        for (int i = 0; i < SELECTION_PROPERTY_END; i++) {
            if (props->selection_properties.selection_property[i].type == TYPE_PREFERENCE) {
                props->selection_properties.selection_property[i].value.simple_preference = NO_PREFERENCE;
            }
        }
    }

    // Build a preconnection with the given property overrides. Tests call this
    // after configuring props, so they only set what's relevant to their scenario.
    void BuildPreconnection(ct_security_parameters_t* sec_params = nullptr) {
        ASSERT_NE(props, nullptr);
        remote_endpoint = ct_remote_endpoint_new();
        ASSERT_NE(remote_endpoint, nullptr);
        ct_remote_endpoint_with_hostname(remote_endpoint, "test.com");
        preconnection = ct_preconnection_new(remote_endpoint, 1, props, sec_params);
        ASSERT_NE(preconnection, nullptr);
    }

    void TearDown() override {
        if (preconnection) ct_preconnection_free(preconnection);
        if (props)         ct_transport_properties_free(props);
        if (remote_endpoint) ct_remote_endpoint_free(remote_endpoint);
    }
};

// --- Tests ---

TEST_F(CandidateGatheringTest, CreatesAndResolvesFullTree) {
    BuildPreconnection();

    GArray* candidates = get_ordered_candidate_nodes(preconnection);
    ASSERT_NE(candidates, nullptr);
    ASSERT_EQ(candidates->len, 2 * 3 * 1); // 2 local, 3 protocols, 1 remote

    ASSERT_EQ(faked_ct_local_endpoint_resolve_fake.call_count, 1);
    ASSERT_EQ(faked_ct_remote_endpoint_resolve_fake.call_count, 6);

    ct_candidate_node_t first = g_array_index(candidates, ct_candidate_node_t, 0);
    ASSERT_EQ(first.type, NODE_TYPE_ENDPOINT);

    free_candidate_array(candidates);
}

TEST_F(CandidateGatheringTest, PrunesPathAndProtocol) {
    ct_tp_set_sel_prop_preference(props, RELIABILITY, REQUIRE); // UDP pruned
    ct_tp_set_sel_prop_interface(props, "Ethernet", REQUIRE);
    BuildPreconnection();

    GArray* candidates = get_ordered_candidate_nodes(preconnection);
    ASSERT_NE(candidates, nullptr);
    ASSERT_EQ(candidates->len, 1 * 2 * 1); // lo pruned, UDP pruned

    ASSERT_STRNE(g_array_index(candidates, ct_candidate_node_t, 0).protocol_candidate->protocol_impl->name, "UDP");
    ASSERT_STRNE(g_array_index(candidates, ct_candidate_node_t, 1).protocol_candidate->protocol_impl->name, "UDP");

    free_candidate_array(candidates);
}

TEST_F(CandidateGatheringTest, SortsOnPreferOverAvoid) {
    ct_tp_set_sel_prop_preference(props, PRESERVE_MSG_BOUNDARIES, REQUIRE);       // Prune TCP 
    // QUIC should win even if UDP has more avoids, since prefers are stronger than avoids
    ct_tp_set_sel_prop_preference(props, MULTISTREAMING, PREFER);
    ct_tp_set_sel_prop_preference(props, PRESERVE_ORDER, AVOID);
    ct_tp_set_sel_prop_preference(props, CONGESTION_CONTROL, AVOID);
    BuildPreconnection();

    GArray* candidates = get_ordered_candidate_nodes(preconnection);
    ASSERT_NE(candidates, nullptr);
    ASSERT_EQ(candidates->len, 2 * 2 * 1);

    ASSERT_STREQ(g_array_index(candidates, ct_candidate_node_t, 0).protocol_candidate->protocol_impl->name, "QUIC");
    ASSERT_STREQ(g_array_index(candidates, ct_candidate_node_t, 1).protocol_candidate->protocol_impl->name, "QUIC");
    ASSERT_STREQ(g_array_index(candidates, ct_candidate_node_t, 2).protocol_candidate->protocol_impl->name, "UDP");
    ASSERT_STREQ(g_array_index(candidates, ct_candidate_node_t, 3).protocol_candidate->protocol_impl->name, "UDP");

    free_candidate_array(candidates);
}

TEST_F(CandidateGatheringTest, UsesAvoidAsTieBreaker) {
    // QUIC and TCP both match these two 
    ct_tp_set_sel_prop_preference(props, RELIABILITY, PREFER);
    ct_tp_set_sel_prop_preference(props, CONGESTION_CONTROL, PREFER);
    // But only TCP here, so TCP should be placed first
    ct_tp_set_sel_prop_preference(props, PRESERVE_MSG_BOUNDARIES, AVOID);
    BuildPreconnection();

    GArray* candidates = get_ordered_candidate_nodes(preconnection);
    ASSERT_NE(candidates, nullptr);
    ASSERT_EQ(candidates->len, 2 * 3 * 1);

    ASSERT_STREQ(g_array_index(candidates, ct_candidate_node_t, 0).protocol_candidate->protocol_impl->name, "TCP");
    ASSERT_STREQ(g_array_index(candidates, ct_candidate_node_t, 1).protocol_candidate->protocol_impl->name, "TCP");
    ASSERT_STREQ(g_array_index(candidates, ct_candidate_node_t, 2).protocol_candidate->protocol_impl->name, "QUIC");
    ASSERT_STREQ(g_array_index(candidates, ct_candidate_node_t, 3).protocol_candidate->protocol_impl->name, "QUIC");
    ASSERT_STREQ(g_array_index(candidates, ct_candidate_node_t, 4).protocol_candidate->protocol_impl->name, "UDP");
    ASSERT_STREQ(g_array_index(candidates, ct_candidate_node_t, 5).protocol_candidate->protocol_impl->name, "UDP");

    free_candidate_array(candidates);
}

TEST_F(CandidateGatheringTest, GivesNoCandidateNodesWhenAllProtocolsProhibited) {
    ct_tp_set_sel_prop_preference(props, RELIABILITY, PROHIBIT);
    ct_tp_set_sel_prop_preference(props, PRESERVE_MSG_BOUNDARIES, REQUIRE);
    ct_tp_set_sel_prop_preference(props, CONGESTION_CONTROL, REQUIRE);

    BuildPreconnection();

    GArray* candidates = get_ordered_candidate_nodes(preconnection);
    ASSERT_NE(candidates, nullptr);
    ASSERT_EQ(candidates->len, 0);

    free_candidate_array(candidates);
}

TEST_F(CandidateGatheringTest, AlpnIsOnlySetWhenSupportedByProtocol) {
    ct_security_parameters_t* sec = ct_security_parameters_new();
    const char* alpns[] = { "simple-ping", "complicated-ping" };
    ct_sec_param_set_property_string_array(sec, ALPN, alpns, 2);

    BuildPreconnection(sec);
    ct_sec_param_free(sec);

    GArray* candidates = get_ordered_candidate_nodes(preconnection);
    // QUIC (ALPN): 2 local * 2 ALPNs × 1 protos = 4
    // TCP/QUIC (no ALPN):  2 local * 2 protos = 4
    ASSERT_EQ(candidates->len, 8);

    std::map<std::pair<std::string, std::string>, int> alpn_counts;
    for (guint i = 0; i < candidates->len; i++) {
        ct_candidate_node_t c = g_array_index(candidates, ct_candidate_node_t, i);
        if (c.protocol_candidate->protocol_impl->supports_alpn) {
            ASSERT_NE(c.protocol_candidate->alpn, nullptr);
            std::string alpn = c.protocol_candidate->alpn;
            ASSERT_TRUE(alpn == "simple-ping" || alpn == "complicated-ping");
            std::pair<std::string, std::string> key = {c.protocol_candidate->protocol_impl->name, alpn};
            alpn_counts[key]++;
        } else {
            ASSERT_EQ(c.protocol_candidate->alpn, nullptr);
        }
    }
    std::pair<std::string, std::string> quic_simple = {"QUIC", "simple-ping"};
    std::pair<std::string, std::string> quic_complicated = {"QUIC", "complicated-ping"};
    ASSERT_EQ(alpn_counts[quic_simple], 2); // One QUIC with a given ALPN per local endpoint
    ASSERT_EQ(alpn_counts[quic_complicated], 2);

    free_candidate_array(candidates);
}

#include <gmock/gmock-matchers.h>

#include "gtest/gtest.h"
extern "C" {
#include "ctaps.h"
#include "ctaps_internal.h"
}

// =============================================================================
// Creation and Destruction Tests
// =============================================================================

TEST(SecurityParametersTest, newReturnsNonNull) {
    ct_security_parameters_t* params = ct_security_parameters_new();

    ASSERT_NE(params, nullptr);

    ct_sec_param_free(params);
}

TEST(SecurityParametersTest, newInitializesWithDefaultValues) {
    ct_security_parameters_t* params = ct_security_parameters_new();
    ASSERT_NE(params, nullptr);

    // All parameters should have set_by_user = false initially
    for (int i = 0; i < SEC_PROPERTY_END; i++) {
        EXPECT_FALSE(params->security_parameters[i].set_by_user);
    }

    ct_sec_param_free(params);
}

TEST(SecurityParametersTest, newInitializesCorrectNames) {
    ct_security_parameters_t* params = ct_security_parameters_new();
    ASSERT_NE(params, nullptr);

    EXPECT_STREQ(params->security_parameters[SUPPORTED_GROUP].name, "supportedGroup");
    EXPECT_STREQ(params->security_parameters[CIPHERSUITE].name, "ciphersuite");
    EXPECT_STREQ(params->security_parameters[SERVER_CERTIFICATE].name, "serverCertificate");
    EXPECT_STREQ(params->security_parameters[CLIENT_CERTIFICATE].name, "clientCertificate");
    EXPECT_STREQ(params->security_parameters[SIGNATURE_ALGORITHM].name, "signatureAlgorithm");
    EXPECT_STREQ(params->security_parameters[ALPN].name, "alpn");
    EXPECT_STREQ(params->security_parameters[TICKET_STORE_PATH].name, "ticketStorePath");

    ct_sec_param_free(params);
}

TEST(SecurityParametersTest, newInitializesCorrectTypes) {
    ct_security_parameters_t* params = ct_security_parameters_new();
    ASSERT_NE(params, nullptr);

    EXPECT_EQ(params->security_parameters[SUPPORTED_GROUP].type, TYPE_STRING_ARRAY);
    EXPECT_EQ(params->security_parameters[CIPHERSUITE].type, TYPE_STRING_ARRAY);
    EXPECT_EQ(params->security_parameters[SERVER_CERTIFICATE].type, TYPE_CERTIFICATE_BUNDLES);
    EXPECT_EQ(params->security_parameters[CLIENT_CERTIFICATE].type, TYPE_CERTIFICATE_BUNDLES);
    EXPECT_EQ(params->security_parameters[SIGNATURE_ALGORITHM].type, TYPE_STRING_ARRAY);
    EXPECT_EQ(params->security_parameters[ALPN].type, TYPE_STRING_ARRAY);
    EXPECT_EQ(params->security_parameters[TICKET_STORE_PATH].type, TYPE_STRING);

    ct_sec_param_free(params);
}

TEST(SecurityParametersTest, freeHandlesNullPointer) {
    ct_sec_param_free(nullptr);
    // Test passes if no crash occurs
}

// =============================================================================
// String Array Property Setter Tests
// =============================================================================

TEST(SecurityParametersTest, setAlpnSetsCorrectValue) {
    ct_security_parameters_t* params = ct_security_parameters_new();
    ASSERT_NE(params, nullptr);

    const char* alpn_strings[] = {(char*)"h2", (char*)"http/1.1"};
    int result = ct_sec_param_set_property_string_array(params, ALPN, alpn_strings, 2);

    EXPECT_EQ(result, 0);
    EXPECT_TRUE(params->security_parameters[ALPN].set_by_user);
    ASSERT_NE(params->security_parameters[ALPN].value.array_of_strings, nullptr);
    EXPECT_EQ(params->security_parameters[ALPN].value.array_of_strings->num_strings, 2);
    EXPECT_STREQ(params->security_parameters[ALPN].value.array_of_strings->strings[0], "h2");
    EXPECT_STREQ(params->security_parameters[ALPN].value.array_of_strings->strings[1], "http/1.1");

    ct_sec_param_free(params);
}

TEST(SecurityParametersTest, setCiphersuiteSetsCorrectValue) {
    ct_security_parameters_t* params = ct_security_parameters_new();
    ASSERT_NE(params, nullptr);

    const char* ciphersuite_strings[] = {(char*)"TLS_AES_128_GCM_SHA256", (char*)"TLS_AES_256_GCM_SHA384"};
    int result = ct_sec_param_set_property_string_array(params, CIPHERSUITE, ciphersuite_strings, 2);

    EXPECT_EQ(result, 0);
    EXPECT_TRUE(params->security_parameters[CIPHERSUITE].set_by_user);
    ASSERT_NE(params->security_parameters[CIPHERSUITE].value.array_of_strings, nullptr);
    EXPECT_EQ(params->security_parameters[CIPHERSUITE].value.array_of_strings->num_strings, 2);
    EXPECT_STREQ(params->security_parameters[CIPHERSUITE].value.array_of_strings->strings[0], "TLS_AES_128_GCM_SHA256");
    EXPECT_STREQ(params->security_parameters[CIPHERSUITE].value.array_of_strings->strings[1], "TLS_AES_256_GCM_SHA384");

    ct_sec_param_free(params);
}

TEST(SecurityParametersTest, setSupportedGroupSetsCorrectValue) {
    ct_security_parameters_t* params = ct_security_parameters_new();
    ASSERT_NE(params, nullptr);

    const char* group_strings[] = {(char*)"x25519", (char*)"secp256r1", (char*)"secp384r1"};
    int result = ct_sec_param_set_property_string_array(params, SUPPORTED_GROUP, group_strings, 3);

    EXPECT_EQ(result, 0);
    EXPECT_TRUE(params->security_parameters[SUPPORTED_GROUP].set_by_user);
    ASSERT_NE(params->security_parameters[SUPPORTED_GROUP].value.array_of_strings, nullptr);
    EXPECT_EQ(params->security_parameters[SUPPORTED_GROUP].value.array_of_strings->num_strings, 3);
    EXPECT_STREQ(params->security_parameters[SUPPORTED_GROUP].value.array_of_strings->strings[0], "x25519");
    EXPECT_STREQ(params->security_parameters[SUPPORTED_GROUP].value.array_of_strings->strings[1], "secp256r1");
    EXPECT_STREQ(params->security_parameters[SUPPORTED_GROUP].value.array_of_strings->strings[2], "secp384r1");

    ct_sec_param_free(params);
}

TEST(SecurityParametersTest, setSignatureAlgorithmSetsCorrectValue) {
    ct_security_parameters_t* params = ct_security_parameters_new();
    ASSERT_NE(params, nullptr);

    const char* sig_strings[] = {(char*)"ecdsa_secp256r1_sha256", (char*)"rsa_pss_rsae_sha256"};
    int result = ct_sec_param_set_property_string_array(params, SIGNATURE_ALGORITHM, sig_strings, 2);

    EXPECT_EQ(result, 0);
    EXPECT_TRUE(params->security_parameters[SIGNATURE_ALGORITHM].set_by_user);
    ASSERT_NE(params->security_parameters[SIGNATURE_ALGORITHM].value.array_of_strings, nullptr);
    EXPECT_EQ(params->security_parameters[SIGNATURE_ALGORITHM].value.array_of_strings->num_strings, 2);
    EXPECT_STREQ(params->security_parameters[SIGNATURE_ALGORITHM].value.array_of_strings->strings[0], "ecdsa_secp256r1_sha256");
    EXPECT_STREQ(params->security_parameters[SIGNATURE_ALGORITHM].value.array_of_strings->strings[1], "rsa_pss_rsae_sha256");

    ct_sec_param_free(params);
}

TEST(SecurityParametersTest, setStringArrayWithSingleElement) {
    ct_security_parameters_t* params = ct_security_parameters_new();
    ASSERT_NE(params, nullptr);

    const char* alpn_strings[] = {(char*)"h3"};
    int result = ct_sec_param_set_property_string_array(params, ALPN, alpn_strings, 1);

    EXPECT_EQ(result, 0);
    ASSERT_NE(params->security_parameters[ALPN].value.array_of_strings, nullptr);
    EXPECT_EQ(params->security_parameters[ALPN].value.array_of_strings->num_strings, 1);
    EXPECT_STREQ(params->security_parameters[ALPN].value.array_of_strings->strings[0], "h3");

    ct_sec_param_free(params);
}

TEST(SecurityParametersTest, setStringArrayOverwritesPreviousValue) {
    ct_security_parameters_t* params = ct_security_parameters_new();
    ASSERT_NE(params, nullptr);

    const char* alpn_strings1[] = {(char*)"h2"};
    ct_sec_param_set_property_string_array(params, ALPN, alpn_strings1, 1);

    const char* alpn_strings2[] = {(char*)"h3", (char*)"h2"};
    int result = ct_sec_param_set_property_string_array(params, ALPN, alpn_strings2, 2);

    EXPECT_EQ(result, 0);
    EXPECT_EQ(params->security_parameters[ALPN].value.array_of_strings->num_strings, 2);
    EXPECT_STREQ(params->security_parameters[ALPN].value.array_of_strings->strings[0], "h3");
    EXPECT_STREQ(params->security_parameters[ALPN].value.array_of_strings->strings[1], "h2");

    ct_sec_param_free(params);
}

TEST(SecurityParametersTest, setStringArrayReturnsErrorForInvalidProperty) {
    ct_security_parameters_t* params = ct_security_parameters_new();
    ASSERT_NE(params, nullptr);

    const char* strings[] = {(char*)"test"};
    int result = ct_sec_param_set_property_string_array(params, SEC_PROPERTY_END, strings, 1);

    EXPECT_NE(result, 0);

    ct_sec_param_free(params);
}

TEST(SecurityParametersTest, setStringArrayReturnsErrorForWrongType) {
    ct_security_parameters_t* params = ct_security_parameters_new();
    ASSERT_NE(params, nullptr);

    // SERVER_CERTIFICATE is TYPE_CERTIFICATE_BUNDLES, not TYPE_STRING_ARRAY
    const char* strings[] = {(char*)"test"};
    int result = ct_sec_param_set_property_string_array(params, SERVER_CERTIFICATE, strings, 1);

    EXPECT_NE(result, 0);

    ct_sec_param_free(params);
}

// =============================================================================
// Certificate Bundles Setter Tests
// =============================================================================

TEST(SecurityParametersTest, setServerCertificateSetsCorrectValue) {
    ct_security_parameters_t* params = ct_security_parameters_new();
    ASSERT_NE(params, nullptr);

    ct_certificate_bundles_t* bundles = ct_certificate_bundles_new();
    ASSERT_NE(bundles, nullptr);
    ct_certificate_bundles_add_cert(bundles, "/path/to/cert.pem", "/path/to/key.pem");

    int result = ct_sec_param_set_property_certificate_bundles(params, SERVER_CERTIFICATE, bundles);

    EXPECT_EQ(result, 0);
    EXPECT_TRUE(params->security_parameters[SERVER_CERTIFICATE].set_by_user);
    ASSERT_NE(params->security_parameters[SERVER_CERTIFICATE].value.certificate_bundles, nullptr);
    EXPECT_EQ(params->security_parameters[SERVER_CERTIFICATE].value.certificate_bundles->num_bundles, 1);

    ct_certificate_bundles_free(bundles);
    ct_sec_param_free(params);
}

TEST(SecurityParametersTest, setClientCertificateSetsCorrectValue) {
    ct_security_parameters_t* params = ct_security_parameters_new();
    ASSERT_NE(params, nullptr);

    ct_certificate_bundles_t* bundles = ct_certificate_bundles_new();
    ASSERT_NE(bundles, nullptr);
    ct_certificate_bundles_add_cert(bundles, "/path/to/client_cert.pem", "/path/to/client_key.pem");

    int result = ct_sec_param_set_property_certificate_bundles(params, CLIENT_CERTIFICATE, bundles);

    EXPECT_EQ(result, 0);
    EXPECT_TRUE(params->security_parameters[CLIENT_CERTIFICATE].set_by_user);
    ASSERT_NE(params->security_parameters[CLIENT_CERTIFICATE].value.certificate_bundles, nullptr);
    EXPECT_EQ(params->security_parameters[CLIENT_CERTIFICATE].value.certificate_bundles->num_bundles, 1);

    ct_certificate_bundles_free(bundles);
    ct_sec_param_free(params);
}

TEST(SecurityParametersTest, setCertificateBundlesWithMultipleBundles) {
    GTEST_SKIP(); // Multiple certificate bundles not yet supported in ct_certificate_bundles_add_cert
    ct_security_parameters_t* params = ct_security_parameters_new();
    ASSERT_NE(params, nullptr);

    ct_certificate_bundles_t* bundles = ct_certificate_bundles_new();
    ASSERT_NE(bundles, nullptr);
    ct_certificate_bundles_add_cert(bundles, "/path/to/cert1.pem", "/path/to/key1.pem");
    ct_certificate_bundles_add_cert(bundles, "/path/to/cert2.pem", "/path/to/key2.pem");

    int result = ct_sec_param_set_property_certificate_bundles(params, SERVER_CERTIFICATE, bundles);

    EXPECT_EQ(result, 0);
    EXPECT_EQ(params->security_parameters[SERVER_CERTIFICATE].value.certificate_bundles->num_bundles, 2);

    ct_certificate_bundles_free(bundles);
    ct_sec_param_free(params);
}

TEST(SecurityParametersTest, setCertificateBundlesOverwritesPreviousValue) {
    ct_security_parameters_t* params = ct_security_parameters_new();
    ASSERT_NE(params, nullptr);

    ct_certificate_bundles_t* bundles1 = ct_certificate_bundles_new();
    ct_certificate_bundles_add_cert(bundles1, "/path/to/cert1.pem", "/path/to/key1.pem");
    ct_sec_param_set_property_certificate_bundles(params, SERVER_CERTIFICATE, bundles1);

    ct_certificate_bundles_t* bundles2 = ct_certificate_bundles_new();
    ct_certificate_bundles_add_cert(bundles2, "/path/to/cert2.pem", "/path/to/key2.pem");

    int result = ct_sec_param_set_property_certificate_bundles(params, SERVER_CERTIFICATE, bundles2);

    EXPECT_EQ(result, 0);
    EXPECT_EQ(params->security_parameters[SERVER_CERTIFICATE].value.certificate_bundles->num_bundles, 1);
    EXPECT_STREQ(params->security_parameters[SERVER_CERTIFICATE].value.certificate_bundles->certificate_bundles[0].certificate_file_name, "/path/to/cert2.pem");

    ct_certificate_bundles_free(bundles1);
    ct_certificate_bundles_free(bundles2);
    ct_sec_param_free(params);
}

TEST(SecurityParametersTest, setCertificateBundlesReturnsErrorForInvalidProperty) {
    ct_security_parameters_t* params = ct_security_parameters_new();
    ASSERT_NE(params, nullptr);

    ct_certificate_bundles_t* bundles = ct_certificate_bundles_new();
    ct_certificate_bundles_add_cert(bundles, "/path/to/cert.pem", "/path/to/key.pem");

    int result = ct_sec_param_set_property_certificate_bundles(params, SEC_PROPERTY_END, bundles);

    EXPECT_NE(result, 0);

    ct_certificate_bundles_free(bundles);
    ct_sec_param_free(params);
}

TEST(SecurityParametersTest, setCertificateBundlesReturnsErrorForWrongType) {
    ct_security_parameters_t* params = ct_security_parameters_new();
    ASSERT_NE(params, nullptr);

    ct_certificate_bundles_t* bundles = ct_certificate_bundles_new();
    ct_certificate_bundles_add_cert(bundles, "/path/to/cert.pem", "/path/to/key.pem");

    // ALPN is TYPE_STRING_ARRAY, not TYPE_CERTIFICATE_BUNDLES
    int result = ct_sec_param_set_property_certificate_bundles(params, ALPN, bundles);

    EXPECT_NE(result, 0);

    ct_certificate_bundles_free(bundles);
    ct_sec_param_free(params);
}

TEST(SecurityParametersTest, setCertificateBundlesReturnsErrorForNullBundles) {
    ct_security_parameters_t* params = ct_security_parameters_new();
    ASSERT_NE(params, nullptr);

    int result = ct_sec_param_set_property_certificate_bundles(params, SERVER_CERTIFICATE, nullptr);

    EXPECT_NE(result, 0);

    ct_sec_param_free(params);
}

// =============================================================================
// Ticket Store Path Setter Tests
// =============================================================================

TEST(SecurityParametersTest, setTicketStorePathSetsCorrectValue) {
    ct_security_parameters_t* params = ct_security_parameters_new();
    ASSERT_NE(params, nullptr);

    int result = ct_sec_param_set_ticket_store_path(params, "/path/to/tickets.bin");

    EXPECT_EQ(result, 0);
    EXPECT_TRUE(params->security_parameters[TICKET_STORE_PATH].set_by_user);
    ASSERT_NE(params->security_parameters[TICKET_STORE_PATH].value.string, nullptr);
    EXPECT_STREQ(params->security_parameters[TICKET_STORE_PATH].value.string, "/path/to/tickets.bin");

    ct_sec_param_free(params);
}

TEST(SecurityParametersTest, setTicketStorePathOverwritesPreviousValue) {
    ct_security_parameters_t* params = ct_security_parameters_new();
    ASSERT_NE(params, nullptr);

    ct_sec_param_set_ticket_store_path(params, "/path/to/old_tickets.bin");
    int result = ct_sec_param_set_ticket_store_path(params, "/path/to/new_tickets.bin");

    EXPECT_EQ(result, 0);
    EXPECT_STREQ(params->security_parameters[TICKET_STORE_PATH].value.string, "/path/to/new_tickets.bin");

    ct_sec_param_free(params);
}

TEST(SecurityParametersTest, setTicketStorePathAcceptsNullToClear) {
    ct_security_parameters_t* params = ct_security_parameters_new();
    ASSERT_NE(params, nullptr);

    ct_sec_param_set_ticket_store_path(params, "/path/to/tickets.bin");
    int result = ct_sec_param_set_ticket_store_path(params, nullptr);

    EXPECT_EQ(result, 0);
    EXPECT_TRUE(params->security_parameters[TICKET_STORE_PATH].set_by_user);
    EXPECT_EQ(params->security_parameters[TICKET_STORE_PATH].value.string, nullptr);

    ct_sec_param_free(params);
}

TEST(SecurityParametersTest, setTicketStorePathHandlesNullSecurityParameters) {
    int result = ct_sec_param_set_ticket_store_path(nullptr, "/path/to/tickets.bin");

    EXPECT_NE(result, 0);
}

TEST(SecurityParametersTest, setTicketStorePathWithEmptyString) {
    ct_security_parameters_t* params = ct_security_parameters_new();
    ASSERT_NE(params, nullptr);

    int result = ct_sec_param_set_ticket_store_path(params, "");

    EXPECT_EQ(result, 0);
    EXPECT_TRUE(params->security_parameters[TICKET_STORE_PATH].set_by_user);
    EXPECT_STREQ(params->security_parameters[TICKET_STORE_PATH].value.string, "");

    ct_sec_param_free(params);
}

#include <gmock/gmock-matchers.h>

#include "gtest/gtest.h"
extern "C" {
#include "ctaps.h"
#include "ctaps_internal.h"
#include "security_parameter/security_parameters.h"
}

// =============================================================================
// Creation and Destruction Tests
// =============================================================================

TEST(SecurityParametersTest, newReturnsNonNull) {
    ct_security_parameters_t* params = ct_security_parameters_new();

    ASSERT_NE(params, nullptr);

    ct_security_parameters_free(params);
}

TEST(SecurityParametersTest, newInitializesWithDefaultValues) {
    ct_security_parameters_t* params = ct_security_parameters_new();
    ASSERT_NE(params, nullptr);

    // All parameters should have set_by_user = false initially
    for (int i = 0; i < SEC_PROPERTY_END; i++) {
        EXPECT_FALSE(params->list[i].set_by_user);
    }

    ct_security_parameters_free(params);
}

TEST(SecurityParametersTest, newInitializesCorrectNames) {
    ct_security_parameters_t* params = ct_security_parameters_new();
    ASSERT_NE(params, nullptr);

    EXPECT_STREQ(params->list[SUPPORTED_GROUP].name, "supportedGroup");
    EXPECT_STREQ(params->list[CIPHERSUITE].name, "ciphersuite");
    EXPECT_STREQ(params->list[SERVER_CERTIFICATE].name, "serverCertificate");
    EXPECT_STREQ(params->list[CLIENT_CERTIFICATE].name, "clientCertificate");
    EXPECT_STREQ(params->list[SIGNATURE_ALGORITHM].name, "signatureAlgorithm");
    EXPECT_STREQ(params->list[ALPN].name, "alpn");
    EXPECT_STREQ(params->list[TICKET_STORE_PATH].name, "ticketStorePath");
    EXPECT_STREQ(params->list[SERVER_NAME_IDENTIFICATION].name, "serverNameIdentification");
    EXPECT_STREQ(params->list[SESSION_TICKET_ENCRYPTION_KEY].name, "sessionTicketEncryptionKey");


    ct_security_parameters_free(params);
}

TEST(SecurityParametersTest, newInitializesCorrectTypes) {
    ct_security_parameters_t* params = ct_security_parameters_new();
    ASSERT_NE(params, nullptr);

    EXPECT_EQ(params->list[SUPPORTED_GROUP].type, TYPE_STRING_ARRAY);
    EXPECT_EQ(params->list[CIPHERSUITE].type, TYPE_STRING_ARRAY);
    EXPECT_EQ(params->list[SERVER_CERTIFICATE].type, TYPE_CERTIFICATE_BUNDLES);
    EXPECT_EQ(params->list[CLIENT_CERTIFICATE].type, TYPE_CERTIFICATE_BUNDLES);
    EXPECT_EQ(params->list[SIGNATURE_ALGORITHM].type, TYPE_STRING_ARRAY);
    EXPECT_EQ(params->list[ALPN].type, TYPE_STRING_ARRAY);
    EXPECT_EQ(params->list[TICKET_STORE_PATH].type, TYPE_STRING);
    EXPECT_EQ(params->list[SESSION_TICKET_ENCRYPTION_KEY].type, TYPE_BYTE_ARRAY);
    EXPECT_EQ(params->list[SERVER_NAME_IDENTIFICATION].type, TYPE_STRING);

    ct_security_parameters_free(params);
}

TEST(SecurityParametersTest, freeHandlesNullPointer) {
    ct_security_parameters_free(nullptr);
    // Test passes if no crash occurs
}

// =============================================================================
// String Array Property Setter Tests
// =============================================================================

TEST(SecurityParametersTest, setAlpnSetsCorrectValue) {
    ct_security_parameters_t* params = ct_security_parameters_new();
    ASSERT_NE(params, nullptr);

    const char* alpn_strings[] = {(char*)"h2", (char*)"http/1.1"};
    int result = ct_security_parameters_add_alpn(params, "h2");
    EXPECT_EQ(result, 0);
    result = ct_security_parameters_add_alpn(params, "http/1.1");


    EXPECT_TRUE(params->list[ALPN].set_by_user);
    ASSERT_NE(params->list[ALPN].value.array_of_strings.strings, nullptr);
    EXPECT_EQ(params->list[ALPN].value.array_of_strings.num_strings, 2);
    EXPECT_STREQ(params->list[ALPN].value.array_of_strings.strings[0], "h2");
    EXPECT_STREQ(params->list[ALPN].value.array_of_strings.strings[1], "http/1.1");

    ct_security_parameters_free(params);
}

TEST(SecurityParametersTest, clearAlpnClearsPreviousValue) {
    ct_security_parameters_t* params = ct_security_parameters_new();
    ASSERT_NE(params, nullptr);

    ct_security_parameters_add_alpn(params, "h2");
    
    size_t num_alpn = 0;
    const char** alpns = ct_security_parameters_get_alpns(params, &num_alpn);

    ASSERT_EQ(num_alpn, 1);
    ASSERT_STREQ(alpns[0], "h2");

    int result = ct_security_parameters_clear_alpn(params);

    alpns = ct_security_parameters_get_alpns(params, &num_alpn);

    ASSERT_EQ(num_alpn, 0);
    ASSERT_EQ(alpns, nullptr);

    ct_security_parameters_free(params);
}

TEST(SecurityParametersTest, clearAlpnHandlesNullPointer) {
    int result = ct_security_parameters_clear_alpn(nullptr);

    ASSERT_NE(result, 0);
}

// =============================================================================
// Certificate Bundles Setter Tests
// =============================================================================

TEST(SecurityParametersTest, setServerCertificateSetsCorrectValue) {
    ct_security_parameters_t* params = ct_security_parameters_new();
    ASSERT_NE(params, nullptr);

    int rc = ct_security_parameters_add_server_certificate(params, "test.c", "test.b");

    ASSERT_EQ(rc, 0);

    int count = ct_security_parameters_get_server_certificate_count(params);
    ASSERT_EQ(count, 1);

    const char* certificate_file = ct_security_parameters_get_server_certificate_file(params, 0);
    const char* key_file = ct_security_parameters_get_server_certificate_key_file(params, 0);

    ASSERT_STREQ(certificate_file, "test.c");
    ASSERT_STREQ(key_file, "test.b");

    ct_security_parameters_free(params);
}

TEST(SecurityParametersTest, setClientCertificateSetsCorrectValue) {
    ct_security_parameters_t* params = ct_security_parameters_new();
    ASSERT_NE(params, nullptr);

    int rc = ct_security_parameters_add_client_certificate(params, "test.c", "test.b");

    ASSERT_EQ(rc, 0);

    int count = ct_security_parameters_get_client_certificate_count(params);
    ASSERT_EQ(count, 1);

    const char* certificate_file = ct_security_parameters_get_client_certificate_file(params, 0);
    const char* key_file = ct_security_parameters_get_client_certificate_key_file(params, 0);

    ASSERT_STREQ(certificate_file, "test.c");
    ASSERT_STREQ(key_file, "test.b");

    ct_security_parameters_free(params);
}

// =============================================================================
// Ticket Store Path Setter Tests
// =============================================================================

TEST(SecurityParametersTest, setTicketStorePathSetsCorrectValue) {
    ct_security_parameters_t* params = ct_security_parameters_new();
    ASSERT_NE(params, nullptr);

    int result = ct_security_parameters_set_ticket_store_path(params, "/path/to/tickets.bin");

    EXPECT_EQ(result, 0);
    EXPECT_TRUE(params->list[TICKET_STORE_PATH].set_by_user);
    ASSERT_NE(params->list[TICKET_STORE_PATH].value.string, nullptr);
    EXPECT_STREQ(params->list[TICKET_STORE_PATH].value.string, "/path/to/tickets.bin");

    ct_security_parameters_free(params);
}

TEST(SecurityParametersTest, setTicketStorePathOverwritesPreviousValue) {
    ct_security_parameters_t* params = ct_security_parameters_new();
    ASSERT_NE(params, nullptr);

    ct_security_parameters_set_ticket_store_path(params, "/path/to/old_tickets.bin");
    int result = ct_security_parameters_set_ticket_store_path(params, "/path/to/new_tickets.bin");

    EXPECT_EQ(result, 0);
    EXPECT_STREQ(params->list[TICKET_STORE_PATH].value.string, "/path/to/new_tickets.bin");

    ct_security_parameters_free(params);
}

TEST(SecurityParametersTest, setTicketStorePathAcceptsNullToClear) {
    ct_security_parameters_t* params = ct_security_parameters_new();
    ASSERT_NE(params, nullptr);

    ct_security_parameters_set_ticket_store_path(params, "/path/to/tickets.bin");
    int result = ct_security_parameters_set_ticket_store_path(params, nullptr);

    EXPECT_EQ(result, 0);
    EXPECT_TRUE(params->list[TICKET_STORE_PATH].set_by_user);
    EXPECT_EQ(params->list[TICKET_STORE_PATH].value.string, nullptr);

    ct_security_parameters_free(params);
}

TEST(SecurityParametersTest, setTicketStorePathHandlesNullSecurityParameters) {
    int result = ct_security_parameters_set_ticket_store_path(nullptr, "/path/to/tickets.bin");

    EXPECT_NE(result, 0);
}

TEST(SecurityParametersTest, setTicketStorePathWithEmptyString) {
    ct_security_parameters_t* params = ct_security_parameters_new();
    ASSERT_NE(params, nullptr);

    int result = ct_security_parameters_set_ticket_store_path(params, "");

    EXPECT_EQ(result, 0);
    EXPECT_TRUE(params->list[TICKET_STORE_PATH].set_by_user);
    EXPECT_STREQ(params->list[TICKET_STORE_PATH].value.string, "");

    ct_security_parameters_free(params);
}

// =============================================================================
// Server name identification Setter Tests
// =============================================================================

TEST(SecurityParametersTest, setServerNameIdentificationSetsCorrectValue) {
    ct_security_parameters_t* params = ct_security_parameters_new();
    ASSERT_NE(params, nullptr);

    int result = ct_security_parameters_set_server_name_identification(params, "localhost123");

    EXPECT_EQ(result, 0);
    EXPECT_TRUE(params->list[SERVER_NAME_IDENTIFICATION].set_by_user);
    EXPECT_STREQ(params->list[SERVER_NAME_IDENTIFICATION].value.string, "localhost123");

    ct_security_parameters_free(params);
}

TEST(SecurityParametersTest, setServerNameIdentificationOverwritesPreviousValue) {
    ct_security_parameters_t* params = ct_security_parameters_new();

    ct_security_parameters_set_server_name_identification(params, "localhost123");
    EXPECT_STREQ(params->list[SERVER_NAME_IDENTIFICATION].value.string, "localhost123");
    int result = ct_security_parameters_set_server_name_identification(params, "localhost321");

    EXPECT_EQ(result, 0);
    EXPECT_STREQ(params->list[SERVER_NAME_IDENTIFICATION].value.string, "localhost321");

    ct_security_parameters_free(params);
}

TEST(SecurityParametersTest, setServerNameIdentificationAcceptsNullToClear) {
    ct_security_parameters_t* params = ct_security_parameters_new();

    ct_security_parameters_set_server_name_identification(params, "localhost");
    EXPECT_STREQ(params->list[SERVER_NAME_IDENTIFICATION].value.string, "localhost");
    int result = ct_security_parameters_set_server_name_identification(params, nullptr);

    EXPECT_EQ(result, 0);
    EXPECT_TRUE(params->list[SERVER_NAME_IDENTIFICATION].set_by_user);
    EXPECT_EQ(params->list[SERVER_NAME_IDENTIFICATION].value.string, nullptr);

    ct_security_parameters_free(params);
}

TEST(SecurityParametersTest, setServerNameIdentificationHandlesNullSecurityParameters) {
    int result = ct_security_parameters_set_server_name_identification(nullptr, "localhost");

    EXPECT_NE(result, 0);
}

TEST(SecurityParametersTest, setServerNameIdentificationWithEmptyString) {
    ct_security_parameters_t* params = ct_security_parameters_new();
    ASSERT_NE(params, nullptr);

    int result = ct_security_parameters_set_server_name_identification(params, "");

    EXPECT_EQ(result, 0);
    EXPECT_TRUE(params->list[SERVER_NAME_IDENTIFICATION].set_by_user);
    EXPECT_STREQ(params->list[SERVER_NAME_IDENTIFICATION].value.string, "");

    ct_security_parameters_free(params);
}

TEST(SecurityParametersTest, setServerNameIdentificationTakesDeepCopy) {
    ct_security_parameters_t* params = ct_security_parameters_new();

    char sni[] = "abcdef";
    int result = ct_security_parameters_set_server_name_identification(params, "abcdef");
    EXPECT_STREQ(params->list[SERVER_NAME_IDENTIFICATION].value.string, "abcdef");
    sni[0] = 'x'; // Modify original string after setting

    EXPECT_TRUE(params->list[SERVER_NAME_IDENTIFICATION].set_by_user);
    EXPECT_STREQ(params->list[SERVER_NAME_IDENTIFICATION].value.string, "abcdef");

    ct_security_parameters_free(params);
}

TEST(SecurityParametersTest, CopiesServerNameIdentification) {
    ct_security_parameters_t* src = ct_security_parameters_new();
    ct_security_parameters_set_server_name_identification(src, "example.com");

    ct_security_parameters_t* copy = ct_security_parameters_deep_copy(src);
    ASSERT_NE(copy, nullptr);
    EXPECT_STREQ(copy->list[SERVER_NAME_IDENTIFICATION].value.string, "example.com");
    // Must be a distinct allocation
    EXPECT_NE(copy->list[SERVER_NAME_IDENTIFICATION].value.string,
              src->list[SERVER_NAME_IDENTIFICATION].value.string);
    EXPECT_TRUE(copy->list[SERVER_NAME_IDENTIFICATION].set_by_user);

    ct_security_parameters_free(src);
    ct_security_parameters_free(copy);
}

TEST(SecurityParametersTest, CopiesAlpn) {
    ct_security_parameters_t* src = ct_security_parameters_new();
    ct_security_parameters_add_alpn(src, "h2");
    ct_security_parameters_add_alpn(src, "http/1.1");

    ct_security_parameters_t* copy = ct_security_parameters_deep_copy(src);
    ASSERT_NE(copy, nullptr);

    size_t num_alpns = 0;
    const char** alpns = ct_security_parameters_get_alpns(copy, &num_alpns);
    ASSERT_EQ(num_alpns, 2);
    EXPECT_STREQ(alpns[0], "h2");
    EXPECT_STREQ(alpns[1], "http/1.1");
    // Must be distinct allocations
    EXPECT_NE(alpns, ct_security_parameters_get_alpns(src, &num_alpns));

    ct_security_parameters_free(src);
    ct_security_parameters_free(copy);
}

TEST(SecurityParametersTest, CopiesEmptyStringArray) {
    // Bug 1: empty array should not be treated as failure
    ct_security_parameters_t* src = ct_security_parameters_new();
    // set_by_user = true but empty array
    src->list[ALPN].set_by_user = true;
    src->list[ALPN].value.array_of_strings.strings = nullptr;
    src->list[ALPN].value.array_of_strings.num_strings = 0;

    ct_security_parameters_t* copy = ct_security_parameters_deep_copy(src);
    ASSERT_NE(copy, nullptr);
    EXPECT_EQ(copy->list[ALPN].value.array_of_strings.num_strings, 0);
    EXPECT_TRUE(copy->list[ALPN].set_by_user);

    ct_security_parameters_free(src);
    ct_security_parameters_free(copy);
}

TEST(SecurityParametersTest, CopiesSessionTicketEncryptionKey) {
    ct_security_parameters_t* src = ct_security_parameters_new();
    uint8_t key[] = {0x01, 0x02, 0x03, 0x04};
    ct_security_parameters_set_session_ticket_encryption_key(src, key, sizeof(key));

    ct_security_parameters_t* copy = ct_security_parameters_deep_copy(src);
    ASSERT_NE(copy, nullptr);

    size_t key_len = 0;
    const uint8_t* copied_key = ct_security_parameters_get_session_ticket_encryption_key(copy, &key_len);
    ASSERT_EQ(key_len, sizeof(key));
    EXPECT_EQ(memcmp(copied_key, key, key_len), 0);
    EXPECT_NE(copied_key, key);

    ct_security_parameters_free(src);
    ct_security_parameters_free(copy);
}

TEST(SecurityParametersTest, CopiesZeroLengthByteArray) {
    // Bug 2: zero-length byte array should not trigger error path
    ct_security_parameters_t* src = ct_security_parameters_new();
    src->list[SESSION_TICKET_ENCRYPTION_KEY].set_by_user = true;
    src->list[SESSION_TICKET_ENCRYPTION_KEY].value.byte_array.bytes = NULL;
    src->list[SESSION_TICKET_ENCRYPTION_KEY].value.byte_array.length = 0;
    src->list[SESSION_TICKET_ENCRYPTION_KEY].set_by_user = true;

    ct_security_parameters_t* copy = ct_security_parameters_deep_copy(src);
    ASSERT_NE(copy, nullptr);
    EXPECT_EQ(copy->list[SESSION_TICKET_ENCRYPTION_KEY].value.byte_array.length, 0);
    EXPECT_TRUE(copy->list[SESSION_TICKET_ENCRYPTION_KEY].set_by_user);
    ASSERT_EQ(copy->list[SESSION_TICKET_ENCRYPTION_KEY].value.byte_array.bytes, nullptr);

    ct_security_parameters_free(src);
    ct_security_parameters_free(copy);
}

TEST(SecurityParametersTest, CopiesServerCertificate) {
    ct_security_parameters_t* src = ct_security_parameters_new();
    ct_security_parameters_add_server_certificate(src, "cert.pem", "key.pem");

    ct_security_parameters_t* copy = ct_security_parameters_deep_copy(src);
    ASSERT_NE(copy, nullptr);

    ASSERT_EQ(ct_security_parameters_get_server_certificate_count(copy), 1);
    EXPECT_STREQ(ct_security_parameters_get_server_certificate_file(copy, 0), "cert.pem");
    EXPECT_STREQ(ct_security_parameters_get_server_certificate_key_file(copy, 0), "key.pem");
    // Must be distinct allocations
    EXPECT_NE(ct_security_parameters_get_server_certificate_file(copy, 0),
              ct_security_parameters_get_server_certificate_file(src, 0));

    ct_security_parameters_free(src);
    ct_security_parameters_free(copy);
}

TEST(SecurityParametersTest, NullSourceReturnsNull) {
    EXPECT_EQ(ct_security_parameters_deep_copy(nullptr), nullptr);
}

TEST(SecurityParametersTest, ModifyingSourceDoesNotAffectCopy) {
    ct_security_parameters_t* src = ct_security_parameters_new();
    ct_security_parameters_add_alpn(src, "h2");

    ct_security_parameters_t* copy = ct_security_parameters_deep_copy(src);
    ASSERT_NE(copy, nullptr);

    ct_security_parameters_add_alpn(src, "http/1.1");

    size_t num_alpns = 0;
    ct_security_parameters_get_alpns(copy, &num_alpns);
    EXPECT_EQ(num_alpns, 1);

    ct_security_parameters_free(src);
    ct_security_parameters_free(copy);
}

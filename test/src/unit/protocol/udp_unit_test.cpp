#include "gtest/gtest.h"
extern "C" {
  #include "protocol/udp/udp.h"
  #include "fff.h"
  extern void on_send(uv_udp_send_t* req, int status);
}

DEFINE_FFF_GLOBALS;
FAKE_VOID_FUNC(faked_message_send_error, ct_connection_t*, ct_message_context_t*, int);
FAKE_VOID_FUNC(faked_message_free, ct_message_t*);

extern "C" {
  void __wrap_ct_message_free(ct_message_t* message) {
    faked_message_free(message);
  }

  void __wrap_free(void* message) {
  }
}

class UdpUnitTest : public ::testing::Test {
protected:
    void SetUp() override {
        dummy_socket_manager.callbacks.message_send_error = faked_message_send_error;
        dummy_connection.socket_manager = &dummy_socket_manager;
        dummy_send_data.connection = &dummy_connection;
        dummy_send_data.message_context = &dummy_message_context;
        dummy_send_data.message = &dummy_message;
        RESET_FAKE(faked_message_send_error);
        FFF_RESET_HISTORY();
    }

    
    void TearDown() override {
    }

    ct_socket_manager_t dummy_socket_manager;
    udp_send_data_t dummy_send_data = {0};
    ct_connection_t dummy_connection;
    ct_message_t dummy_message = {0};
    ct_message_context_t dummy_message_context = {0};
};

TEST_F(UdpUnitTest, MessageSendErrorInvokedOnWriteError) {
    uv_udp_send_t* req = (uv_udp_send_t*)calloc(1, sizeof(uv_udp_send_t));
    req->data = &dummy_send_data;

    on_send(req, UV_ECONNRESET);

    ASSERT_EQ(faked_message_send_error_fake.call_count, 1);
    ASSERT_EQ(faked_message_send_error_fake.arg0_val, &dummy_connection);
    ASSERT_EQ(faked_message_send_error_fake.arg1_val, &dummy_message_context);
    ASSERT_EQ(faked_message_send_error_fake.arg2_val, UV_ECONNRESET);
    ASSERT_EQ(faked_message_free_fake.call_count, 1);
}

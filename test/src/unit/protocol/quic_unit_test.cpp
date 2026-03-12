#include "gtest/gtest.h"
extern "C" {
  #include "protocol/quic/quic.h"
  #include "fff.h"
}

DEFINE_FFF_GLOBALS;

extern "C" {

FAKE_VALUE_FUNC(int, __wrap_picoquic_set_stream_priority, picoquic_cnx_t*, uint64_t, uint8_t);

}

class QuicUnitTest : public ::testing::Test {
protected:
    void SetUp() override {
        RESET_FAKE(__wrap_picoquic_set_stream_priority);
        FFF_RESET_HISTORY();
        dummy_group_state.picoquic_connection = dummy_cnx;
        dummy_group.connection_group_state = &dummy_group_state;
        dummy_connection.connection_group = &dummy_group;

        dummy_connection.internal_connection_state = &dummy_stream_state;

        dummy_stream_state.stream_id = 123;
        dummy_stream_state.stream_initialized = true;
        
        __wrap_picoquic_set_stream_priority_fake.return_val = 0;
    }

    
    void TearDown() override {
    }

    ct_connection_t dummy_connection = {0};
    ct_connection_group_t dummy_group = {0};
    ct_quic_stream_state_t dummy_stream_state = {0};
    ct_quic_connection_group_state_t dummy_group_state = {0};
    picoquic_cnx_t* dummy_cnx = (picoquic_cnx_t*)0xdeadbeef; // Dummy pointer value for testing
};

TEST_F(QuicUnitTest, picoquicSetConnectionPriorityInvoked) {
    int rc = quic_set_connection_priority(&dummy_connection, 50);

    ASSERT_EQ(rc, 0);
    ASSERT_EQ(__wrap_picoquic_set_stream_priority_fake.call_count, 1);
    ASSERT_EQ(__wrap_picoquic_set_stream_priority_fake.arg0_val, dummy_cnx);
    ASSERT_EQ(__wrap_picoquic_set_stream_priority_fake.arg1_val, dummy_stream_state.stream_id);
    ASSERT_EQ(__wrap_picoquic_set_stream_priority_fake.arg2_val, 50);
}

TEST_F(QuicUnitTest, errorOnPicoquicSetPriorityError) {
    __wrap_picoquic_set_stream_priority_fake.return_val = 0x400; // Picoquic returns positive error codes
    int rc = quic_set_connection_priority(&dummy_connection, 50);

    ASSERT_EQ(rc, -EIO);
    ASSERT_EQ(__wrap_picoquic_set_stream_priority_fake.call_count, 1);
    ASSERT_EQ(__wrap_picoquic_set_stream_priority_fake.arg0_val, dummy_cnx);
    ASSERT_EQ(__wrap_picoquic_set_stream_priority_fake.arg1_val, dummy_stream_state.stream_id);
    ASSERT_EQ(__wrap_picoquic_set_stream_priority_fake.arg2_val, 50);
}

TEST_F(QuicUnitTest, diesOnNoPicoquicConnection) {
    dummy_connection.connection_group->connection_group_state = NULL; // No group state, so no picoquic connection
    EXPECT_DEATH(quic_set_connection_priority(&dummy_connection, 50), "");
}

TEST_F(QuicUnitTest, einvalOnNotInitializedStream) {
    dummy_stream_state.stream_initialized = false; // Stream not initialized
    int rc = quic_set_connection_priority(&dummy_connection, 50);
    EXPECT_EQ(rc, -EINVAL);
}

TEST_F(QuicUnitTest, diesOnNoStreamState) {
    dummy_stream_state = {0};
    dummy_connection.internal_connection_state = NULL; // No stream state
    EXPECT_DEATH(quic_set_connection_priority(&dummy_connection, 50), "");
}

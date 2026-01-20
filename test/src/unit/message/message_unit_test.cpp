#include <gmock/gmock-matchers.h>

#include "gtest/gtest.h"
#include "fff.h"
extern "C" {
#include "ctaps.h"
#include "ctaps_internal.h"
#include "fff.h"
}

#include "fixtures/awaiting_fixture.cpp"

TEST(MessageUnitTest, messageSetContentHandlesNullMessage) {
    ct_message_t* message = ct_message_new_with_content("hello", 5);

    ct_message_set_content(NULL, "world", 5);
    ASSERT_EQ(message->length, 5);
    ASSERT_STREQ((const char*)message->content, "hello");
    ct_message_free(message);
}

TEST(MessageUnitTest, messageSetContentHandlesNullContent) {
    ct_message_t* message = ct_message_new_with_content("hello", 5);

    ct_message_set_content(message, NULL, 0);

    ASSERT_EQ(message->length, 0);
    ASSERT_EQ(message->content, nullptr);
    ct_message_free(message);
}

TEST(MessageUnitTest, messageSetContentHandlesMessageContentAsContent) {
    ct_message_t* message = ct_message_new_with_content("hello", 5);

    ct_message_set_content(message, message->content, message->length);

    ASSERT_EQ(message->length, 5);
    ASSERT_STREQ((const char*)message->content, "hello");
    ct_message_free(message);
}

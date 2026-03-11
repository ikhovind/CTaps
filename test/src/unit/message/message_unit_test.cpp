#include <gmock/gmock-matchers.h>

#include "gtest/gtest.h"
#include "fff.h"
extern "C" {
#include "ctaps.h"
#include "ctaps_internal.h"
#include "fff.h"
}

#include "fixtures/integration_fixture.h"

TEST(MessageUnitTest, messageSetContentHandlesNullMessage) {
    ct_message_set_content(NULL, "world", strlen("world") + 1);
}

TEST(MessageUnitTest, messageSetContentHandlesNullContent) {
    ct_message_t* message = ct_message_new_with_content("hello", strlen("hello") + 1);

    ct_message_set_content(message, NULL, 0);

    ASSERT_EQ(message->length, 0);
    ASSERT_EQ(message->content, nullptr);
    ct_message_free(message);
}

TEST(MessageUnitTest, messageSetContentHandlesMessageContentAsContent) {
    ct_message_t* message = ct_message_new_with_content("hello", strlen("hello") + 1);

    ct_message_set_content(message, message->content, message->length);

    EXPECT_EQ(message->length, 6);
    EXPECT_STREQ(message->content, "hello");
    ct_message_free(message);
}

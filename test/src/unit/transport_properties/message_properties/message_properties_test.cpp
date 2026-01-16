#include <gmock/gmock-matchers.h>

#include "gtest/gtest.h"
extern "C" {
#include "ctaps.h"
#include "ctaps_internal.h"
}

TEST(MessagePropertiesUnitTests, NewInitializesWithDefaultValues) {
    ct_message_properties_t* message_properties = ct_message_properties_new();
    ASSERT_NE(message_properties, nullptr);

    // Verify default boolean properties
    EXPECT_EQ(message_properties->message_property[MSG_ORDERED].value.boolean_value, true);
    EXPECT_EQ(message_properties->message_property[MSG_SAFELY_REPLAYABLE].value.boolean_value, false);
    EXPECT_EQ(message_properties->message_property[MSG_RELIABLE].value.boolean_value, true);
    EXPECT_EQ(message_properties->message_property[NO_FRAGMENTATION].value.boolean_value, false);
    EXPECT_EQ(message_properties->message_property[NO_SEGMENTATION].value.boolean_value, false);

    // Verify default integer properties
    EXPECT_EQ(message_properties->message_property[MSG_PRIORITY].value.uint32_value, 100);
    EXPECT_EQ(message_properties->message_property[MSG_CHECKSUM_LEN].value.uint32_value, 0);

    // Verify default uint64 properties
    EXPECT_EQ(message_properties->message_property[MSG_LIFETIME].value.uint64_value, 0);

    // Verify default enum properties
    EXPECT_EQ(message_properties->message_property[MSG_CAPACITY_PROFILE].value.enum_value, CAPACITY_PROFILE_BEST_EFFORT);

    // Cleanup
    ct_message_properties_free(message_properties);
}

TEST(MessagePropertiesUnitTests, NewSetsPropertyNames) {
    ct_message_properties_t* message_properties = ct_message_properties_new();
    ASSERT_NE(message_properties, nullptr);

    EXPECT_STREQ(message_properties->message_property[MSG_LIFETIME].name, "msgLifetime");
    EXPECT_STREQ(message_properties->message_property[MSG_PRIORITY].name, "msgPriority");
    EXPECT_STREQ(message_properties->message_property[MSG_ORDERED].name, "msgOrdered");
    EXPECT_STREQ(message_properties->message_property[MSG_SAFELY_REPLAYABLE].name, "msgSafelyReplayable");
    EXPECT_STREQ(message_properties->message_property[FINAL].name, "final");
    EXPECT_STREQ(message_properties->message_property[MSG_CHECKSUM_LEN].name, "msgChecksumLen");
    EXPECT_STREQ(message_properties->message_property[MSG_RELIABLE].name, "msgReliable");
    EXPECT_STREQ(message_properties->message_property[MSG_CAPACITY_PROFILE].name, "msgCapacityProfile");
    EXPECT_STREQ(message_properties->message_property[NO_FRAGMENTATION].name, "noFragmentation");
    EXPECT_STREQ(message_properties->message_property[NO_SEGMENTATION].name, "noSegmentation");

    // Cleanup
    ct_message_properties_free(message_properties);
}

TEST(MessagePropertiesUnitTests, NewSetsPropertyTypes) {
    ct_message_properties_t* message_properties = ct_message_properties_new();
    ASSERT_NE(message_properties, nullptr);

    EXPECT_EQ(message_properties->message_property[MSG_LIFETIME].type, TYPE_UINT64_MSG);
    EXPECT_EQ(message_properties->message_property[MSG_PRIORITY].type, TYPE_UINT32_MSG);
    EXPECT_EQ(message_properties->message_property[MSG_ORDERED].type, TYPE_BOOLEAN_MSG);
    EXPECT_EQ(message_properties->message_property[MSG_SAFELY_REPLAYABLE].type, TYPE_BOOLEAN_MSG);
    EXPECT_EQ(message_properties->message_property[FINAL].type, TYPE_BOOLEAN_MSG);
    EXPECT_EQ(message_properties->message_property[MSG_CHECKSUM_LEN].type, TYPE_UINT32_MSG);
    EXPECT_EQ(message_properties->message_property[MSG_RELIABLE].type, TYPE_BOOLEAN_MSG);
    EXPECT_EQ(message_properties->message_property[MSG_CAPACITY_PROFILE].type, TYPE_ENUM_MSG);
    EXPECT_EQ(message_properties->message_property[NO_FRAGMENTATION].type, TYPE_BOOLEAN_MSG);
    EXPECT_EQ(message_properties->message_property[NO_SEGMENTATION].type, TYPE_BOOLEAN_MSG);

    // Cleanup
    ct_message_properties_free(message_properties);
}

TEST(MessagePropertiesUnitTests, IsFinalReturnsFalseByDefault) {
    ct_message_properties_t* message_properties = ct_message_properties_new();
    ASSERT_NE(message_properties, nullptr);

    bool is_final = ct_message_properties_is_final(message_properties);

    EXPECT_FALSE(is_final);

    // Cleanup
    ct_message_properties_free(message_properties);
}

TEST(MessagePropertiesUnitTests, IsFinalReturnsTrueAfterSet) {
    ct_message_properties_t* message_properties = ct_message_properties_new();
    ASSERT_NE(message_properties, nullptr);

    ct_message_properties_set_boolean(message_properties, FINAL, true);
    bool is_final = ct_message_properties_is_final(message_properties);

    EXPECT_TRUE(is_final);

    // Cleanup
    ct_message_properties_free(message_properties);
}

TEST(MessagePropertiesUnitTests, IsFinalReturnsFalseForNullPointer) {
    bool is_final = ct_message_properties_is_final(nullptr);

    EXPECT_FALSE(is_final);
}

TEST(MessagePropertiesUnitTests, SetFinalHandlesNullPointer) {
    ct_message_properties_set_boolean(nullptr, FINAL, true);

    SUCCEED();
}

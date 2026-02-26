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
    EXPECT_EQ(message_properties->list[MSG_ORDERED].value.bool_val, true);
    EXPECT_EQ(message_properties->list[MSG_SAFELY_REPLAYABLE].value.bool_val, false);
    EXPECT_EQ(message_properties->list[MSG_RELIABLE].value.bool_val, true);
    EXPECT_EQ(message_properties->list[NO_FRAGMENTATION].value.bool_val, false);
    EXPECT_EQ(message_properties->list[NO_SEGMENTATION].value.bool_val, false);

    // Verify default integer properties
    EXPECT_EQ(message_properties->list[MSG_PRIORITY].value.uint32_val, 100);
    EXPECT_EQ(message_properties->list[MSG_CHECKSUM_LEN].value.uint32_val, MESSAGE_CHECKSUM_FULL_COVERAGE);

    // Verify default uint64 properties
    EXPECT_EQ(message_properties->list[MSG_LIFETIME].value.uint64_val, 0);

    // Verify default enum properties
    EXPECT_EQ(message_properties->list[MSG_CAPACITY_PROFILE].value.enum_val, CAPACITY_PROFILE_BEST_EFFORT);

    // Cleanup
    ct_message_properties_free(message_properties);
}

TEST(MessagePropertiesUnitTests, NewSetsPropertyNames) {
    ct_message_properties_t* message_properties = ct_message_properties_new();
    ASSERT_NE(message_properties, nullptr);

    EXPECT_STREQ(message_properties->list[MSG_LIFETIME].name, "msgLifetime");
    EXPECT_STREQ(message_properties->list[MSG_PRIORITY].name, "msgPriority");
    EXPECT_STREQ(message_properties->list[MSG_ORDERED].name, "msgOrdered");
    EXPECT_STREQ(message_properties->list[MSG_SAFELY_REPLAYABLE].name, "msgSafelyReplayable");
    EXPECT_STREQ(message_properties->list[FINAL].name, "final");
    EXPECT_STREQ(message_properties->list[MSG_CHECKSUM_LEN].name, "msgChecksumLen");
    EXPECT_STREQ(message_properties->list[MSG_RELIABLE].name, "msgReliable");
    EXPECT_STREQ(message_properties->list[MSG_CAPACITY_PROFILE].name, "msgCapacityProfile");
    EXPECT_STREQ(message_properties->list[NO_FRAGMENTATION].name, "noFragmentation");
    EXPECT_STREQ(message_properties->list[NO_SEGMENTATION].name, "noSegmentation");

    // Cleanup
    ct_message_properties_free(message_properties);
}

TEST(MessagePropertiesUnitTests, IsFinalReturnsFalseByDefault) {
    ct_message_properties_t* message_properties = ct_message_properties_new();
    ASSERT_NE(message_properties, nullptr);

    bool is_final = ct_message_properties_get_final(message_properties);

    EXPECT_FALSE(is_final);

    // Cleanup
    ct_message_properties_free(message_properties);
}

TEST(MessagePropertiesUnitTests, IsFinalReturnsTrueAfterSet) {
    ct_message_properties_t* message_properties = ct_message_properties_new();
    ASSERT_NE(message_properties, nullptr);

    ct_message_properties_set_final(message_properties, true);
    bool is_final = ct_message_properties_get_final(message_properties);

    EXPECT_TRUE(is_final);

    // Cleanup
    ct_message_properties_free(message_properties);
}

TEST(MessagePropertiesUnitTests, IsFinalReturnsFalseForNullPointer) {
    bool is_final = ct_message_properties_get_final(nullptr);

    EXPECT_FALSE(is_final);
}

TEST(MessagePropertiesUnitTests, GetFinalHandlesNullPointer) {
    ct_message_properties_get_final(nullptr);

    SUCCEED();
}

// Tests for ct_message_properties_set_uint64
TEST(MessagePropertiesUnitTests, SetUint64SetsLifetime) {
    ct_message_properties_t* message_properties = ct_message_properties_new();
    ASSERT_NE(message_properties, nullptr);

    ct_message_properties_set_lifetime(message_properties, 5000);

    EXPECT_EQ(message_properties->list[MSG_LIFETIME].value.uint64_val, 5000);

    ct_message_properties_free(message_properties);
}

TEST(MessagePropertiesUnitTests, SetUint64HandlesNullPointer) {
    ct_message_properties_set_lifetime(nullptr, 5000);

    SUCCEED();
}

TEST(MessagePropertiesUnitTests, SetPriority) {
    ct_message_properties_t* message_properties = ct_message_properties_new();
    ASSERT_NE(message_properties, nullptr);

    ct_message_properties_set_priority(message_properties, 50);

    EXPECT_EQ(message_properties->list[MSG_PRIORITY].value.uint32_val, 50);

    ct_message_properties_free(message_properties);
}

TEST(MessagePropertiesUnitTests, SetUint32SetsChecksumLen) {
    ct_message_properties_t* message_properties = ct_message_properties_new();
    ASSERT_NE(message_properties, nullptr);

    ct_message_properties_set_checksum_len(message_properties, 128);

    EXPECT_EQ(message_properties->list[MSG_CHECKSUM_LEN].value.uint32_val, 128);

    ct_message_properties_free(message_properties);
}

TEST(MessagePropertiesUnitTests, SetsOrdered) {
    ct_message_properties_t* message_properties = ct_message_properties_new();
    ASSERT_NE(message_properties, nullptr);

    ct_message_properties_set_ordered(message_properties, false);

    EXPECT_FALSE(message_properties->list[MSG_ORDERED].value.bool_val);

    ct_message_properties_free(message_properties);
}

TEST(MessagePropertiesUnitTests, SetBooleanSetsSafelyReplayable) {
    ct_message_properties_t* message_properties = ct_message_properties_new();
    ASSERT_NE(message_properties, nullptr);

    ct_message_properties_set_safely_replayable(message_properties, true);

    EXPECT_TRUE(message_properties->list[MSG_SAFELY_REPLAYABLE].value.bool_val);

    ct_message_properties_free(message_properties);
}

TEST(MessagePropertiesUnitTests, SetBooleanSetsReliable) {
    ct_message_properties_t* message_properties = ct_message_properties_new();
    ASSERT_NE(message_properties, nullptr);

    ct_message_properties_set_reliable(message_properties, false);

    EXPECT_FALSE(message_properties->list[MSG_RELIABLE].value.bool_val);

    ct_message_properties_free(message_properties);
}

TEST(MessagePropertiesUnitTests, SetBooleanSetsNoFragmentation) {
    ct_message_properties_t* message_properties = ct_message_properties_new();
    ASSERT_NE(message_properties, nullptr);

    ct_message_properties_set_no_fragmentation(message_properties, true);

    EXPECT_TRUE(message_properties->list[NO_FRAGMENTATION].value.bool_val);

    ct_message_properties_free(message_properties);
}

TEST(MessagePropertiesUnitTests, SetBooleanSetsNoSegmentation) {
    ct_message_properties_t* message_properties = ct_message_properties_new();
    ASSERT_NE(message_properties, nullptr);

    ct_message_properties_set_no_segmentation(message_properties, true);

    EXPECT_TRUE(message_properties->list[NO_SEGMENTATION].value.bool_val);

    ct_message_properties_free(message_properties);
}

TEST(MessagePropertiesUnitTests, SetBooleanHandlesNullPointer) {
    ct_message_properties_set_ordered(nullptr, false);

    SUCCEED();
}

// Tests for ct_message_properties_set_capacity_profile
TEST(MessagePropertiesUnitTests, SetCapacityProfileSetsValue) {
    ct_message_properties_t* message_properties = ct_message_properties_new();
    ASSERT_NE(message_properties, nullptr);

    ct_message_properties_set_capacity_profile(message_properties, CAPACITY_PROFILE_LOW_LATENCY_INTERACTIVE);

    EXPECT_EQ(message_properties->list[MSG_CAPACITY_PROFILE].value.enum_val, CAPACITY_PROFILE_LOW_LATENCY_INTERACTIVE);

    ct_message_properties_free(message_properties);
}

TEST(MessagePropertiesUnitTests, SetCapacityProfileHandlesNullPointer) {
    ct_message_properties_set_capacity_profile(nullptr, CAPACITY_PROFILE_LOW_LATENCY_INTERACTIVE);

    SUCCEED();
}

// Positive tests for getters
TEST(MessagePropertiesUnitTests, GetLifetimeReturnsSetValue) {
    ct_message_properties_t* message_properties = ct_message_properties_new();
    ASSERT_NE(message_properties, nullptr);

    ct_message_properties_set_lifetime(message_properties, 5000);

    EXPECT_EQ(ct_message_properties_get_lifetime(message_properties), 5000);

    ct_message_properties_free(message_properties);
}

TEST(MessagePropertiesUnitTests, setPriorityReturnsSetValue) {
    ct_message_properties_t* message_properties = ct_message_properties_new();
    ASSERT_NE(message_properties, nullptr);

    ct_message_properties_set_priority(message_properties, 50);

    EXPECT_EQ(ct_message_properties_get_priority(message_properties), 50);

    ct_message_properties_free(message_properties);
}

TEST(MessagePropertiesUnitTests, GetBooleanReturnsSetValue) {
    ct_message_properties_t* message_properties = ct_message_properties_new();
    ASSERT_NE(message_properties, nullptr);

    ct_message_properties_set_ordered(message_properties, false);

    EXPECT_FALSE(ct_message_properties_get_ordered(message_properties));

    ct_message_properties_free(message_properties);
}

TEST(MessagePropertiesUnitTests, GetCapacityProfileReturnsSetValue) {
    ct_message_properties_t* message_properties = ct_message_properties_new();
    ASSERT_NE(message_properties, nullptr);

    ct_message_properties_set_capacity_profile(message_properties, CAPACITY_PROFILE_LOW_LATENCY_INTERACTIVE);

    EXPECT_EQ(ct_message_properties_get_capacity_profile(message_properties), CAPACITY_PROFILE_LOW_LATENCY_INTERACTIVE);

    ct_message_properties_free(message_properties);
}

// Null pointer tests for getters
TEST(MessagePropertiesUnitTests, getLifetimeReturnsDefaultForNullPointer) {
    EXPECT_EQ(ct_message_properties_get_lifetime(nullptr), 0);
}

TEST(MessagePropertiesUnitTests, getPriorityReturnsDefaultForNullPointer) {
    EXPECT_EQ(ct_message_properties_get_priority(nullptr), 100);
}

TEST(MessagePropertiesUnitTests, GetOrderedReturnsDefaultForNullPointer) {
    EXPECT_TRUE(ct_message_properties_get_ordered(nullptr));
}

TEST(MessagePropertiesUnitTests, GetCapacityProfileReturnsDefaultForNullPointer) {
    EXPECT_EQ(ct_message_properties_get_capacity_profile(nullptr), CAPACITY_PROFILE_BEST_EFFORT);
}

TEST(MessagePropertiesUnitTests, GetSafelyReplayableHandlesNullptr) {
    EXPECT_FALSE(ct_message_properties_get_safely_replayable(nullptr));
}

TEST(MessagePropertiesUnitTests, GetFinalHandlesNullptr) {
    EXPECT_FALSE(ct_message_properties_get_final(nullptr));
}

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
    EXPECT_EQ(message_properties->message_property[MSG_CHECKSUM_LEN].value.uint32_value, MESSAGE_CHECKSUM_FULL_COVERAGE);

    // Verify default uint64 properties
    EXPECT_EQ(message_properties->message_property[MSG_LIFETIME].value.uint64_value, 0);

    // Verify default enum properties
    EXPECT_EQ(message_properties->message_property[MSG_CAPACITY_PROFILE].value.capacity_profile_enum_value, CAPACITY_PROFILE_BEST_EFFORT);

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

// Tests for ct_message_properties_set_uint64
TEST(MessagePropertiesUnitTests, SetUint64SetsLifetime) {
    ct_message_properties_t* message_properties = ct_message_properties_new();
    ASSERT_NE(message_properties, nullptr);

    ct_message_properties_set_uint64(message_properties, MSG_LIFETIME, 5000);

    EXPECT_EQ(message_properties->message_property[MSG_LIFETIME].value.uint64_value, 5000);

    ct_message_properties_free(message_properties);
}

TEST(MessagePropertiesUnitTests, SetUint64HandlesNullPointer) {
    ct_message_properties_set_uint64(nullptr, MSG_LIFETIME, 5000);

    SUCCEED();
}

// Tests for ct_message_properties_set_uint32
TEST(MessagePropertiesUnitTests, SetUint32SetsPriority) {
    ct_message_properties_t* message_properties = ct_message_properties_new();
    ASSERT_NE(message_properties, nullptr);

    ct_message_properties_set_uint32(message_properties, MSG_PRIORITY, 50);

    EXPECT_EQ(message_properties->message_property[MSG_PRIORITY].value.uint32_value, 50);

    ct_message_properties_free(message_properties);
}

TEST(MessagePropertiesUnitTests, SetUint32SetsChecksumLen) {
    ct_message_properties_t* message_properties = ct_message_properties_new();
    ASSERT_NE(message_properties, nullptr);

    ct_message_properties_set_uint32(message_properties, MSG_CHECKSUM_LEN, 128);

    EXPECT_EQ(message_properties->message_property[MSG_CHECKSUM_LEN].value.uint32_value, 128);

    ct_message_properties_free(message_properties);
}

TEST(MessagePropertiesUnitTests, SetUint32HandlesNullPointer) {
    ct_message_properties_set_uint32(nullptr, MSG_PRIORITY, 50);

    SUCCEED();
}

// Tests for ct_message_properties_set_boolean
TEST(MessagePropertiesUnitTests, SetBooleanSetsOrdered) {
    ct_message_properties_t* message_properties = ct_message_properties_new();
    ASSERT_NE(message_properties, nullptr);

    ct_message_properties_set_boolean(message_properties, MSG_ORDERED, false);

    EXPECT_FALSE(message_properties->message_property[MSG_ORDERED].value.boolean_value);

    ct_message_properties_free(message_properties);
}

TEST(MessagePropertiesUnitTests, SetBooleanSetsSafelyReplayable) {
    ct_message_properties_t* message_properties = ct_message_properties_new();
    ASSERT_NE(message_properties, nullptr);

    ct_message_properties_set_boolean(message_properties, MSG_SAFELY_REPLAYABLE, true);

    EXPECT_TRUE(message_properties->message_property[MSG_SAFELY_REPLAYABLE].value.boolean_value);

    ct_message_properties_free(message_properties);
}

TEST(MessagePropertiesUnitTests, SetBooleanSetsReliable) {
    ct_message_properties_t* message_properties = ct_message_properties_new();
    ASSERT_NE(message_properties, nullptr);

    ct_message_properties_set_boolean(message_properties, MSG_RELIABLE, false);

    EXPECT_FALSE(message_properties->message_property[MSG_RELIABLE].value.boolean_value);

    ct_message_properties_free(message_properties);
}

TEST(MessagePropertiesUnitTests, SetBooleanSetsNoFragmentation) {
    ct_message_properties_t* message_properties = ct_message_properties_new();
    ASSERT_NE(message_properties, nullptr);

    ct_message_properties_set_boolean(message_properties, NO_FRAGMENTATION, true);

    EXPECT_TRUE(message_properties->message_property[NO_FRAGMENTATION].value.boolean_value);

    ct_message_properties_free(message_properties);
}

TEST(MessagePropertiesUnitTests, SetBooleanSetsNoSegmentation) {
    ct_message_properties_t* message_properties = ct_message_properties_new();
    ASSERT_NE(message_properties, nullptr);

    ct_message_properties_set_boolean(message_properties, NO_SEGMENTATION, true);

    EXPECT_TRUE(message_properties->message_property[NO_SEGMENTATION].value.boolean_value);

    ct_message_properties_free(message_properties);
}

TEST(MessagePropertiesUnitTests, SetBooleanHandlesNullPointer) {
    ct_message_properties_set_boolean(nullptr, MSG_ORDERED, false);

    SUCCEED();
}

// Tests for ct_message_properties_set_capacity_profile
TEST(MessagePropertiesUnitTests, SetCapacityProfileSetsValue) {
    ct_message_properties_t* message_properties = ct_message_properties_new();
    ASSERT_NE(message_properties, nullptr);

    ct_message_properties_set_capacity_profile(message_properties, MSG_CAPACITY_PROFILE, CAPACITY_PROFILE_LOW_LATENCY_INTERACTIVE);

    EXPECT_EQ(message_properties->message_property[MSG_CAPACITY_PROFILE].value.capacity_profile_enum_value, CAPACITY_PROFILE_LOW_LATENCY_INTERACTIVE);

    ct_message_properties_free(message_properties);
}

TEST(MessagePropertiesUnitTests, SetCapacityProfileHandlesNullPointer) {
    ct_message_properties_set_capacity_profile(nullptr, MSG_CAPACITY_PROFILE, CAPACITY_PROFILE_LOW_LATENCY_INTERACTIVE);

    SUCCEED();
}

// Negative tests - type mismatch should not modify the value
TEST(MessagePropertiesUnitTests, SetUint64OnBooleanPropertyDoesNotModify) {
    ct_message_properties_t* message_properties = ct_message_properties_new();
    ASSERT_NE(message_properties, nullptr);

    bool original_value = message_properties->message_property[MSG_ORDERED].value.boolean_value;
    ct_message_properties_set_uint64(message_properties, MSG_ORDERED, 12345);

    EXPECT_EQ(message_properties->message_property[MSG_ORDERED].value.boolean_value, original_value);

    ct_message_properties_free(message_properties);
}

TEST(MessagePropertiesUnitTests, SetUint32OnBooleanPropertyDoesNotModify) {
    ct_message_properties_t* message_properties = ct_message_properties_new();
    ASSERT_NE(message_properties, nullptr);

    bool original_value = message_properties->message_property[MSG_RELIABLE].value.boolean_value;
    ct_message_properties_set_uint32(message_properties, MSG_RELIABLE, 999);

    EXPECT_EQ(message_properties->message_property[MSG_RELIABLE].value.boolean_value, original_value);

    ct_message_properties_free(message_properties);
}

TEST(MessagePropertiesUnitTests, SetBooleanOnUint32PropertyDoesNotModify) {
    ct_message_properties_t* message_properties = ct_message_properties_new();
    ASSERT_NE(message_properties, nullptr);

    uint32_t original_value = message_properties->message_property[MSG_PRIORITY].value.uint32_value;
    ct_message_properties_set_boolean(message_properties, MSG_PRIORITY, true);

    EXPECT_EQ(message_properties->message_property[MSG_PRIORITY].value.uint32_value, original_value);

    ct_message_properties_free(message_properties);
}

TEST(MessagePropertiesUnitTests, SetCapacityProfileOnBooleanPropertyDoesNotModify) {
    ct_message_properties_t* message_properties = ct_message_properties_new();
    ASSERT_NE(message_properties, nullptr);

    bool original_value = message_properties->message_property[MSG_ORDERED].value.boolean_value;
    ct_message_properties_set_capacity_profile(message_properties, MSG_ORDERED, CAPACITY_PROFILE_LOW_LATENCY_INTERACTIVE);

    EXPECT_EQ(message_properties->message_property[MSG_ORDERED].value.boolean_value, original_value);

    ct_message_properties_free(message_properties);
}

// Positive tests for getters
TEST(MessagePropertiesUnitTests, GetUint64ReturnsSetValue) {
    ct_message_properties_t* message_properties = ct_message_properties_new();
    ASSERT_NE(message_properties, nullptr);

    ct_message_properties_set_uint64(message_properties, MSG_LIFETIME, 5000);

    EXPECT_EQ(ct_message_properties_get_uint64(message_properties, MSG_LIFETIME), 5000);

    ct_message_properties_free(message_properties);
}

TEST(MessagePropertiesUnitTests, GetUint32ReturnsSetValue) {
    ct_message_properties_t* message_properties = ct_message_properties_new();
    ASSERT_NE(message_properties, nullptr);

    ct_message_properties_set_uint32(message_properties, MSG_PRIORITY, 50);

    EXPECT_EQ(ct_message_properties_get_uint32(message_properties, MSG_PRIORITY), 50);

    ct_message_properties_free(message_properties);
}

TEST(MessagePropertiesUnitTests, GetBooleanReturnsSetValue) {
    ct_message_properties_t* message_properties = ct_message_properties_new();
    ASSERT_NE(message_properties, nullptr);

    ct_message_properties_set_boolean(message_properties, MSG_ORDERED, false);

    EXPECT_FALSE(ct_message_properties_get_boolean(message_properties, MSG_ORDERED));

    ct_message_properties_free(message_properties);
}

TEST(MessagePropertiesUnitTests, GetCapacityProfileReturnsSetValue) {
    ct_message_properties_t* message_properties = ct_message_properties_new();
    ASSERT_NE(message_properties, nullptr);

    ct_message_properties_set_capacity_profile(message_properties, MSG_CAPACITY_PROFILE, CAPACITY_PROFILE_LOW_LATENCY_INTERACTIVE);

    EXPECT_EQ(ct_message_properties_get_capacity_profile(message_properties), CAPACITY_PROFILE_LOW_LATENCY_INTERACTIVE);

    ct_message_properties_free(message_properties);
}

// Null pointer tests for getters
TEST(MessagePropertiesUnitTests, GetUint64ReturnsZeroForNullPointer) {
    EXPECT_EQ(ct_message_properties_get_uint64(nullptr, MSG_LIFETIME), 0);
}

TEST(MessagePropertiesUnitTests, GetUint32ReturnsZeroForNullPointer) {
    EXPECT_EQ(ct_message_properties_get_uint32(nullptr, MSG_PRIORITY), 0);
}

TEST(MessagePropertiesUnitTests, GetBooleanReturnsFalseForNullPointer) {
    EXPECT_FALSE(ct_message_properties_get_boolean(nullptr, MSG_ORDERED));
}

TEST(MessagePropertiesUnitTests, GetCapacityProfileReturnsDefaultForNullPointer) {
    EXPECT_EQ(ct_message_properties_get_capacity_profile(nullptr), CAPACITY_PROFILE_BEST_EFFORT);
}

// Negative tests for getters - type mismatch returns default value
TEST(MessagePropertiesUnitTests, GetUint64OnBooleanPropertyReturnsZero) {
    ct_message_properties_t* message_properties = ct_message_properties_new();
    ASSERT_NE(message_properties, nullptr);

    EXPECT_EQ(ct_message_properties_get_uint64(message_properties, MSG_ORDERED), 0);

    ct_message_properties_free(message_properties);
}

TEST(MessagePropertiesUnitTests, GetUint32OnBooleanPropertyReturnsZero) {
    ct_message_properties_t* message_properties = ct_message_properties_new();
    ASSERT_NE(message_properties, nullptr);

    EXPECT_EQ(ct_message_properties_get_uint32(message_properties, MSG_RELIABLE), 0);

    ct_message_properties_free(message_properties);
}

TEST(MessagePropertiesUnitTests, GetBooleanOnUint32PropertyReturnsFalse) {
    ct_message_properties_t* message_properties = ct_message_properties_new();
    ASSERT_NE(message_properties, nullptr);

    EXPECT_FALSE(ct_message_properties_get_boolean(message_properties, MSG_PRIORITY));

    ct_message_properties_free(message_properties);
}

TEST(MessagePropertiesUnitTests, GetSafelyReplayableHandlesNullptr) {
    EXPECT_FALSE(ct_message_properties_get_safely_replayable(nullptr));
}

TEST(MessagePropertiesUnitTests, GetFinalHandlesNullptr) {
    EXPECT_FALSE(ct_message_properties_is_final(nullptr));
}

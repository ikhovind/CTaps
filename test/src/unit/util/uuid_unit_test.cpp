// test_uuid.cpp
#include <gtest/gtest.h>
#include <fff.h>
#include <cstring>
#include <cctype>
#include <unistd.h>
#include <sys/random.h>

DEFINE_FFF_GLOBALS;

// Wrap getrandom

extern "C" {
    FAKE_VALUE_FUNC(ssize_t, __wrap_getrandom, void*, size_t, unsigned int);
    void generate_uuid_string(char* uuid_str);
    extern ssize_t __real_getrandom(void* buf, size_t buflen, unsigned int flags);

}

// ── Helpers ──────────────────────────────────────────────────────────────────

static ssize_t fake_getrandom_full(void* buf, size_t buflen, unsigned int) {
    // Fill with a deterministic pattern for predictable output
    memset(buf, 0xAB, buflen);
    return (ssize_t)buflen;
}

static ssize_t fake_getrandom_short(void* buf, size_t buflen, unsigned int) {
    // Simulates a short read — only fills half
    memset(buf, 0x00, buflen / 2);
    return (ssize_t)(buflen / 2);
}

static ssize_t fake_getrandom_fail(void*, size_t, unsigned int) {
    return -1;
}

// ── Fixture ──────────────────────────────────────────────────────────────────

class UuidTest : public ::testing::Test {
protected:
    void SetUp() override {
        RESET_FAKE(__wrap_getrandom);
        FFF_RESET_HISTORY();
    }

    char uuid_str[37]{};
};

// ── Format tests ─────────────────────────────────────────────────────────────

TEST_F(UuidTest, OutputIsNullTerminated) {
    __wrap_getrandom_fake.custom_fake = fake_getrandom_full;
    generate_uuid_string(uuid_str);
    // snprintf always null-terminates within buffer, but let's be explicit
    EXPECT_EQ(uuid_str[36], '\0');
}

TEST_F(UuidTest, OutputLengthIs36) {
    __wrap_getrandom_fake.custom_fake = fake_getrandom_full;
    generate_uuid_string(uuid_str);
    EXPECT_EQ(strlen(uuid_str), 36u);
}

TEST_F(UuidTest, HyphensAtCorrectPositions) {
    __wrap_getrandom_fake.custom_fake = fake_getrandom_full;
    generate_uuid_string(uuid_str);
    EXPECT_EQ(uuid_str[8],  '-');
    EXPECT_EQ(uuid_str[13], '-');
    EXPECT_EQ(uuid_str[18], '-');
    EXPECT_EQ(uuid_str[23], '-');
}

TEST_F(UuidTest, AllNonHyphenCharsAreHex) {
    __wrap_getrandom_fake.custom_fake = fake_getrandom_full;
    generate_uuid_string(uuid_str);
    for (int i = 0; i < 36; i++) {
        if (i == 8 || i == 13 || i == 18 || i == 23) continue;
        EXPECT_TRUE(isxdigit((unsigned char)uuid_str[i]))
            << "Non-hex char at position " << i << ": " << uuid_str[i];
    }
}

TEST_F(UuidTest, DeterministicOutputForKnownInput) {
    __wrap_getrandom_fake.custom_fake = fake_getrandom_full; // always fills with 0xAB
    generate_uuid_string(uuid_str);
    EXPECT_STREQ(uuid_str, "abababab-abab-4bab-abab-abababababab");
}

// ── Uniqueness ───────────────────────────────────────────────────────────────

TEST_F(UuidTest, TwoCallsProduceDifferentResults) {
    // Use the real getrandom for this one
    __wrap_getrandom_fake.custom_fake = [](void* buf, size_t len, unsigned int flags) -> ssize_t {
        return __real_getrandom(buf, len, flags);
    };
    char uuid2[37]{};
    generate_uuid_string(uuid_str);
    generate_uuid_string(uuid2);
    EXPECT_STRNE(uuid_str, uuid2);
}

TEST_F(UuidTest, HandlesGetrandomFailure) {
    __wrap_getrandom_fake.custom_fake = fake_getrandom_fail;
    generate_uuid_string(uuid_str);
    EXPECT_EQ(strlen(uuid_str), 36u);
    EXPECT_EQ(uuid_str[8],  '-');
    EXPECT_EQ(uuid_str[13], '-');
    EXPECT_EQ(uuid_str[18], '-');
    EXPECT_EQ(uuid_str[23], '-');
    EXPECT_EQ(uuid_str[36], '\0');
}

TEST_F(UuidTest, HandlesShortRead) {
    __wrap_getrandom_fake.custom_fake = fake_getrandom_short;
    generate_uuid_string(uuid_str);
    EXPECT_EQ(strlen(uuid_str), 36u);
    EXPECT_EQ(uuid_str[8],  '-');
    EXPECT_EQ(uuid_str[13], '-');
    EXPECT_EQ(uuid_str[18], '-');
    EXPECT_EQ(uuid_str[23], '-');
    EXPECT_EQ(uuid_str[36], '\0');
}

// ── RFC 4122 compliance (also currently FAIL) ────────────────────────────────

TEST_F(UuidTest, VersionBitsIndicateV4) {
    __wrap_getrandom_fake.custom_fake = fake_getrandom_full;
    generate_uuid_string(uuid_str);
    // uuid[6] high nibble must be '4'
    EXPECT_EQ(uuid_str[14], '4')
        << "Version nibble at position 14 should be '4' for UUID v4";
}

TEST_F(UuidTest, VariantBitsAreCorrect) {
    __wrap_getrandom_fake.custom_fake = fake_getrandom_full;
    generate_uuid_string(uuid_str);
    // uuid[8] high nibble must be 8, 9, a, or b
    char variant = uuid_str[19];
    EXPECT_TRUE(variant == '8' || variant == '9' ||
                variant == 'a' || variant == 'b')
        << "Variant nibble at position 19 should be 8/9/a/b, got: " << variant;
}

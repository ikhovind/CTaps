#include <string.h>

#include "gtest/gtest.h"
#include "fff.h"
extern "C" {
#include "lib.h"
#include "mocked_lib.h"
}

DEFINE_FFF_GLOBALS;
FAKE_VOID_FUNC(mocked_function);

TEST(CallsMockedFunctionTest, Returns42) {
    int result = calls_mocked_function();

    EXPECT_EQ(1, mocked_function_fake.call_count);
    EXPECT_EQ(42, result);
}

TEST(CreateLibraryTest, SetsProperName) {
    struct library lib = create_library();

    EXPECT_STREQ("mocking", lib.name);
}

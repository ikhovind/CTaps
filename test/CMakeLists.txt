project(CTapsTests)


enable_testing()
include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/heads/main.zip
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE  # Ensure the extracted files use the timestamp of extraction
)

FetchContent_MakeAvailable(googletest)

FetchContent_Declare(
  fff
  URL https://github.com/meekrosoft/fff/archive/refs/heads/master.zip
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE  # Ensure the extracted files use the timestamp of extraction
)
FetchContent_MakeAvailable(fff)

# ---- Tests ----

add_executable(mocking_test source/mocking_test.cpp)
target_link_libraries(mocking_test PRIVATE gtest gtest_main gmock fff c_lib )
target_compile_features(mocking_test PRIVATE c_std_99)

add_test(NAME mocking_test COMMAND mocking_test)

add_executable(networking source/networking.cpp)
target_link_libraries(networking PRIVATE gtest gtest_main gmock fff CTaps)
target_compile_features(networking PRIVATE c_std_99)

add_test(NAME networking COMMAND networking)

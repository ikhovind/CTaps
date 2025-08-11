# Parent project does not export its library target, so this CML implicitly
# depends on being added from it, i.e. the testing is done only from the build
# tree and is not feasible from an install location

project(mockingTests)


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
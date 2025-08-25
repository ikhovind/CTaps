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

add_executable(CTaps_test
        source/integration/udp_ping.cpp
        source/util/util.c
)
target_link_libraries(CTaps_test PRIVATE gtest gtest_main gmock fff CTaps)
target_compile_features(CTaps_test PRIVATE c_std_99)
target_include_directories(CTaps_test PRIVATE ${CMAKE_SOURCE_DIR}/test/source)

add_test(NAME CTaps_test COMMAND CTaps_test)

add_executable(initiation_test
        source/preconnection/initiation.cpp
        source/util/util.c
)
target_link_libraries(initiation_test PRIVATE gtest gtest_main gmock fff CTaps)
target_compile_features(initiation_test PRIVATE c_std_99)
target_include_directories(initiation_test PRIVATE ${CMAKE_SOURCE_DIR}/test/source)

add_test(NAME initiation_test COMMAND initiation_test)

add_executable(remote_endpoint_unit_test
        source/unit/endpoint/remote.cpp
        source/util/util.c
)
target_link_libraries(remote_endpoint_unit_test PRIVATE gtest gtest_main gmock fff CTaps)
target_compile_features(remote_endpoint_unit_test PRIVATE c_std_99)
target_include_directories(remote_endpoint_unit_test PRIVATE ${CMAKE_SOURCE_DIR}/test/source)

add_test(NAME remote_endpoint_unit_test COMMAND remote_endpoint_unit_test)

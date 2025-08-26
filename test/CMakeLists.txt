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
function(add_gtest test_name)
    set(source_files ${ARGN})

    add_executable(${test_name}
            ${source_files}
            source/util/util.c
    )

    target_link_libraries(${test_name}
            PRIVATE
            gtest
            gtest_main
            gmock
            fff
            CTaps
            c_lib # Add any other common test libraries here
    )

    # Set common properties
    target_compile_features(${test_name} PRIVATE c_std_99)
    target_include_directories(${test_name} PRIVATE ${CMAKE_SOURCE_DIR}/test/source)

    # Add it to CTest
    add_test(NAME ${test_name} COMMAND ${test_name})
endfunction()


add_gtest(mocking_test source/mocking_test.cpp)
add_gtest(CTaps_test source/integration/udp_ping.cpp)
add_gtest(initiation_test source/preconnection/initiation.cpp)
add_gtest(remote_endpoint_unit_test source/unit/endpoint/remote.cpp)
add_gtest(dns_integration_test source/integration/dns/dns_test.cpp)

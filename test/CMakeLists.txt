project(CTapsTests)

set_property(GLOBAL PROPERTY CTAPS_TEST_TARGETS "")

enable_testing()
include(GoogleTest)
include(FetchContent)
FetchContent_Declare(
  googletest
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE  # Ensure the extracted files use the timestamp of extraction
  URL https://github.com/google/googletest/archive/refs/heads/main.zip
)

FetchContent_MakeAvailable(googletest)

FetchContent_Declare(
  fff
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE  # Ensure the extracted files use the timestamp of extraction
  URL https://github.com/meekrosoft/fff/archive/refs/heads/master.zip
)
FetchContent_MakeAvailable(fff)

# ---- Tests ----
function(add_gtest test_name)
    set(options "ASAN_ENABLED" "DEBUG_SYMBOLS") # Option to selectively enable ASan
    set(oneValueArgs "")
    set(multiValueArgs SOURCES WRAP_FUNCTIONS)

    # Parse the arguments passed to the function (ARGN).
    cmake_parse_arguments(ARGS "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    add_executable(${test_name} ${ARGS_SOURCES})

    target_compile_options(${test_name} PRIVATE -Werror -Wno-write-strings)

    if (ARGS_DEBUG_SYMBOLS)
        message(STATUS "Adding debug symbols to ${test_name}")
        target_compile_options(${test_name} PRIVATE -g)
    endif()

    target_compile_definitions(${test_name}
            PRIVATE
            TEST_RESOURCE_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/quic\"
    )

    if (ARGS_ASAN_ENABLED)
        message(STATUS "Adding AddressSanitizer to ${test_name}")
        # Compilation flags for AddressSanitizer
        target_compile_options(${test_name} PRIVATE
                -fsanitize=address
                -g
                -O1
                -fno-omit-frame-pointer
        )

        # Linker flag to link with the ASan runtime library
        target_link_options(${test_name} PRIVATE
                -fsanitize=leak,address,undefined
        )
    endif()


    foreach(func_name ${ARGS_WRAP_FUNCTIONS})
        target_link_options(${test_name} PRIVATE "-Wl,--wrap=${func_name}")
    endforeach()


    target_link_libraries(${test_name}
            PRIVATE
            gtest
            gtest_main
            gmock
            fff
            ${GLIB_LIBRARIES}
            ${UUID_LIBRARIES}
            uv
            picoquic-core
    )

    target_compile_features(${test_name} PRIVATE c_std_99)
    target_include_directories(${test_name} PRIVATE
            ${CMAKE_SOURCE_DIR}/test/src
            ${CMAKE_SOURCE_DIR}/include
            ${CMAKE_SOURCE_DIR}/src
            ${GLIB_INCLUDE_DIRS}
            ${GLIB_LIBRARIES}
    )

    target_include_directories(${test_name}
            PRIVATE
            "${picotls_SOURCE_DIR}/include"
    )

    gtest_discover_tests(${test_name})

    get_property(all_tests GLOBAL PROPERTY CTAPS_TEST_TARGETS)
    list(APPEND all_tests ${test_name})
    set_property(GLOBAL PROPERTY CTAPS_TEST_TARGETS "${all_tests}")
    message(STATUS "Added test target: ${test_name}")
endfunction()

# Get CTAPS_SOURCES from parent CMakeLists.txt
list(TRANSFORM CTAPS_SOURCES PREPEND ${CMAKE_SOURCE_DIR}/)


add_gtest(udp_ping_test SOURCES src/integration/udp_ping.cpp ${CTAPS_SOURCES})
add_gtest(tcp_ping_test SOURCES src/integration/tcp/tcp_ping_test.cpp ${CTAPS_SOURCES})
add_gtest(
        quic_ping_test
        SOURCES
            src/integration/quic/quic_ping_test.cpp
            ${CTAPS_SOURCES}
)
add_gtest(quic_listener_test SOURCES src/integration/quic/quic_listen_test.cpp ${CTAPS_SOURCES})
add_gtest(connection_clone_test SOURCES src/integration/quic/connection_clone_test.cpp ${CTAPS_SOURCES})
add_gtest(
        quic_abort_test
        SOURCES
            src/integration/quic/quic_abort_test.cpp
            ${CTAPS_SOURCES}
        WRAP_FUNCTIONS
            picoquic_reset_stream
            picoquic_close_immediate
)
add_gtest(remote_endpoint_unit_test SOURCES src/unit/endpoint/remote.cpp ${CTAPS_SOURCES})
add_gtest(preconnection_unit_test SOURCES src/unit/connections/preconnection.cpp ${CTAPS_SOURCES})
add_gtest(connection_unit_test SOURCES src/unit/connections/connection_unit_test.cpp ${CTAPS_SOURCES})
add_gtest(connection_group_unit_test SOURCES src/unit/connections/connection_group_test.cpp ${CTAPS_SOURCES})
add_gtest(candidate_gathering_test
        SOURCES
            src/unit/candidate_gathering/candidate_gathering_test.cpp
            ${CTAPS_SOURCES}
        WRAP_FUNCTIONS
            ct_local_endpoint_resolve
            ct_remote_endpoint_resolve
            ct_get_supported_protocols
            ct_get_num_protocols
        ASAN_ENABLED
)

add_gtest(local_endpoint_unit_test
        SOURCES
            src/unit/endpoint/local.cpp ${CTAPS_SOURCES}
          WRAP_FUNCTIONS
            resolve_local_endpoint_from_handle
)
add_gtest(dns_integration_test
        SOURCES
            src/integration/dns/dns_test.cpp
            ${CTAPS_SOURCES}
)
add_gtest(remote_endpoint_integration_test
        SOURCES
            src/integration/endpoint/remote_endpoint_test.cpp
            ${CMAKE_SOURCE_DIR}/src/endpoint/remote_endpoint.c
            ${CMAKE_SOURCE_DIR}/src/endpoint/local_endpoint.c
            ${CMAKE_SOURCE_DIR}/src/logging/log.c
            ${CMAKE_SOURCE_DIR}/src/message/message.c
            ${CMAKE_SOURCE_DIR}/src/message/message_context.c
            ${CMAKE_SOURCE_DIR}/src/endpoint/util.c
            ${CMAKE_SOURCE_DIR}/src/protocol/udp/udp.c
            ${CMAKE_SOURCE_DIR}/src/protocol/tcp/tcp.c
            ${CMAKE_SOURCE_DIR}/src/protocol/quic/quic.c
            ${CMAKE_SOURCE_DIR}/src/protocol/common/socket_utils.c
            ${CMAKE_SOURCE_DIR}/src/protocol/common/protocol.c
            ${CMAKE_SOURCE_DIR}/src/util/uuid_util.c
            ${CMAKE_SOURCE_DIR}/src/state/ctaps_state.c
            ${CMAKE_SOURCE_DIR}/src/transport_property/transport_properties.c
            ${CMAKE_SOURCE_DIR}/src/transport_property/selection_properties/selection_properties.c
            ${CMAKE_SOURCE_DIR}/src/transport_property/message_properties/message_properties.c
            ${CMAKE_SOURCE_DIR}/src/transport_property/connection_properties/connection_properties.c
            ${CMAKE_SOURCE_DIR}/src/security_parameter/security_parameters.c
            ${CMAKE_SOURCE_DIR}/src/security_parameter/certificate_bundles/certificate_bundles.c
            ${CMAKE_SOURCE_DIR}/src/security_parameter/byte_array/byte_array.c
            ${CMAKE_SOURCE_DIR}/src/connection/connection.c
            ${CMAKE_SOURCE_DIR}/src/connection/connection_group.c
            ${CMAKE_SOURCE_DIR}/src/protocol/protocol_registry.c
            ${CMAKE_SOURCE_DIR}/src/connection/listener.c
            ${CMAKE_SOURCE_DIR}/src/connection/socket_manager/socket_manager.c
        WRAP_FUNCTIONS
            uv_getaddrinfo
            uv_freeaddrinfo
            get_service_port
)
add_gtest(udp_listen_test SOURCES src/integration/udp/udp_listen_test.cpp ${CTAPS_SOURCES})
add_gtest(tcp_listen_test SOURCES src/integration/tcp/tcp_listen_test.cpp ${CTAPS_SOURCES})
add_gtest(selection_property_test
  SOURCES
    src/unit/transport_properties/selection_properties/selection_properties_test.cpp
    ${CTAPS_SOURCES}
)
add_gtest(message_properties_test
  SOURCES
    src/unit/transport_properties/message_properties/message_properties_test.cpp
    ${CTAPS_SOURCES}
)
add_gtest(candidate_racing_test
  SOURCES
    src/integration/candidate_racing_test.cpp
    ${CTAPS_SOURCES}
  ASAN_ENABLED
)

add_gtest(framing_test
  SOURCES
    src/integration/framing_test.cpp
    ${CTAPS_SOURCES}
)

add_gtest(local_endpoint_resolve_test
  SOURCES
    src/unit/endpoint/local/local_endpoint_resolve_test.cpp
    ${CMAKE_SOURCE_DIR}/src/endpoint/local_endpoint.c
    ${CMAKE_SOURCE_DIR}/src/logging/log.c
  WRAP_FUNCTIONS
    get_service_port
)

add_gtest(message_context_unit_test
  SOURCES
    src/unit/message/message_context_unit_test.cpp
    ${CTAPS_SOURCES}
)

add_gtest(connection_properties_test
  SOURCES
    src/unit/transport_properties/connection_properties/connection_properties_test.cpp
    ${CTAPS_SOURCES}
)

add_gtest(security_parameters_test
  SOURCES
    src/unit/security_parameters/security_parameters_unit_test.cpp
    ${CTAPS_SOURCES}
)

add_gtest(message_context_integration_test
  SOURCES
    src/integration/message/message_context_integration_test.cpp
    ${CTAPS_SOURCES}
)

add_gtest(message_unit_test
  SOURCES
    src/unit/message/message_unit_test.cpp
    ${CTAPS_SOURCES}
)

add_gtest(tcp_close_callback_test
  SOURCES
    src/unit/protocol/tcp_close_test.cpp
    ${CTAPS_SOURCES}
  WRAP_FUNCTIONS
    uv_close
    uv_tcp_close_reset
    uv_is_closing
  ASAN_ENABLED
)

get_property(all_tests GLOBAL PROPERTY CTAPS_TEST_TARGETS)
add_custom_target(ctaps_tests
    DEPENDS ${all_tests}
    COMMENT "Building all CTaps tests"
)

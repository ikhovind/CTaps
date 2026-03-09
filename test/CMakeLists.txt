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

add_executable(picoquic_ping_server picoquic_ping_server.c)
target_link_libraries(picoquic_ping_server PRIVATE picoquic-core)

add_executable(tcp_ping_server tcp_ping_server.c)
add_executable(udp_ping_server udp_ping_server.c)

target_compile_definitions(picoquic_ping_server PRIVATE TEST_RESOURCE_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/resources\")

# ---- Tests ----
function(add_gtest test_name)
    set(options "ASAN_ENABLED" "DEBUG_SYMBOLS") # Option to selectively enable ASan
    set(oneValueArgs "")
    set(multiValueArgs SOURCES WRAP_FUNCTIONS TEST_PROPERTIES)

    # Parse the arguments passed to the function (ARGN).
    cmake_parse_arguments(ARGS "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    add_executable(${test_name} ${ARGS_SOURCES})

    target_compile_options(${test_name} PRIVATE -Werror -Wno-write-strings)

    if (ARGS_DEBUG_SYMBOLS)
        message(STATUS "Adding debug symbols to ${test_name}")
        target_compile_options(${test_name} PRIVATE -g -O0)
    endif()

    target_compile_definitions(${test_name}
            PRIVATE
            TEST_RESOURCE_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/resources\"
            PICOQUIC_PING_SERVER_PATH="$<TARGET_FILE:picoquic_ping_server>"
            TCP_PING_SERVER_PATH="$<TARGET_FILE:tcp_ping_server>"
            UDP_PING_SERVER_PATH="$<TARGET_FILE:udp_ping_server>"
    )

    if (ARGS_ASAN_ENABLED)
        message(STATUS "Adding AddressSanitizer to ${test_name}")
        # Compilation flags for AddressSanitizer
        target_compile_options(${test_name} PRIVATE
                -fsanitize=address
                -g
                -O0
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
            picoquic-log
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
            "${picoquic_SOURCE_DIR}/loglib"
            "${picoquic_SOURCE_DIR}/picoquic"
    )

    # Always malloc helps us discover memory leaks with ASAN
    gtest_discover_tests(
      ${test_name}
      PROPERTIES
        ENVIRONMENT
          "G_SLICE=always-malloc"
          ${ARGS_TEST_PROPERTIES}
    )

    get_property(all_tests GLOBAL PROPERTY CTAPS_TEST_TARGETS)
    list(APPEND all_tests ${test_name})
    set_property(GLOBAL PROPERTY CTAPS_TEST_TARGETS "${all_tests}")
    message(STATUS "Added test target: ${test_name}")
endfunction()

# Get CTAPS_SOURCES from parent CMakeLists.txt
list(TRANSFORM CTAPS_SOURCES PREPEND ${CMAKE_SOURCE_DIR}/)


add_gtest(udp_ping_test SOURCES src/integration/udp/udp_ping_test.cpp ${CTAPS_SOURCES} ASAN_ENABLED)
add_gtest(tcp_ping_test SOURCES src/integration/tcp/tcp_ping_test.cpp ${CTAPS_SOURCES} ASAN_ENABLED)
add_gtest(
        quic_ping_test
        SOURCES
            src/integration/quic/quic_ping_test.cpp
            ${CTAPS_SOURCES}
        ASAN_ENABLED
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
add_gtest(remote_endpoint_unit_test SOURCES src/unit/endpoint/remote_endpoint_unit_test.cpp ${CTAPS_SOURCES})
add_gtest(endpoint_util_unit_test SOURCES src/unit/endpoint/endpoint_util_unit_test.cpp ${CTAPS_SOURCES})
add_gtest(preconnection_unit_test 
  SOURCES 
    src/unit/connections/preconnection_unit_test.cpp 
    ${CTAPS_SOURCES}
)
add_gtest(connection_unit_test
  SOURCES 
    src/unit/connections/connection_unit_test.cpp
    ${CTAPS_SOURCES}
  WRAP_FUNCTIONS
    ct_message_deep_copy
    ct_message_context_new_from_connection
    ct_message_context_deep_copy
    ct_message_context_free
    ct_message_free
    socket_manager_insert_connection
    ct_socket_manager_ref
    ct_socket_manager_notify_protocol_of_priority_change
    ct_local_endpoint_resolved_equals
    ct_local_endpoint_copy_content
  ASAN_ENABLED
)

add_gtest(socket_manager_unit_test
  SOURCES 
    src/unit/connections/socket_manager_unit_test.cpp
    ${CTAPS_SOURCES}
  WRAP_FUNCTIONS
    ct_message_context_free
    ct_message_free
)

add_gtest(connection_group_unit_test 
  SOURCES
    src/unit/connections/connection_group_unit_test.cpp
    ${CTAPS_SOURCES}
  WRAP_FUNCTIONS
    ct_connection_set_active_remote_endpoint
    ct_connection_set_active_local_endpoint
    ct_socket_manager_free_connection_state
    ct_connection_abort
)
add_gtest(candidate_gathering_test
        SOURCES
            src/unit/candidate_gathering/candidate_gathering_test.cpp
            ${CTAPS_SOURCES}
        WRAP_FUNCTIONS
            ct_local_endpoint_resolve
            ct_remote_endpoint_resolve
        ASAN_ENABLED
)

add_gtest(local_endpoint_unit_test
        SOURCES
            src/unit/endpoint/local_endpoint_unit_test.cpp ${CTAPS_SOURCES}
          WRAP_FUNCTIONS
            resolve_local_endpoint_from_handle
)
add_gtest(dns_integration_test
        SOURCES
            src/integration/dns/dns_test.cpp
            ${CTAPS_SOURCES}
)

add_gtest(remote_endpoint_resolve_test
        SOURCES
            src/unit/endpoint/remote_endpoint_resolve_test.cpp
            ${CTAPS_SOURCES}
        WRAP_FUNCTIONS
            ct_remote_endpoint_resolve_cb
            perform_dns_lookup
        ASAN_ENABLED
)
add_gtest(udp_listen_test SOURCES src/integration/udp/udp_listen_test.cpp ${CTAPS_SOURCES})
add_gtest(tcp_listen_test SOURCES src/integration/tcp/tcp_listen_test.cpp ${CTAPS_SOURCES})
add_gtest(selection_properties_unit_test
  SOURCES
    src/unit/transport_properties/selection_properties/selection_properties_unit_test.cpp
    ${CTAPS_SOURCES}
)
add_gtest(message_properties_test
  SOURCES
    src/unit/transport_properties/message_properties/message_properties_test.cpp
    ${CTAPS_SOURCES}
)
add_gtest(transport_properties_test
  SOURCES
    src/unit/transport_properties/transport_properties_unit_test.cpp
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
    ${CTAPS_SOURCES}
  WRAP_FUNCTIONS
    get_service_port
    get_interface_addresses
)

add_gtest(message_context_unit_test
  SOURCES
    src/unit/message/message_context_unit_test.cpp
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

add_gtest(tcp_unit_test
  SOURCES
    src/unit/protocol/tcp_unit_test.cpp
    ${CTAPS_SOURCES}
    WRAP_FUNCTIONS
      uv_tcp_close_reset
      uv_close
      ct_message_free
      free
)

add_gtest(quic_unit_test
  SOURCES
    src/unit/protocol/quic_unit_test.cpp
    ${CTAPS_SOURCES}
    WRAP_FUNCTIONS
      picoquic_set_stream_priority
)

add_gtest(udp_unit_test
  SOURCES
    src/unit/protocol/udp_unit_test.cpp
    ${CTAPS_SOURCES}
  WRAP_FUNCTIONS
    ct_message_free
    free
)

add_gtest(message_unit_test
  SOURCES
    src/unit/message/message_unit_test.cpp
    ${CTAPS_SOURCES}
)

# Because we need root access
option(CTAPS_ENABLE_MIGRATION_TESTS "Enable migration tests (requires CAP_NET_ADMIN)" OFF)

if(CTAPS_ENABLE_MIGRATION_TESTS)
    add_test(NAME QuicMigrationCleanup COMMAND bash -c "
        iptables -D OUTPUT -p udp --dport 4433 ! -s 127.0.0.0/8 -j DROP 2>/dev/null
        iptables -D INPUT  -p udp --sport 4433 ! -d 127.0.0.0/8 -j DROP 2>/dev/null
        iptables -D OUTPUT -s 127.0.0.1 -p udp --dport 4433 -j DROP 2>/dev/null
        iptables -D OUTPUT -d 127.0.0.1 -p udp --dport 4433 -j DROP 2>/dev/null
        iptables -D INPUT  -d 127.0.0.1 -p udp --sport 4433 -j DROP 2>/dev/null
        iptables -D INPUT  -s 127.0.0.1 -p udp --sport 4433 -j DROP 2>/dev/null
        ip addr del 127.0.0.2/8 dev lo 2>/dev/null
        exit 0
    ")
    set_tests_properties(QuicMigrationCleanup PROPERTIES FIXTURES_CLEANUP QuicMigrationFixture)

    add_gtest(quic_migration_test
        SOURCES
            src/integration/quic/quic_migration_test.cpp
            ${CTAPS_SOURCES}
        TEST_PROPERTIES FIXTURES_REQUIRED QuicMigrationFixture
    )

endif()

get_property(all_tests GLOBAL PROPERTY CTAPS_TEST_TARGETS)
add_custom_target(ctaps_tests
    DEPENDS ${all_tests}
    COMMENT "Building all CTaps tests"
)

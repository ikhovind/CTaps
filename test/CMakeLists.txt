project(CTapsTests)


enable_testing()
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
            uv
            picoquic-core
    )

    target_compile_features(${test_name} PRIVATE c_std_99)
    target_include_directories(${test_name} PRIVATE
            ${CMAKE_SOURCE_DIR}/test/source
            ${CMAKE_SOURCE_DIR}/source/api
            ${CMAKE_SOURCE_DIR}/source/impl
            ${GLIB_INCLUDE_DIRS}
            ${GLIB_LIBRARIES}
    )

    target_include_directories(${test_name}
            PRIVATE
            "${picotls_SOURCE_DIR}/include"
    )

   add_test(NAME ${test_name} COMMAND ${test_name})
endfunction()

set(CTAPS_SOURCES
        ${API_SOURCES}
        ${IMPL_SOURCES}
)

list(TRANSFORM CTAPS_SOURCES PREPEND ${CMAKE_SOURCE_DIR}/)


add_gtest(udp_ping_test SOURCES source/integration/udp_ping.cpp ${CTAPS_SOURCES})
add_gtest(tcp_ping_test SOURCES source/integration/tcp/tcp_ping_test.cpp ${CTAPS_SOURCES})
add_gtest(
        quic_ping_test
        SOURCES
            source/integration/quic/quic_ping_test.cpp
            ${CTAPS_SOURCES}
        ASAN_ENABLED
)
add_gtest(quic_listener_test SOURCES source/integration/quic/quic_listen_test.cpp ${CTAPS_SOURCES} ASAN_ENABLED)
add_gtest(initiation_test SOURCES source/preconnection/initiation.cpp ${CTAPS_SOURCES} source/util/util.c)
add_gtest(remote_endpoint_unit_test SOURCES source/unit/endpoint/remote.cpp ${CTAPS_SOURCES})
add_gtest(preconnection_unit_test SOURCES source/unit/connections/preconnection.cpp ${CTAPS_SOURCES})
add_gtest(connection_unit_test SOURCES source/unit/connections/connection_test.cpp ${CTAPS_SOURCES})
add_gtest(candidate_gathering_test
        SOURCES
            source/unit/candidate_gathering/candidate_gathering_test.cpp
            ${CTAPS_SOURCES}
        WRAP_FUNCTIONS
            local_endpoint_resolve
            remote_endpoint_resolve
            get_supported_protocols
            get_num_protocols
        ASAN_ENABLED
)

add_gtest(local_endpoint_unit_test
        SOURCES
            source/unit/endpoint/local.cpp ${CTAPS_SOURCES}
)
add_gtest(dns_integration_test
        SOURCES
            source/integration/dns/dns_test.cpp
            ${CTAPS_SOURCES}
            source/util/util.c
)
add_gtest(remote_endpoint_integration_test
        SOURCES
            source/integration/endpoint/remote_endpoint_test.cpp
            ${CMAKE_SOURCE_DIR}/source/api/endpoints/remote/remote_endpoint.c
            ${CMAKE_SOURCE_DIR}/source/impl/logging/log.c
            ${CMAKE_SOURCE_DIR}/source/impl/endpoints/util.c
            ${CMAKE_SOURCE_DIR}/source/impl/protocols/udp/udp.c
            ${CMAKE_SOURCE_DIR}/source/impl/protocols/tcp/tcp.c
            ${CMAKE_SOURCE_DIR}/source/impl/protocols/quic/quic.c
            ${CMAKE_SOURCE_DIR}/source/impl/protocols/common/socket_utils.c
            ${CMAKE_SOURCE_DIR}/source/api/ctaps.c
            ${CMAKE_SOURCE_DIR}/source/api/transport_properties/transport_properties.c
            ${CMAKE_SOURCE_DIR}/source/api/transport_properties/selection_properties/selection_properties.c
            ${CMAKE_SOURCE_DIR}/source/api/transport_properties/connection_properties/connection_properties.c
            ${CMAKE_SOURCE_DIR}/source/api/connections/connection/connection.c
            ${CMAKE_SOURCE_DIR}/source/api/protocols/registry/protocol_registry.c
            ${CMAKE_SOURCE_DIR}/source/api/connections/listener/listener.c
            ${CMAKE_SOURCE_DIR}/source/impl/connections/listener/socket_manager/socket_manager.c
        WRAP_FUNCTIONS
            uv_getaddrinfo
            uv_freeaddrinfo
)
#add_gtest(udp_listener_test SOURCES source/integration/listener_test.cpp ${CTAPS_SOURCES})
add_gtest(tcp_listener_test SOURCES source/integration/tcp/tcp_listen_test.cpp ${CTAPS_SOURCES})
add_gtest(selection_property_test
  SOURCES
    source/unit/transport_properties/selection_properties/selection_properties_test.cpp
    ${CTAPS_SOURCES}
)
add_gtest(candidate_racing_test
  SOURCES
    source/integration/candidate_racing_test.cpp
    ${CTAPS_SOURCES}
)

add_gtest(local_endpoint_resolve_test
  SOURCES
    source/unit/endpoint/local/local_endpoint_resolve_test.cpp
    ${CMAKE_SOURCE_DIR}/source/api/endpoints/local/local_endpoint.c
    ${CMAKE_SOURCE_DIR}/source/impl/logging/log.c
)

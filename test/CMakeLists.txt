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
    set(options "")
    set(oneValueArgs "")
    set(multiValueArgs SOURCES WRAP_FUNCTIONS)

    # Parse the arguments passed to the function (ARGN).
    cmake_parse_arguments(ARGS "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    add_executable(${test_name} ${ARGS_SOURCES})

    foreach(func_name ${ARGS_WRAP_FUNCTIONS})
        target_link_options(${test_name} PRIVATE "-Wl,--wrap=${func_name}")
    endforeach()


    target_link_libraries(${test_name}
            PRIVATE
            gtest
            gtest_main
            gmock
            fff
            c_lib
            ${GLIB_LIBRARIES}
            uv
    )

    target_compile_features(${test_name} PRIVATE c_std_99)
    target_include_directories(${test_name} PRIVATE
            ${CMAKE_SOURCE_DIR}/test/source
            ${CMAKE_SOURCE_DIR}/source/api
            ${CMAKE_SOURCE_DIR}/source/impl
            ${GLIB_INCLUDE_DIRS}
            ${GLIB_LIBRARIES}
    )

   add_test(NAME ${test_name} COMMAND ${test_name})
endfunction()

set(CTAPS_SOURCES
        ${API_SOURCES}
        ${IMPL_SOURCES}
)

list(TRANSFORM CTAPS_SOURCES PREPEND ${CMAKE_SOURCE_DIR}/)


add_gtest(mocking_test SOURCES source/mocking_test.cpp ${CTAPS_SOURCES})
add_gtest(CTaps_test SOURCES source/integration/udp_ping.cpp ${CTAPS_SOURCES})
add_gtest(initiation_test SOURCES source/preconnection/initiation.cpp ${CTAPS_SOURCES} source/util/util.c)
add_gtest(remote_endpoint_unit_test SOURCES source/unit/endpoint/remote.cpp ${CTAPS_SOURCES})
add_gtest(preconnection_unit_test SOURCES source/unit/connections/preconnection.cpp ${CTAPS_SOURCES})
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
            ${CMAKE_SOURCE_DIR}/source/impl/endpoints/util.c
            ${CMAKE_SOURCE_DIR}/source/impl/protocols/udp/udp.c
            ${CMAKE_SOURCE_DIR}/source/api/ctaps.c
            ${CMAKE_SOURCE_DIR}/source/api/transport_properties/transport_properties.c
            ${CMAKE_SOURCE_DIR}/source/api/transport_properties/selection_properties/selection_properties.c
            ${CMAKE_SOURCE_DIR}/source/api/connections/connection/connection.c
            ${CMAKE_SOURCE_DIR}/source/api/protocols/registry/protocol_registry.c
            ${CMAKE_SOURCE_DIR}/source/api/connections/listener/socket_manager/socket_manager.c
        WRAP_FUNCTIONS
            uv_getaddrinfo

)
#add_gtest(udp_listener_test SOURCES source/integration/listener_test.cpp ${CTAPS_SOURCES})
add_gtest(selection_property_test SOURCES source/unit/transport_properties/selection_properties/selection_properties_test.cpp ${CTAPS_SOURCES})
# Need to exclude the transport_properties.c since we are mocking a function in it
add_gtest(
        transport_properties_test
        SOURCES
        source/unit/transport_properties/transport_properties_test.cpp
        ${CMAKE_SOURCE_DIR}/source/api/transport_properties/transport_properties.c
        ${CMAKE_SOURCE_DIR}/source/api/transport_properties/selection_properties/selection_properties.c
)

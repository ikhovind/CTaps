cmake_minimum_required(VERSION 3.21)

project(
    CTaps
    VERSION 0.1.0
    DESCRIPTION "A C implementation of the Transport Services as described in RFC 9622"
    HOMEPAGE_URL "https://github.com/ikhovind/ctaps"
    LANGUAGES C
)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

include(tidy)
include(format)
include(dependencies)

set(API_SOURCES
    source/api/transport_properties/transport_properties.c
    source/api/transport_properties/selection_properties/selection_properties.c
    source/api/transport_properties/connection_properties/connection_properties.c
    source/api/connections/preconnection/preconnection.c
    source/api/endpoints/remote/remote_endpoint.c
    source/api/message/message.c
    source/api/connections/connection/connection.c
    source/api/protocols/registry/protocol_registry.c
    source/api/ctaps.c
    source/api/endpoints/local/local_endpoint.c
    source/api/connections/listener/listener.c
)

set(IMPL_SOURCES
    source/impl/protocols/udp/udp.c
    source/impl/protocols/tcp/tcp.c
    source/impl/protocols/quic/quic.c
    source/impl/connections/listener/socket_manager/socket_manager.c
    source/impl/endpoints/util.c
    source/impl/endpoints/port_util.c
    source/impl/logging/log.c
    source/impl/candidate_gathering/candidate_gathering.c
)

add_library(
        CTaps SHARED
        ${API_SOURCES}
        ${IMPL_SOURCES}
)

target_include_directories(CTaps
        PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/source/api>
        $<INSTALL_INTERFACE:include> # Path after installation
)

target_include_directories(CTaps
        PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/source/impl
)

target_link_libraries(
        CTaps
        PRIVATE
            uv
            picoquic-core
)

find_package(PkgConfig REQUIRED)

pkg_check_modules(GLIB REQUIRED glib-2.0)

target_include_directories(CTaps PUBLIC ${GLIB_INCLUDE_DIRS})
target_link_libraries(CTaps PUBLIC ${GLIB_LIBRARIES})


# ---- library for testing etc. -----


# ---- Declare executable ----

add_executable(mocking_exe source/main.c)

target_link_libraries(mocking_exe
        PRIVATE
        CTaps
)

set_property(TARGET mocking_exe PROPERTY OUTPUT_NAME mocking)

target_compile_features(mocking_exe PRIVATE c_std_99)

enable_testing()

add_subdirectory (test)

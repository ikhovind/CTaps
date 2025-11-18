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
set(CMAKE_MESSAGE_LOG_LEVEL DEBUG)

include(tidy)
include(format)
include(dependencies)

set(CTAPS_SOURCES
    # API implementations
    source/impl/api/transport_properties/transport_properties.c
    source/impl/api/transport_properties/selection_properties/selection_properties.c
    source/impl/api/transport_properties/connection_properties/connection_properties.c
    source/impl/api/security_parameters/security_parameters.c
    source/impl/api/connections/preconnection/preconnection.c
    source/impl/api/endpoints/remote/remote_endpoint.c
    source/impl/api/message/message.c
    source/impl/api/connections/connection/connection.c
    source/impl/api/protocols/registry/protocol_registry.c
    source/impl/api/state/ctaps_state.c
    source/impl/api/endpoints/local/local_endpoint.c
    source/impl/api/connections/listener/listener.c
    # Protocol implementations
    source/impl/protocols/udp/udp.c
    source/impl/protocols/tcp/tcp.c
    source/impl/protocols/quic/quic.c
    source/impl/protocols/common/socket_utils.c
    # Internal utilities
    source/impl/connections/listener/socket_manager/socket_manager.c
    source/impl/endpoints/util.c
    source/impl/endpoints/port_util.c
    source/impl/logging/log.c
    source/impl/candidate_gathering/candidate_gathering.c
    source/impl/candidate_gathering/candidate_racing.c
)

add_library(
        CTaps SHARED
        ${CTAPS_SOURCES}
)

# Set symbol visibility to hidden by default - only CT_EXTERN functions are exported
set_target_properties(CTaps PROPERTIES
    C_VISIBILITY_PRESET hidden
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

# This is the only way I have managed to link against picotls
target_include_directories(CTaps
        PUBLIC
        "${picotls_SOURCE_DIR}/include"
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

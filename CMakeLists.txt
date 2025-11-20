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
    # Transport properties
    src/transport_property/transport_properties.c
    src/transport_property/selection_properties/selection_properties.c
    src/transport_property/connection_properties/connection_properties.c
    src/transport_property/message_properties/message_properties.c
    # Security parameters
    src/security_parameter/security_parameters.c
    # Connections
    src/connection/preconnection.c
    src/connection/connection.c
    src/connection/listener.c
    src/connection/socket_manager/socket_manager.c
    # Endpoints
    src/endpoint/remote_endpoint.c
    src/endpoint/local_endpoint.c
    src/endpoint/util.c
    src/endpoint/port_util.c
    # Messages
    src/message/message.c
    # Protocols
    src/protocol/protocol_registry.c
    src/protocol/udp/udp.c
    src/protocol/tcp/tcp.c
    src/protocol/quic/quic.c
    src/protocol/common/socket_utils.c
    # State
    src/state/ctaps_state.c
    # Candidate gathering
    src/candidate_gathering/candidate_gathering.c
    src/candidate_gathering/candidate_racing.c
    # Logging
    src/logging/log.c
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
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include> # Path after installation
)

target_include_directories(CTaps
        PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# This is the only way I have managed to link against picotls
target_include_directories(CTaps
        PUBLIC
        "${picotls_SOURCE_DIR}/include"
)

target_link_libraries(
        CTaps
        PUBLIC
            uv
        PRIVATE
            picoquic-core
)

find_package(PkgConfig REQUIRED)

pkg_check_modules(GLIB REQUIRED glib-2.0)

target_include_directories(CTaps PUBLIC ${GLIB_INCLUDE_DIRS})
target_link_libraries(CTaps PUBLIC ${GLIB_LIBRARIES})


# ---- library for testing etc. -----


# ---- Declare executable ----

add_executable(mocking_exe src/main.c)

target_link_libraries(mocking_exe
        PRIVATE
        CTaps
)

set_property(TARGET mocking_exe PROPERTY OUTPUT_NAME mocking)

target_compile_features(mocking_exe PRIVATE c_std_99)

enable_testing()

add_subdirectory (test)

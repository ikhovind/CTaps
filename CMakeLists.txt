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


# ---- Build Type Configuration ----
# Set default build type to Debug if not specified
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")

# Linker flags for Release build (strip symbols)
set(CMAKE_SHARED_LINKER_FLAGS_RELEASE "-s" CACHE STRING "Shared linker flags for Release build")

include(tidy)
include(format)
include(doxygen)
include(dependencies)

set(CTAPS_SOURCES
    # Transport properties
    src/transport_property/transport_properties.c
    src/transport_property/selection_properties/selection_properties.c
    src/transport_property/connection_properties/connection_properties.c
    src/transport_property/message_properties/message_properties.c
    # Security parameters
    src/security_parameter/security_parameters.c
    src/security_parameter/certificate_bundles/certificate_bundles.c
    src/security_parameter/byte_array/byte_array.c
    # Connections
    src/connection/preconnection.c
    src/connection/connection.c
    src/connection/connection_group.c
    src/connection/listener.c
    src/connection/socket_manager/socket_manager.c
    # Endpoints
    src/endpoint/remote_endpoint.c
    src/endpoint/local_endpoint.c
    src/endpoint/util.c
    src/endpoint/port_util.c
    # Messages
    src/message/message.c
    src/message/message_context.c
    # Protocols
    src/protocol/protocol_registry.c
    src/protocol/udp/udp.c
    src/protocol/tcp/tcp.c
    src/protocol/quic/quic.c
    src/protocol/common/protocol.c
    src/protocol/common/socket_utils.c
    # State
    src/state/ctaps_state.c
    # Candidate gathering
    src/candidate_gathering/candidate_gathering.c
    src/candidate_gathering/candidate_racing.c
    # Logging
    src/logging/log.c
    # Utilities
    src/util/uuid_util.c
)

add_library(
        CTaps SHARED
        ${CTAPS_SOURCES}
)

# Set symbol visibility to hidden by default - only CT_EXTERN functions are exported
set_target_properties(CTaps PROPERTIES
    C_VISIBILITY_PRESET hidden
)

target_compile_options(CTaps PRIVATE
     -Wall -Wextra -Wpedantic -Werror
     $<$<CONFIG:Debug>:-O0 -g3>
     $<$<CONFIG:Release>:-O2 -DNDEBUG>
)

target_include_directories(CTaps
        PUBLIC
          $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
          $<INSTALL_INTERFACE:include> # Path after installation
        PRIVATE
          $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
)

find_package(PkgConfig REQUIRED)

pkg_check_modules(GLIB REQUIRED glib-2.0)
pkg_check_modules(UUID REQUIRED uuid)


# This is the only way I have managed to link against picotls
target_include_directories(CTaps
        PRIVATE 
        "${picotls_SOURCE_DIR}/include"
        ${GLIB_INCLUDE_DIRS}
        ${UUID_INCLUDE_DIRS}
)

target_link_libraries(
        CTaps
        PRIVATE
            uv
            picoquic-core
            ${GLIB_LIBRARIES}
            ${UUID_LIBRARIES}
)

enable_testing()

add_subdirectory (test)

add_subdirectory (benchmark)

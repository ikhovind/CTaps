cmake_minimum_required(VERSION 3.21)

project(
    CTaps
    VERSION 0.1.0
    DESCRIPTION "A C implementation of the Transport Services as described in RFC 9622"
    HOMEPAGE_URL "https://github.com/ikhovind/ctaps"
    LANGUAGES C
)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(tidy)
include(format)

set(API_SOURCES
    source/api/transport_properties/transport_properties.c
    source/api/transport_properties/selection_properties/selection_properties.c
    source/api/connections/preconnection/preconnection.c
    source/api/endpoints/remote/remote_endpoint.c
    source/api/message/message.c
    source/api/connections/connection/connection.c
    source/api/protocols/registry/protocol_registry.c
    source/api/ctaps.c
    source/api/endpoints/local/local_endpoint.c
)

set(IMPL_SOURCES
    source/impl/protocols/udp/udp.c
    source/api/connections/connection/connection.c
        source/api/connections/listener/listener.c
        source/api/connections/listener/listener.h
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

#target_include_directories(CTaps PUBLIC source/api)

target_link_libraries(CTaps PRIVATE uv)

find_package(PkgConfig REQUIRED)

pkg_check_modules(GLIB REQUIRED glib-2.0)

target_include_directories(CTaps PUBLIC ${GLIB_INCLUDE_DIRS})
target_link_libraries(CTaps PUBLIC ${GLIB_LIBRARIES})


# ---- library for testing etc. -----
add_library(
    c_lib SHARED
    source/lib.c
    source/mocked_lib.c
)

target_include_directories(
    c_lib ${warning_guard}
    PUBLIC
    "\$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/source>"
)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ---- Declare executable ----

add_executable(mocking_exe source/main.c)

set_property(TARGET mocking_exe PROPERTY OUTPUT_NAME mocking)

target_compile_features(mocking_exe PRIVATE c_std_99)

target_link_libraries(mocking_exe PRIVATE c_lib CTaps)

enable_testing()

add_subdirectory (test)

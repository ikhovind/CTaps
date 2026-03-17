cmake_minimum_required(VERSION 3.21)

project(ctaps_benchmark C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

find_package(Threads REQUIRED)

# Common library
add_library(benchmark_common
    src/common/timing.c
    src/common/file_generator.c
    src/common/benchmark_stats.c
)

# Common compile options for all benchmark targets
set(BENCHMARK_COMPILE_OPTIONS
    -Wall -Wextra
    $<$<CONFIG:Debug>:-O0 -g3>
    $<$<CONFIG:Release>:-O2>
)

target_compile_options(benchmark_common PUBLIC ${BENCHMARK_COMPILE_OPTIONS})

target_compile_definitions(benchmark_common PUBLIC
    RESOURCE_FOLDER=\"${CMAKE_CURRENT_SOURCE_DIR}/../test/resources\"
    _GNU_SOURCE
    $<$<CONFIG:Release>:NDEBUG>
)

target_compile_definitions(benchmark_common
    PUBLIC
    RESOURCE_FOLDER=\"${CMAKE_CURRENT_SOURCE_DIR}/../test/resources\"
    _GNU_SOURCE
)

target_include_directories(benchmark_common PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# TCP Server
add_executable(tcp_benchmark_server
    src/server/tcp_benchmark_server.c
)

target_link_libraries(tcp_benchmark_server
    benchmark_common
    Threads::Threads
)

# TCP Client
add_executable(tcp_benchmark_client
    src/client/tcp_benchmark_client.c
)

add_executable(taps_benchmark_racing_client
    src/client/taps_benchmark_racing_client.c
)

target_link_libraries(taps_benchmark_racing_client
    benchmark_common
    CTaps
)

target_link_libraries(tcp_benchmark_client
    benchmark_common
)

# QUIC Server
add_executable(quic_benchmark_server
    src/server/quic_benchmark_server.c
)

target_link_libraries(quic_benchmark_server
    benchmark_common
    picoquic-core
)

# Our picoquic benchmark client uses the built in picoquic package
# loop. This package loop only notifies picoquic about a path being
# unavailable if errno after sendmsg fails fits some internal
# picoquic criteria (that is, a list of "implies unreachable" errnos)
#
# When we use iptables to simulate a path breakdown in our benchmarking
# then errno is set to EPERM, which is not normally recognized as one
# of these "implies unreachable" error codes. We therefore created this
# special fork which has added EPERM to this list, which we use with our
# benchmark client
#
# For the TAPS implementation we don't use the picoquic package loop, so
# we are free to notify the picoquic quic engine of an unreachable endpoint
# however we see fit, so this is not needed any other places than the benchmark
# client
FetchContent_Declare(
        picoquic_fork_with_eperm
        GIT_REPOSITORY https://github.com/ikhovind/picoquic.git
        GIT_TAG 7b4c96cee25772e0811bb46ac9035d4ede441d9d
)

set(PICOQUIC_FETCH_PTLS OFF  CACHE BOOL "Fetch PicoTLS during configuration" FORCE)
set(BUILD_LOGLIB        OFF  CACHE BOOL "Build picoquic-log" FORCE)
set(BUILD_DEMO          OFF CACHE BOOL "Build picoquicdemo" FORCE)
set(BUILD_HTTP          OFF CACHE BOOL "Build picohttp" FORCE)
set(BUILD_PQBENCH       OFF CACHE BOOL "Build pqbench" FORCE)
set(BUILD_LOGREADER     OFF CACHE BOOL "Build picolog_t the log reader" FORCE)
set(PICOQUIC_FETCH_PTLS ON)

FetchContent_MakeAvailable(picoquic_fork_with_eperm)

# QUIC Client
add_executable(quic_benchmark_client
    src/client/quic_benchmark_client.c
)


target_link_libraries(quic_benchmark_client
    benchmark_common
    picoquic-core-fork-with-eperm
)

add_custom_target(benchmark ALL
    DEPENDS
        tcp_benchmark_server
        tcp_benchmark_client
        taps_benchmark_racing_client
        quic_benchmark_server
        quic_benchmark_client
)

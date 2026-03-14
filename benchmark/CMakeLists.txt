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

target_link_libraries(benchmark_common
    CTaps
)

target_compile_definitions(benchmark_common
    PUBLIC
    RESOURCE_FOLDER=\"${CMAKE_CURRENT_SOURCE_DIR}/../test/quic\"
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

target_link_libraries(tcp_benchmark_client
    benchmark_common
)

# TAPS TCP Client
add_executable(taps_benchmark_tcp_client
    src/client/taps_benchmark_tcp_client.c
    src/client/benchmark_common_taps.c
)

target_link_libraries(taps_benchmark_tcp_client
    benchmark_common
    CTaps
)

# TAPS QUIC Client
add_executable(taps_benchmark_quic_client
    src/client/taps_benchmark_quic_client.c
    src/client/benchmark_common_taps.c
)

target_link_libraries(taps_benchmark_quic_client
    benchmark_common
    CTaps
)

# QUIC Server
add_executable(quic_benchmark_server
    src/server/quic_benchmark_server.c
)

target_link_libraries(quic_benchmark_server
    benchmark_common
    picoquic-core
)

# QUIC Client
add_executable(quic_benchmark_client
    src/client/quic_benchmark_client.c
)

target_link_libraries(quic_benchmark_client
    benchmark_common
    picoquic-core
)

# Installation
install(TARGETS 
    tcp_benchmark_server
    tcp_benchmark_client
    taps_benchmark_tcp_client
    taps_benchmark_quic_client
    quic_benchmark_server
    quic_benchmark_client
    RUNTIME DESTINATION bin
)

add_custom_target(benchmark ALL
    DEPENDS
        tcp_benchmark_server
        tcp_benchmark_client
        taps_benchmark_tcp_client
        taps_benchmark_quic_client
        quic_benchmark_server
        quic_benchmark_client
)

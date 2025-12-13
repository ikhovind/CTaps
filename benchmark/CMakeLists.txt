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

target_include_directories(benchmark_common PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# TCP Server
add_executable(tcp_server
    src/server/tcp_server.c
)

target_link_libraries(tcp_server
    benchmark_common
    Threads::Threads
)

# TCP Client
add_executable(tcp_client
    src/client/tcp_client.c
)

target_link_libraries(tcp_client
    benchmark_common
)

# TAPS TCP Client
add_executable(taps_tcp_client
    src/client/taps_tcp_client.c
    src/client/common_taps.c
)

target_link_libraries(taps_tcp_client
    benchmark_common
    CTaps
)

# TAPS QUIC Client
add_executable(taps_quic_client
    src/client/taps_quic_client.c
    src/client/common_taps.c
)

target_link_libraries(taps_quic_client
    benchmark_common
    CTaps
)

target_compile_definitions(taps_quic_client
    PRIVATE
    RESOURCE_FOLDER=\"${CMAKE_CURRENT_SOURCE_DIR}/../test/quic\"
)

# QUIC Server
add_executable(quic_server
    src/server/quic_server.c
)

target_compile_definitions(quic_server
    PRIVATE
    RESOURCE_FOLDER=\"${CMAKE_CURRENT_SOURCE_DIR}/../test/quic\"
)

target_link_libraries(quic_server
    benchmark_common
    picoquic-core
)

# QUIC Client
add_executable(quic_client
    src/client/quic_client.c
)

target_link_libraries(quic_client
    benchmark_common
    picoquic-core
)

# Installation
install(TARGETS tcp_server tcp_client taps_tcp_client taps_quic_client quic_server quic_client
    RUNTIME DESTINATION bin
)

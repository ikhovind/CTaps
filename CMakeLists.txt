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

# ---- Declare library ----
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

target_link_libraries(mocking_exe PRIVATE c_lib)

enable_testing()

add_subdirectory (test)

cmake_minimum_required(VERSION 3.21)

project(
    mocking
    VERSION 0.1.0
    DESCRIPTION "Short description"
    HOMEPAGE_URL "https://example.com/"
    LANGUAGES C
)

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

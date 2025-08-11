file(GLOB_RECURSE ALL_C_FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/*.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
)

add_custom_target(format
        COMMAND clang-format -i ${ALL_C_FILES}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Formatting C files with clang-format..."
)

find_program(CLANG_TIDY_EXE clang-tidy)

if(CLANG_TIDY_EXE)
           set(CMAKE_C_CLANG_TIDY "${CLANG_TIDY_EXE};--config-file=${CMAKE_SOURCE_DIR}/.clang-tidy")
endif()

#pragma once

/**
 * @brief Contains the name of the project
 */
struct library {
  char const* name;
};

typedef struct testing123 {
  char const* name;
} testing_t;

/**
 * @brief Creates an instance of library with the name of the project
 */
struct library create_library(void);

int calls_mocked_function(void);

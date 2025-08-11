#include "lib.h"

#include "mocked_lib.h"

int calls_mocked_function(void) {
  mocked_function();
  return 42;
}

int returns_mocked_function(void) {
  return returning_mocked_function();
}

struct library create_library(void) {
  struct library lib = {"mocking"};
  return lib;
}

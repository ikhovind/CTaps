#include "lib.h"

#include <stdio.h>

#include "mocked_lib.h"

int calls_mocked_function(void)
{
  mocked_function();
  return 42;
}

struct library create_library(void)
{
  struct library lib = {"mocking"};
  return lib;
}

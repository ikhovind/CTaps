#include <stdio.h>

#include "lib.h"

int main(int argc, char const* argv[])
{
  struct library lib = create_library();

  (void)argc;
  (void)argv;

  if (printf("Hello from %s!\n", lib.name) < 0) {
    return 1;
  }

  return 0;
}

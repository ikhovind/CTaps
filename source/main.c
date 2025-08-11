#include <stdio.h>

#include "lib.h"

int main() {
  struct library lib = create_library();

  if (printf("Hello from %s!\n", lib.name) < 0) {
    return 1;
  }

  return 0;
}

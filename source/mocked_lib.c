#include <stdio.h>

void mocked_function(void) {
  printf("Mocked function\n");
}

int returning_mocked_function(void) {
  return 100;
}

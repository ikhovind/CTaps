#include "listener.h"

void listener_close(Listener* listener) {
  printf("Closing listener pointer: %p\n", listener);
  listener->socket_manager->ref_count--;
  listener->socket_manager->listener = NULL;
}

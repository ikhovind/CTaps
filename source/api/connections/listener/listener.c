#include "listener.h"

void listener_close(Listener* listener) {
  listener->protocol.stop_listen(listener);
}

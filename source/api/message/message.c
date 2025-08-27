#include "message.h"

#include <stdlib.h>
#include <string.h>

void message_build_with_content(Message* message, const char* content, size_t length) {
  message->content = malloc(length);
  message->length = length;
  memcpy(message->content, content, length);
}

void message_build_without_content(Message* message) {
  message->content = 0;
}

void message_free_all(Message* message) {
  free(message->content);
  free(message);
}

void message_free_content(const Message* message) {
  free(message->content);
}

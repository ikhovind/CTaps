#include "message.h"

#include <stdlib.h>
#include <string.h>

void ct_message_build_with_content(ct_message_t* message, const char* content, size_t length) {
  message->content = malloc(length);
  message->length = length;
  memcpy(message->content, content, length);
}

void ct_message_build_without_content(ct_message_t* message) {
  message->content = 0;
}

void ct_message_free_all(ct_message_t* message) {
  free(message->content);
  free(message);
}

void ct_message_free_content(const ct_message_t* message) {
  free(message->content);
}

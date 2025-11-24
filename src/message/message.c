#include "ctaps.h"
#include "message/message.h"

#include <logging/log.h>
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
  log_info("Freeing message of size %zu", message->length);
  free(message->content);
  free(message);
}

void ct_message_free_content(const ct_message_t* message) {
  free(message->content);
}

ct_message_t* ct_message_deep_copy(const ct_message_t* message) {
  if (!message) {
    return NULL;
  }

  ct_message_t* copy = malloc(sizeof(ct_message_t));
  if (!copy) {
    return NULL;
  }
  log_info("Deep copying message of size %zu", message->length);

  copy->length = message->length;
  copy->content = malloc(message->length);
  if (!copy->content) {
    free(copy);
    return NULL;
  }

  memcpy(copy->content, message->content, message->length);
  return copy;
}

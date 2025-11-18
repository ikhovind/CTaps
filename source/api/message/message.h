#ifndef MESSAGE_H
#define MESSAGE_H
#include <stddef.h>

typedef struct {
  char* content;
  unsigned int length;
} ct_message_t;

void ct_message_build_with_content(ct_message_t* message, const char* content, size_t length);

void ct_message_build_without_content(ct_message_t* message);

void ct_message_free_all(ct_message_t* message);

void ct_message_free_content(const ct_message_t* message);

#endif  // MESSAGE_H

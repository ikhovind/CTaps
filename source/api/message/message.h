//
// Created by ikhovind on 16.08.25.
//

#ifndef MESSAGE_H
#define MESSAGE_H
#include <stddef.h>

typedef struct {
  char* content;
  unsigned int length;
} Message;

void message_build_with_content(Message* message, const char* content, size_t length);

void message_build_without_content(Message* message);

void message_free_all(Message* message);

void message_free_content(const Message* message);

#endif  // MESSAGE_H

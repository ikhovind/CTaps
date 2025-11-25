#include "uuid_util.h"
#include <uuid/uuid.h>

void generate_uuid_string(char* uuid_str) {
  uuid_t uuid;
  uuid_generate(uuid);
  uuid_unparse(uuid, uuid_str);
}

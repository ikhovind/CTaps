#include "connection_group.h"


int ct_connection_group_add_connection(ct_connection_group_t* group, ct_connection_t* connection) {
  if (!group || !connection) {
    return -EINVAL;
  }

  int rc = g_hash_table_insert(group->connections, connection->uuid, connection);
  if (!rc) {
    return -EEXIST; // Connection already in group
  }
  group->num_active_connections++;
  return 0;
}

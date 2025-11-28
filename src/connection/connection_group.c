#include "connection_group.h"
#include <logging/log.h>


int ct_connection_group_add_connection(ct_connection_group_t* group, ct_connection_t* connection) {
  if (!group || !connection) {
    return -EINVAL;
  }

  log_debug("Adding connection with UUID %to connection group", connection->uuid);
  int rc = g_hash_table_insert(group->connections, connection->uuid, connection);
  if (!rc) {
    log_error("Connection with UUID %to already exists in group", connection->uuid);
    return -EEXIST; // Connection already in group
  }
  group->num_active_connections++;
  return 0;
}

ct_connection_t* ct_connection_group_get_first(ct_connection_group_t* group) {
  if (!group || !group->connections) {
    return NULL;
  }

  GHashTableIter iter;
  gpointer key, value;
  g_hash_table_iter_init(&iter, group->connections);

  if (g_hash_table_iter_next(&iter, &key, &value)) {
    return (ct_connection_t*)value;
  }

  return NULL;
}

void ct_connection_group_decrement_active(ct_connection_group_t* group) {
  if (group->num_active_connections > 0) {
    group->num_active_connections--;
    log_info("Decremented active connections, remaining: %u", group->num_active_connections);
  }
}

uint64_t ct_connection_group_get_num_active_connections(ct_connection_group_t* group) {
  return group->num_active_connections;
}

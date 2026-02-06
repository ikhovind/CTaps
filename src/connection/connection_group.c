#include "connection_group.h"

#include "connection/connection.h"
#include "connection/socket_manager/socket_manager.h"
#include "util/uuid_util.h"
#include <errno.h>
#include <glib.h>
#include <logging/log.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>


int ct_connection_group_add_connection(ct_connection_group_t* group, ct_connection_t* connection) {
  if (!group || !connection) {
    log_error("ct_connection_group_add_connection called with NULL parameter");
    log_debug("group: %p, connection: %p", (void*)group, (void*)connection);
    return -EINVAL;
  }

  if (connection->connection_group) {
    log_error("Connection with UUID %s already belonged to a connection group", connection->uuid);
    return -EEXIST;
  }

  log_debug("Adding connection with UUID %s connection group", connection->uuid);
  int rc = g_hash_table_insert(group->connections, connection->uuid, connection);
  if (!rc) {
    log_error("Connection with UUID %s already exists in group", connection->uuid);
    return -EEXIST; // Connection already in group
  }
  connection->connection_group = ct_connection_group_ref(group);
  group->num_active_connections++;
  return 0;
}

ct_connection_t* ct_connection_group_get_first(ct_connection_group_t* group) {
  if (!group || !group->connections) {
    log_error("ct_connection_group_get_first called with NULL parameter");
    return NULL;
  }

  GHashTableIter iter;
  gpointer key = NULL;
  gpointer value = NULL;
  g_hash_table_iter_init(&iter, group->connections);

  if (g_hash_table_iter_next(&iter, &key, &value)) {
    return (ct_connection_t*)value;
  }

  log_debug("Connection group %s is empty, no first connection", group->connection_group_id);
  return NULL;
}

void ct_connection_group_close_all(ct_connection_group_t* connection_group) {
  log_info("Closing connection group: %s", connection_group->connection_group_id);
  GHashTableIter iter;
  gpointer key = NULL;
  gpointer value = NULL;
  g_hash_table_iter_init(&iter, connection_group->connections);
  while (g_hash_table_iter_next(&iter, &key, &value)) {
    ct_connection_t* connection = (ct_connection_t*)value;
    if (!ct_connection_is_closed_or_closing(connection)) {
      log_trace("Closing member in connection group: %s", connection->uuid);
      ct_connection_close(connection);
    }
    else {
      log_trace("Member in connection group: %s was closed or closing already", connection->uuid);
    }
  }
}

void ct_connection_group_abort_all(ct_connection_group_t* connection_group) {
  log_info("Aborting connection group: %s", connection_group->connection_group_id);
  GHashTableIter iter;
  gpointer key = NULL;
  gpointer value = NULL;
  g_hash_table_iter_init(&iter, connection_group->connections);
  while (g_hash_table_iter_next(&iter, &key, &value)) {
    ct_connection_t* connection = (ct_connection_t*)value;
    if (!ct_connection_is_closed(connection)) {
      log_trace("Aborting member in connection group: %s", connection->uuid);
      ct_connection_abort(connection);
    }
    else {
      log_trace("Member in connection group: %s was closed already", connection->uuid);
    }
  }
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

int ct_connection_group_remove_connection(ct_connection_group_t* group, ct_connection_t* connection) {
  if (!group || !connection) {
    return -EINVAL;
  }

  log_debug("Removing connection with UUID %s from connection group", connection->uuid);
  gboolean removed = g_hash_table_remove(group->connections, connection->uuid);
  if (!removed) {
    log_warn("Connection with UUID %s not found in group", connection->uuid);
    return -ENOENT;
  }

  log_debug("Connection removed, remaining connections in group: %u", g_hash_table_size(group->connections));
  return 0;
}

bool ct_connection_group_is_empty(ct_connection_group_t* group) {
  if (!group || !group->connections) {
    return true;
  }
  return g_hash_table_size(group->connections) == 0;
}

void ct_connection_group_free(ct_connection_group_t* group) {
  if (!group) {
    return;
  }
  ct_socket_manager_unref(group->socket_manager);

  log_debug("Freeing connection group %s", group->connection_group_id);

  if (group->connections) {
    g_hash_table_destroy(group->connections);
    group->connections = NULL;
  }

  free(group);
}

ct_connection_t** ct_connection_get_grouped_connections(
    const ct_connection_t* connection,
    size_t* out_count
) {
  if (!connection || !out_count) {
    log_error("ct_connection_get_grouped_connections called with NULL parameter");
    if (out_count) {
      *out_count = 0;
    }
    return NULL;
  }

  ct_connection_group_t* group = connection->connection_group;
  if (!group || !group->connections) {
    log_error("Connection has no valid connection group");
    *out_count = 0;
    return NULL;
  }

  // First pass: count non-closed connections
  size_t num_active = 0;
  GHashTableIter iter;
  gpointer key, value;

  g_hash_table_iter_init(&iter, group->connections);
  while (g_hash_table_iter_next(&iter, &key, &value)) {
    ct_connection_t* conn = (ct_connection_t*)value;
    if (!ct_connection_is_closed(conn)) {
      num_active++;
    }
  }

  if (num_active == 0) {
    log_debug("No active connections in group");
    *out_count = 0;
    return NULL;
  }

  // Allocate array of connection pointers
  ct_connection_t** connections = malloc(num_active * sizeof(ct_connection_t*));
  if (!connections) {
    log_error("Failed to allocate memory for connection array");
    *out_count = 0;
    return NULL;
  }

  // Second pass: fill array with non-closed connections
  size_t idx = 0;
  g_hash_table_iter_init(&iter, group->connections);
  while (g_hash_table_iter_next(&iter, &key, &value)) {
    ct_connection_t* conn = (ct_connection_t*)value;
    if (!ct_connection_is_closed(conn)) {
      connections[idx++] = conn;
    }
  }

  log_debug("Returning %zu active connections from group %s", num_active, group->connection_group_id);
  *out_count = num_active;
  return connections;
}

void ct_connection_close_group(ct_connection_t* connection) {
  if (!connection) {
    log_error("ct_connection_close_group called with NULL connection");
    return;
  }

  ct_connection_group_t* group = connection->connection_group;
  if (!group) {
    log_error("Connection has no connection group");
    return;
  }

  log_info("Closing all connections in group via connection %s", connection->uuid);
  ct_connection_group_close_all(group);
}

void ct_connection_abort_group(ct_connection_t* connection) {
  if (!connection) {
    log_error("ct_connection_abort_group called with NULL connection");
    return;
  }

  ct_connection_group_t* group = connection->connection_group;
  if (!group) {
    log_error("Connection has no connection group");
    return;
  }

  log_info("Aborting all connections in group via connection %s", connection->uuid);
  ct_connection_group_abort_all(group);
}

ct_connection_group_t* ct_connection_group_ref(ct_connection_group_t* group) {
  if (!group) {
    log_error("ct_connection_group_ref called with NULL parameter");
    return NULL;
  }
  group->ref_count++;
  return group;
}

void ct_connection_group_unref(ct_connection_group_t* group) {
  if (!group) {
    log_error("ct_connection_group_unref called with NULL parameter");
    return;
  }
  group->ref_count--;
  if (group->ref_count == 0) {
    ct_connection_group_free(group);
  }
}

ct_connection_group_t* ct_connection_group_new(ct_socket_manager_t* socket_manager, const ct_remote_endpoint_t* remote_endpoint) {
  ct_connection_group_t* group = malloc(sizeof(ct_connection_group_t));
  if (!group) {
    log_error("Failed to allocate memory for connection group");
    return NULL;
  }
  memset(group, 0, sizeof(ct_connection_group_t));
  generate_uuid_string(group->connection_group_id);

  int rc = socket_manager_insert_connection_group(socket_manager, remote_endpoint, group);
  if (rc < 0) {
    log_error("Failed to insert connection group into socket manager: %d", rc);
    free(group);
    return NULL;
  }
  group->connections = g_hash_table_new(g_str_hash, g_str_equal);

  return group;
}

void ct_connection_group_mark_all_as_closed(ct_connection_group_t* group) {
  if (!group || !group->connections) {
    log_error("ct_connection_group_mark_all_as_closed called with NULL parameter");
    return;
  }

  GHashTableIter iter;
  gpointer key = NULL;
  gpointer value = NULL;
  g_hash_table_iter_init(&iter, group->connections);
  while (g_hash_table_iter_next(&iter, &key, &value)) {
    ct_connection_t* connection = (ct_connection_t*)value;
    ct_connection_mark_as_closed(connection);
  }
}

#include "connection_group.h"

#include "ctaps.h"
#include <errno.h>
#include <glib.h>
#include <logging/log.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>


int ct_connection_group_add_connection(ct_connection_group_t* group, ct_connection_t* connection) {
  if (!group || !connection) {
    return -EINVAL;
  }

  log_debug("Adding connection with UUID %s connection group", connection->uuid);
  int rc = g_hash_table_insert(group->connections, connection->uuid, connection);
  if (!rc) {
    log_error("Connection with UUID %s already exists in group", connection->uuid);
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
  gpointer key = NULL;
  gpointer value = NULL;
  g_hash_table_iter_init(&iter, group->connections);

  if (g_hash_table_iter_next(&iter, &key, &value)) {
    return (ct_connection_t*)value;
  }

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

  log_debug("Freeing connection group %s", group->connection_group_id);

  if (group->connections) {
    g_hash_table_destroy(group->connections);
    group->connections = NULL;
  }

  // TODO: maybe we have to free connection_group_state in protocol interface?

  free(group);
}

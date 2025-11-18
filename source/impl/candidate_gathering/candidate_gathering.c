#include "candidate_gathering.h"

#include <glib.h>
#include <stdlib.h>
#include <stdbool.h>
#include "candidate_gathering.h" // Assuming the struct definition is in this file
#include "connections/preconnection/preconnection.h"
#include "endpoints/local/local_endpoint.h"
#include "endpoints/remote/remote_endpoint.h"
#include "protocols/protocol_interface.h"
#include "transport_properties/transport_properties.h"
#include <string.h>
#include <stdio.h>
#include <logging/log.h>

#include "protocols/registry/protocol_registry.h"

typedef struct ct_node_pruning_data_t {
  ct_selection_properties_t selection_properties;
  GList* undesirable_nodes;
} ct_node_pruning_data_t;

void build_candidate_tree_recursive(GNode* parent_node);

gboolean free_candidate_node(GNode *node, gpointer user_data) {
  const ct_candidate_node_t* candidate_node = (ct_candidate_node_t*)node->data;
  if (candidate_node->local_endpoint) {
    ct_free_local_endpoint(candidate_node->local_endpoint);
  }
  if (candidate_node->remote_endpoint) {
    ct_free_remote_endpoint(candidate_node->remote_endpoint);
  }
  free(node->data);
  return false;
}

const char* get_generic_interface_type(const char* system_interface_name) {
  log_debug("Getting generic interface type for system interface name: %s", system_interface_name);
  // This is an example of a simple mapping.
  // A real implementation would have a more comprehensive mapping.
  if (strncmp(system_interface_name, "wl", strlen("wl")) == 0) {
    return "Wi-Fi";
  }
  if (strncmp(system_interface_name, "en", strlen("en")) == 0) {
    return "Ethernet";
  }
  if (strcmp(system_interface_name, "lo") == 0) {
    return "Loopback";
  }
  log_trace("No generic interface type found for %s", system_interface_name);
  return NULL;
}

bool protocol_implementation_supports_selection_properties(
  const ct_protocol_implementation_t* protocol,
  const ct_selection_properties_t* selection_properties) {
  for (int i = 0; i < SELECTION_PROPERTY_END; i++) {
    ct_selection_property_t desired_value = selection_properties->selection_property[i];
    ct_selection_property_t protocol_value = protocol->selection_properties.selection_property[i];

    if (desired_value.type == TYPE_PREFERENCE) {
      if (desired_value.value.simple_preference == REQUIRE && protocol_value.value.simple_preference == PROHIBIT) {
        return false;
      }
      if (desired_value.value.simple_preference == PROHIBIT && protocol_value.value.simple_preference == REQUIRE) {
        return false;
      }
    }
  }
  return true;
}

bool interface_is_compatible(const char* interface_name, const ct_transport_properties_t* transport_properties) {
  log_trace("Checking if interface %s is compatible with transport properties", interface_name);
  // iterate over the interface preferences
  GHashTable* interface_map = (GHashTable*)transport_properties->selection_properties.selection_property[INTERFACE].value.preference_map;
  if (interface_map == NULL) {
    // No preferences set, all interfaces are compatible
    return true;
  }
  if (strcmp(interface_name, "any") == 0) {
    return true;
  }
  // iterate each value in the hash table
  GList* keys = g_hash_table_get_keys(interface_map);
  const char* interface_type = get_generic_interface_type(interface_name);
  if (interface_type == NULL) {
    log_trace("Could not determine generic interface type for %s", interface_name);
    // Unknown interface type, consider it incompatible
    g_list_free(keys);
    return false;
  }
  log_trace("Checking compatibility for generic interface type: %s", interface_type);
  for (GList* iter = keys; iter != NULL; iter = iter->next) {
    char* key = (char*)iter->data;
    ct_selection_preference_t preference = GPOINTER_TO_INT(g_hash_table_lookup(interface_map, key));
    log_trace("Preference for interface type %s is %d", key, preference);
    if (strcmp(key, interface_type) == 0) {
      if (preference == PROHIBIT) {
        g_list_free(keys);
        log_trace("Interface %s is prohibited", interface_name);
        return false;
      }
    }
    else {
      // If any other interface is set to REQUIRE, this one is incompatible
      if (preference == REQUIRE) {
        g_list_free(keys);
        log_trace("Interface %s is incompatible due to %s being required", interface_name, key);
        return false;
      }
    }
  }
  g_list_free(keys);
  log_trace("Interface %s is compatible", interface_name);
  return true;
}



gboolean gather_incompatible_path_nodes(GNode *node, gpointer user_data) {
  log_trace("Traversing candidate tree to gather incompatible path nodes");
  struct ct_candidate_node_t* node_data = (struct ct_candidate_node_t*)node->data;
  if (node_data->type == NODE_TYPE_ROOT) {
    return false;
  }
  if (node_data->type != NODE_TYPE_PATH) {
    // No need to traverse further down
    return true;
  }
  struct ct_node_pruning_data_t* pruning_data = (struct ct_node_pruning_data_t*)user_data;
  char* interface_name = "any";

  if (node_data->local_endpoint->interface_name != NULL) {
    interface_name = node_data->local_endpoint->interface_name;
  }
  if (!interface_is_compatible(interface_name, node_data->transport_properties)) {
    log_trace("Found incompatible path node with interface %s", interface_name);
    pruning_data->undesirable_nodes = g_list_append(pruning_data->undesirable_nodes, node);
  }
  else {
    log_trace("Path node with interface %s is compatible", interface_name);
  }
  return false;
}

gboolean gather_incompatible_protocol_nodes(GNode *node, gpointer user_data) {
  log_trace("Traversing candidate tree to gather incompatible protocol nodes");
  const struct ct_candidate_node_t* node_data = (struct ct_candidate_node_t*)node->data;
  if (node_data->type == NODE_TYPE_ROOT || node_data->type == NODE_TYPE_PATH) {
    log_trace("Skipping node since it is not a protocol node");
    return false;
  }
  if (node_data->type == NODE_TYPE_ENDPOINT) {
    log_trace("Done traversing tree for finding incompatible protocol nodes");
    // No need to traverse further down
    return true;
  }

  log_trace("Checking protocol node with protocol %s", node_data->protocol->name);
  ct_node_pruning_data_t* pruning_data = (ct_node_pruning_data_t*)user_data;
  if (!protocol_implementation_supports_selection_properties(node_data->protocol, &node_data->transport_properties->selection_properties)) {
    log_trace("Found incompatible protocol node with protocol %s", node_data->protocol->name);
    pruning_data->undesirable_nodes = g_list_append(pruning_data->undesirable_nodes, node);
  }
  else {
    log_trace("Protocol node with protocol %s is compatible", node_data->protocol->name);
  }
  return false;
}

int prune_candidate_tree(GNode* root, ct_selection_properties_t selection_properties) {
  log_debug("Pruning candidate tree based on selection properties");

  ct_node_pruning_data_t pruning_data = {
    .selection_properties = selection_properties,
    .undesirable_nodes = NULL // This is fince since g_list_append handles initialization
  };


  // Prune from the bottom up, otherwise we may remove a parent first, then try to remove the child
  log_trace("About to gather incompatible protocol nodes");
  g_node_traverse(root, G_LEVEL_ORDER, G_TRAVERSE_NON_LEAVES, -1, gather_incompatible_protocol_nodes, &pruning_data);

  log_trace("About to gather incompatible path nodes");
  g_node_traverse(root, G_LEVEL_ORDER, G_TRAVERSE_NON_LEAVES, -1, gather_incompatible_path_nodes, &pruning_data);

  log_trace("Total nodes in tree before pruning: %d", g_node_n_nodes(root, G_TRAVERSE_ALL));
  GList* current_node_list = pruning_data.undesirable_nodes;
  while (current_node_list != NULL) {
    GNode* node_to_remove = (GNode*)current_node_list->data;
    GList* next_iter = current_node_list->next;

    g_node_traverse(node_to_remove, G_IN_ORDER, G_TRAVERSE_ALL, -1, free_candidate_node, NULL);
    // First remove it from the tree, to avoid trying to free it twice, when later freeing a parent
    g_node_unlink(node_to_remove);
    g_node_destroy(node_to_remove);

    current_node_list = next_iter;
  }

  log_trace("Total nodes in tree after pruning: %d", g_node_n_nodes(root, G_TRAVERSE_ALL));
  return 0;
}

gint compare_prefer_and_avoid_preferences(gconstpointer a, gconstpointer b, gpointer desired_selection_properties) {
  log_trace("In candidate sorting - comparing two candidate nodes based on prefer and avoid selection properties");

  const ct_candidate_node_t* candidate_a = (const ct_candidate_node_t*)a;
  const ct_candidate_node_t* candidate_b = (const ct_candidate_node_t*)b;
  const ct_selection_properties_t* selection_properties = (const ct_selection_properties_t*)desired_selection_properties;

  // order the branches according to the preferred Properties and use any avoided Properties as a tiebreaker
  int a_prefer_score = 0;
  int a_avoid_score = 0;
  for (int i = 0; i < SELECTION_PROPERTY_END; i++) {
    if (selection_properties->selection_property[i].type == TYPE_PREFERENCE) {
      if (selection_properties->selection_property[i].value.simple_preference == PREFER) {
        log_trace("Found PREFER property at index %d", i);
        // If A can provide this property then bump its score
        if (candidate_a->protocol->selection_properties.selection_property[i].value.simple_preference != PROHIBIT) {
          log_trace("A could supply prefer property at index %d", i);
          a_prefer_score++;
        }
        // But if B can provide it too, then do not bump it after all
        // If A cannot provide but B can, then A's score should decrease
        if (candidate_b->protocol->selection_properties.selection_property[i].value.simple_preference != PROHIBIT) {
          log_trace("B could supply prefer property at index %d", i);
          log_trace("B simple preference: %d", candidate_b->transport_properties->selection_properties.selection_property[i].value.simple_preference);
          a_prefer_score--;
        }
      }
      else if (selection_properties->selection_property[i].value.simple_preference == AVOID) {
        log_trace("Found AVOID property at index %d", i);

        if (candidate_a->protocol->selection_properties.selection_property[i].value.simple_preference != REQUIRE) {
          log_trace("A could unsupply avoid property at index %d", i);
          a_avoid_score++;
        }
        if (candidate_b->protocol->selection_properties.selection_property[i].value.simple_preference != REQUIRE) {
          log_trace("B could unsupply avoid property at index %d", i);
          a_avoid_score--;
        }
      }
    }
  }
  //  "The function should return a negative integer if the first value comes before the second"
  if (a_prefer_score != 0) {
    log_trace("Prefer score difference: %d", a_prefer_score);
    return -a_prefer_score;
  }
  log_trace("No prefer score difference, using avoid score difference: %d", a_avoid_score);
  return -a_avoid_score;
}

int sort_candidate_tree(GArray* root, ct_selection_properties_t selection_properties) {
  log_debug("Sorting candidate tree based on selection properties");
  // TODO - give more importance to properties set by the user

}


/**
 * @brief Creates a new candidate_node.
 * * @param type The type of the node (e.g., NODE_TYPE_ROOT).
 * @param local_ep The local endpoint data.
 * @param remote_ep The remote endpoint data.
 * @param proto The protocol implementation.
 * @param props The transport properties.
 * @return A new candidate_node object.
 */
struct ct_candidate_node_t* candidate_node_new(ct_node_type_t type,
                                         const ct_local_endpoint_t* local_ep,
                                         const ct_remote_endpoint_t* remote_ep,
                                         const ct_protocol_implementation_t* proto,
                                         const ct_transport_properties_t* props) {
  log_info("Creating new candidate node of type %d", type);
  ct_candidate_node_t* node = malloc(sizeof(struct ct_candidate_node_t));
  if (node == NULL) {
    log_error("Could not allocate memory for ct_candidate_node_t");
    return NULL;
  }
  node->type = type;
  node->score = 0;
  node->local_endpoint = local_endpoint_copy(local_ep);
  if (node->local_endpoint == NULL) {
    log_error("Could not copy local endpoint for ct_candidate_node_t");
    free(node);
    return NULL;
  }

  node->remote_endpoint = remote_endpoint_copy(remote_ep);
  if (node->remote_endpoint == NULL) {
    log_error("Could not allocate memory for remote_endpoint");
    ct_free_local_endpoint(node->local_endpoint);
    free(node);
    return NULL;
  }

  node->protocol = proto;
  node->transport_properties = props;
  return node;
}

gboolean get_leaf_nodes(GNode *node, gpointer user_data) {
  log_trace("Iterating candidate tree for getting candidate leaf nodes");
  GArray* node_array = user_data;

  if (((ct_candidate_node_t*)node->data)->type == NODE_TYPE_ENDPOINT) {
    log_trace("Found candidate node of type ENDPOINT in candidate tree, adding to output array");
    ct_candidate_node_t candidate_node = *(ct_candidate_node_t*)node->data;
    candidate_node.local_endpoint = local_endpoint_copy(candidate_node.local_endpoint);
    candidate_node.remote_endpoint = remote_endpoint_copy(candidate_node.remote_endpoint);

    g_array_append_val(node_array, candidate_node);
  }

  return false;
}

/**
 * @brief Get an array of candidate nodes. Internally builds a tree
 * as described in RFC9623 and then prunes it. It then gets all the
 * leaf nodes and sorts them according to preferences/avoids
 *
 * @param precon A pointer to a valid ct_preconnection_t object.
 * @return A GArray Containing all the candidate nodes, ordered by preference.
 */
GArray* get_ordered_candidate_nodes(const ct_preconnection_t* precon) {
  log_info("Creating root candidate node from preconnection");
  if (precon == NULL) {
    log_error("NULL preconnection provided");
    return NULL;
  }

  log_trace("ct_preconnection_t local interface name: %s", precon->local.interface_name);
  // 1. Create a new ct_candidate_node_t struct for the root
  struct ct_candidate_node_t* root_data = candidate_node_new(
    NODE_TYPE_ROOT,
    &precon->local,
    &precon->remote_endpoints[0],
    NULL, // Protocol is selected in a later stage
    &precon->transport_properties
  );

  log_info("Local port of root is: %d", root_data->local_endpoint->port);

  if (root_data == NULL) {
    log_error("Could not create root candidate node data");
    return NULL;
  }

  GNode* root_node = g_node_new(root_data);

  build_candidate_tree_recursive(root_node);

  prune_candidate_tree(root_node, precon->transport_properties.selection_properties);

  log_info("Candidate tree has been pruned, extracting leaf nodes");

  GArray *root_array = g_array_new(false, false, sizeof(ct_candidate_node_t));

  log_trace("Fetching leaf nodes from candidate tree");
  // Get leaf nodes and insert them in array
  g_node_traverse(root_node, G_IN_ORDER, G_TRAVERSE_LEAVES, -1, get_leaf_nodes, root_array);

  log_trace("Freeing undesirable nodes from candidate tree");
  // Free data owned by tree
  g_node_traverse(root_node, G_IN_ORDER, G_TRAVERSE_ALL, -1, free_candidate_node, NULL);

  g_node_destroy(root_node);

  log_trace("Sorting candidates based in desirability");
  g_array_sort_with_data(root_array, compare_prefer_and_avoid_preferences, &precon->transport_properties.selection_properties);

  if (root_array->len > 0) {
    log_trace("Most desirable candidate protocol is: %s", (g_array_index(root_array, ct_candidate_node_t, 0)).protocol->name);
  }
  else {
    log_warn("No candidate nodes found after pruning");
  }

  return root_array;
}


/**
 * @brief Recursively builds the candidate tree by applying the branching logic.
 *
 * @param parent_node The current node to expand.
 */
void build_candidate_tree_recursive(GNode* parent_node) {
  log_info("Expanding candidate tree node of type %d", ((struct ct_candidate_node_t*)parent_node->data)->type);
  struct ct_candidate_node_t* parent_data = (struct ct_candidate_node_t*)parent_node->data;

  // According to RFC 9623, the branching order should be:
  // 1. Network Paths (Local Endpoints)
  // 2. Protocol Options
  // 3. Derived Endpoints (Remote Endpoints via DNS)

  // Step 1: Branch by Network Paths (Local Endpoints)
  if (parent_data->type == NODE_TYPE_ROOT) {
    log_trace("Expanding node of type ROOT to PATH nodes");
    ct_local_endpoint_t* local_endpoint_list = NULL;
    size_t num_found_local = 0;

    log_trace("Resolving local endpoint with port: %d", parent_data->local_endpoint->port);
    // Resolve the local endpoint. The `ct_local_endpoint_resolve` function
    // will find all available interfaces when the interface is not specified.
    ct_local_endpoint_resolve(parent_data->local_endpoint, &local_endpoint_list, &num_found_local);
    log_trace("Found %zu local endpoints, adding as children to ROOT node", num_found_local);
    log_trace("Port of first is: %d", local_endpoint_list[0].port);

    for (size_t i = 0; i < num_found_local; i++) {
      // Create a child node for each local endpoint found.
      struct ct_candidate_node_t* path_node_data = candidate_node_new(
        NODE_TYPE_PATH,
        &local_endpoint_list[i],
        parent_data->remote_endpoint,
        NULL, // Protocol not yet specified
        parent_data->transport_properties
      );
      g_node_append_data(parent_node, path_node_data);

      // Recurse to the next level of the tree.
      build_candidate_tree_recursive(g_node_last_child(parent_node));
    }

    // Clean up the allocated memory for the list of local endpoints
    if (local_endpoint_list != NULL) {
      for (int i = 0; i < num_found_local; i++) {
        ct_free_local_endpoint_strings(&local_endpoint_list[i]);
      }
      log_trace("Freeing list of local endpoints after building path nodes");
      free(local_endpoint_list);
    }
  }

  // Step 2: Branch by Protocols, they are pruned later
  else if (parent_data->type == NODE_TYPE_PATH) {
    log_trace("Expanding node of type PATH to PROTOCOL nodes");
    size_t num_found_protocols = 0;

    // Get all protocols that fit the selection properties.
    ct_protocol_implementation_t **candidate_stacks = ct_get_supported_protocols();
    num_found_protocols = ct_get_num_protocols(); // Assume one protocol for demonstration purposes.
    log_trace("Found %d candidate protocols", num_found_protocols);

    for (int i = 0; i < num_found_protocols; i++) {
      // Create a child node for each supported protocol.
      ct_candidate_node_t* proto_node_data = candidate_node_new(
        NODE_TYPE_PROTOCOL,
        parent_data->local_endpoint,
        parent_data->remote_endpoint,
        candidate_stacks[i],
        parent_data->transport_properties
      );
      g_node_append_data(parent_node, proto_node_data);

      // Recurse to the next level of the tree.
      build_candidate_tree_recursive(g_node_last_child(parent_node));
    }
  }

  // Step 3: Branch by Resolved Endpoints (DNS Lookup)
  else if (parent_data->type == NODE_TYPE_PROTOCOL) {
    log_trace("Expanding node of type PROTOCOL to ENDPOINT nodes");
    ct_remote_endpoint_t* resolved_remote_endpoints = NULL;
    size_t num_found_remote = 0;

    // Resolve the remote endpoint (hostname to IP address).
    ct_remote_endpoint_resolve(parent_data->remote_endpoint, &resolved_remote_endpoints, &num_found_remote);

    for (size_t i = 0; i < num_found_remote; i++) {
      // Create a leaf node for each resolved IP address.
      ct_candidate_node_t* leaf_node_data = candidate_node_new(
        NODE_TYPE_ENDPOINT,
        parent_data->local_endpoint,
        &resolved_remote_endpoints[i],
        parent_data->protocol,
        parent_data->transport_properties
      );
      log_info("leaf node data local endpoint interface_name: %s", leaf_node_data->local_endpoint->interface_name);
      g_node_append_data(parent_node, leaf_node_data);
    }

    // Clean up the allocated memory for the list of remote endpoints.
    if (resolved_remote_endpoints != NULL) {
      log_trace("Freeing list of remote endpoints after building leaf nodes");
      free(resolved_remote_endpoints);
    }
  }
}

void free_candidate_array(GArray* candidate_array) {
  for (int i = 0; i < candidate_array->len; i++) {
    const ct_candidate_node_t candidate_node = g_array_index(candidate_array, ct_candidate_node_t, i);
    ct_free_local_endpoint(candidate_node.local_endpoint);
    ct_free_remote_endpoint(candidate_node.remote_endpoint);
  }
  g_array_free(candidate_array, true);
}

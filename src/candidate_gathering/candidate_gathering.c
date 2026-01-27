#include "candidate_gathering.h"

#include "connection/preconnection.h"
#include "endpoint/local_endpoint.h"
#include "ctaps.h"
#include "ctaps_internal.h"
#include <glib.h>
#include <logging/log.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


typedef struct ct_node_pruning_data_t {
  ct_selection_properties_t selection_properties;
  GList* undesirable_nodes;
} ct_node_pruning_data_t;

typedef struct ct_protocol_impl_array_s {
  const ct_protocol_impl_t** protocols;
  size_t count;
} ct_protocol_impl_array_t;

/**
  * Represents all protocol options for candidate gathering.
  *
  * branch_by_protocol function will use this to expand PATH nodes into PROTOCOL nodes.
  * Creating a PROTOCOL node for each combination, where appropriate. Add future options
  * as members in this struct and modify branch_by_protocol accordingly.
  */
typedef struct ct_protocol_options_s {
  ct_protocol_impl_array_t* protocol_impls;
  ct_string_array_value_t* alpns; // e.g., ALPN strings
} ct_protocol_options_t;

ct_protocol_options_t* ct_protocol_options_new(const ct_preconnection_t* precon);

ct_protocol_candidate_t* ct_protocol_candidate_new(const ct_protocol_impl_t* protocol_impl, const char* alpn);

ct_protocol_candidate_t* ct_protocol_candidate_copy(const ct_protocol_candidate_t* protocol_candidate);

ct_candidate_node_t* ct_candidate_node_copy(const ct_candidate_node_t* candidate_node);

void ct_protocol_candidate_free(ct_protocol_candidate_t* protocol_candidate);

void ct_protocol_options_free(ct_protocol_options_t* protocol_options);

ct_protocol_impl_array_t* ct_protocol_impl_array_new(void);

GNode* build_candidate_tree(const ct_preconnection_t* precon);
int branch_by_path(GNode* parent, const ct_local_endpoint_t* local_ep);
int branch_by_protocol_options(GNode* parent, ct_protocol_options_t* protocol_options);
int branch_by_remote(GNode* parent, const ct_remote_endpoint_t* remote_ep);

ct_protocol_impl_array_t* ct_protocol_impl_array_new(void) {
  ct_protocol_impl_array_t* protocol_array = malloc(sizeof(ct_protocol_impl_array_t));
  if (protocol_array == NULL) {
    log_error("Could not allocate memory for ct_protocol_impl_array_t");
    return NULL;
  }
  const ct_protocol_impl_t **candidate_stacks = ct_get_supported_protocols();
  size_t num_found_protocols = ct_get_num_protocols(); // Assume one protocol for demonstration purposes.
  protocol_array->protocols = candidate_stacks;
  protocol_array->count = num_found_protocols;
  return protocol_array;
}

ct_protocol_options_t* ct_protocol_options_new(const ct_preconnection_t* precon) {
  ct_protocol_options_t* options = malloc(sizeof(ct_protocol_options_t));
  if (options == NULL) {
    log_error("Could not allocate memory for ct_protocol_options_t");
    return NULL;
  }
  memset(options, 0, sizeof(ct_protocol_options_t));
  if (precon->security_parameters && precon->security_parameters->security_parameters[ALPN].value.array_of_strings) {
    options->alpns = precon->security_parameters->security_parameters[ALPN].value.array_of_strings;
  }
  options->protocol_impls = ct_protocol_impl_array_new();
  return options;
}

gboolean free_candidate_node(GNode *node, gpointer user_data) {
  (void)user_data;
  const ct_candidate_node_t* candidate_node = (ct_candidate_node_t*)node->data;
  if (candidate_node->local_endpoint) {
    ct_local_endpoint_free(candidate_node->local_endpoint);
  }
  if (candidate_node->remote_endpoint) {
    ct_remote_endpoint_free(candidate_node->remote_endpoint);
  }
  ct_protocol_candidate_free(candidate_node->protocol_candidate);
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
  const ct_protocol_impl_t* protocol,
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

  if (local_endpoint_get_interface_name(node_data->local_endpoint) != NULL) {
    interface_name = local_endpoint_get_interface_name(node_data->local_endpoint);
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

  log_trace("Checking protocol node with protocol %s", node_data->protocol_candidate->protocol_impl->name);
  ct_node_pruning_data_t* pruning_data = (ct_node_pruning_data_t*)user_data;
  if (!protocol_implementation_supports_selection_properties(node_data->protocol_candidate->protocol_impl, &node_data->transport_properties->selection_properties)) {
    log_trace("Found incompatible protocol node with protocol %s", node_data->protocol_candidate->protocol_impl->name);
    pruning_data->undesirable_nodes = g_list_append(pruning_data->undesirable_nodes, node);
  }
  else {
    log_trace("Protocol node with protocol %s is compatible", node_data->protocol_candidate->protocol_impl->name);
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
  g_list_free(pruning_data.undesirable_nodes);

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
        if (candidate_a->protocol_candidate->protocol_impl->selection_properties.selection_property[i].value.simple_preference != PROHIBIT) {
          log_trace("A could supply prefer property at index %d", i);
          a_prefer_score++;
        }
        // But if B can provide it too, then do not bump it after all
        // If A cannot provide but B can, then A's score should decrease
        if (candidate_b->protocol_candidate->protocol_impl->selection_properties.selection_property[i].value.simple_preference != PROHIBIT) {
          log_trace("B could supply prefer property at index %d", i);
          a_prefer_score--;
        }
      }
      else if (selection_properties->selection_property[i].value.simple_preference == AVOID) {
        log_trace("Found AVOID property at index %d", i);

        if (candidate_a->protocol_candidate->protocol_impl->selection_properties.selection_property[i].value.simple_preference != REQUIRE) {
          log_trace("A could unsupply avoid property at index %d", i);
          a_avoid_score++;
        }
        if (candidate_b->protocol_candidate->protocol_impl->selection_properties.selection_property[i].value.simple_preference != REQUIRE) {
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
                                         const ct_protocol_candidate_t* proto,
                                         const ct_transport_properties_t* props) {
  log_debug("Creating new candidate node of type %d", type);
  ct_candidate_node_t* node = malloc(sizeof(struct ct_candidate_node_t));
  if (node == NULL) {
    log_error("Could not allocate memory for ct_candidate_node_t");
    return NULL;
  }
  memset(node, 0, sizeof(struct ct_candidate_node_t));
  node->type = type;
  node->score = 0;
  if (local_ep) {
    node->local_endpoint = local_endpoint_copy(local_ep);
    if (node->local_endpoint == NULL) {
      log_error("Could not copy local endpoint for ct_candidate_node_t");
      free(node);
      return NULL;
    }
  }

  if (remote_ep) {
    node->remote_endpoint = remote_endpoint_copy(remote_ep);
    if (node->remote_endpoint == NULL) {
      log_error("Could not allocate memory for remote_endpoint");
      ct_local_endpoint_free(node->local_endpoint);
      free(node);
      return NULL;
    }
  }

  node->protocol_candidate = ct_protocol_candidate_copy(proto);
  node->transport_properties = props;
  return node;
}

gboolean get_deep_copy_leaf_nodes(GNode *node, gpointer user_data) {
  log_trace("Iterating candidate tree for getting candidate leaf nodes");
  GArray* node_array = user_data;

  if (((ct_candidate_node_t*)node->data)->type == NODE_TYPE_ENDPOINT) {
    log_trace("Found candidate node of type ENDPOINT in candidate tree, adding to output array");
    ct_candidate_node_t* candidate_node = ct_candidate_node_copy(node->data);
    g_array_append_val(node_array, *candidate_node);
    free(candidate_node);
  }

  return false;
}

static gboolean collect_leaves(GNode *node, gpointer user_data) {
  GList** leaves = (GList**)user_data;
  if (G_NODE_IS_LEAF(node)) {
    *leaves = g_list_append(*leaves, node);
  }
  return false;
}

GNode* build_candidate_tree(const ct_preconnection_t* precon) {
  struct ct_candidate_node_t* root = candidate_node_new(
    NODE_TYPE_ROOT,
    NULL, 
    NULL,
    NULL,
    preconnection_get_transport_properties(precon)
  );

  if (root == NULL) {
    log_error("Could not create root candidate node data");
    return NULL;
  }

  GNode* root_node = g_node_new(root);

  if (!root_node) {
    log_error("Could not create root GNode for candidate tree");
    free(root);
    return NULL;
  }

  branch_by_path(root_node, preconnection_get_local_endpoint(precon));  

  GList* leaves = NULL; 
  g_node_traverse(root_node, G_IN_ORDER, G_TRAVERSE_LEAVES, -1, collect_leaves, &leaves);

  ct_protocol_options_t* protocol_options = ct_protocol_options_new(precon);
  for (GList* iter = leaves; iter != NULL; iter = iter->next) {
    GNode* leaf_node = (GNode*)iter->data;
    branch_by_protocol_options(leaf_node, protocol_options);
  }
  ct_protocol_options_free(protocol_options);

  g_list_free(leaves);
  leaves = NULL;

  g_node_traverse(root_node, G_IN_ORDER, G_TRAVERSE_LEAVES, -1, collect_leaves, &leaves);
  for (GList* iter = leaves; iter != NULL; iter = iter->next) {
    GNode* leaf_node = (GNode*)iter->data;
    branch_by_remote(leaf_node, precon->remote_endpoints);
  }

  g_list_free(leaves);
  return root_node;
}

int branch_by_path(GNode* parent, const ct_local_endpoint_t* local_ep) {
  ct_local_endpoint_t* local_endpoint_list = NULL;
  size_t num_found_local = 0;
  struct ct_candidate_node_t* parent_data = (struct ct_candidate_node_t*)parent->data;
  if (parent_data->type != NODE_TYPE_ROOT) {
    log_error("branch_by_path called on non-ROOT node");
    return -1;
  }

  // Resolve the local endpoint. The `ct_local_endpoint_resolve` function
  // will find all available interfaces when the interface is not specified.
  int rc = ct_local_endpoint_resolve(local_ep, &local_endpoint_list, &num_found_local);
  if (rc != 0) {
    log_error("Error resolving local endpoint");
    return rc;
  }
  log_trace("Found %zu local endpoints, adding as children to ROOT node", num_found_local);

  for (size_t i = 0; i < num_found_local; i++) {
    // Create a child node for each local endpoint found.
    struct ct_candidate_node_t* path_node_data = candidate_node_new(
      NODE_TYPE_PATH,
      &local_endpoint_list[i],
      NULL,
      NULL, // Protocol not yet specified
      parent_data->transport_properties
    );
    if (path_node_data == NULL) {
      log_error("Could not create PATH node data");
      free(local_endpoint_list);
      return -1;
    }
    g_node_append_data(parent, path_node_data);
  }
  return 0;
}

int branch_by_protocol_options(GNode* parent, ct_protocol_options_t* protocol_options) {
  struct ct_candidate_node_t* parent_data = (struct ct_candidate_node_t*)parent->data;
  if (parent_data->type != NODE_TYPE_PATH) {
    log_error("branch_by_protocol_options called on non-PATH node");
    return -1;
  }

  log_trace("Expanding node of type PATH to PROTOCOL nodes");

  for (size_t i = 0; i < protocol_options->protocol_impls->count; i++) {
    if (ct_protocol_supports_alpn(protocol_options->protocol_impls->protocols[i])
        && protocol_options->alpns
        && protocol_options->alpns->num_strings > 0) {
      // If the current protocol supports ALPN, create a candidate for each ALPN value
      // Picoquic does support passing multiple ALPNs as a single connection attempt,
      // but not if we want to support 0-rtt because then the alpn has to be passed to 
      // the picoquic_create invocation, which only takes a single value.
      // It was therefore decided to create separate
      // candidates for each ALPN value, as the added overhead is assumed to not be too high
      for (size_t j = 0; j < protocol_options->alpns->num_strings; j++) {
        log_trace("Checking QUIC protocol against selection properties");
        ct_protocol_candidate_t* candidate = ct_protocol_candidate_new(
          protocol_options->protocol_impls->protocols[i],
          protocol_options->alpns->strings[j]
        );
        if (candidate == NULL) {
          log_error("Could not create protocol candidate for protocol %s with ALPN %s",
                    protocol_options->protocol_impls->protocols[i]->name,
                    protocol_options->alpns->strings[j]);
          return -1;
        }

        // Create a child node for each supported protocol.
        ct_candidate_node_t* candidate_node = candidate_node_new(
          NODE_TYPE_PROTOCOL,
          parent_data->local_endpoint,
          NULL,
          candidate,
          parent_data->transport_properties
        );
        ct_protocol_candidate_free(candidate);  // candidate_node_new copies it
        if (candidate_node == NULL) {
          log_error("Could not create PROTOCOL node data");
          return -1;
        }
        g_node_append_data(parent, candidate_node);
      }
    }
    else {
      // If the current protocol does not support ALPN, just create a single candidate without any alpn
      ct_protocol_candidate_t* candidate = ct_protocol_candidate_new(
        protocol_options->protocol_impls->protocols[i],
        NULL
      );
      if (candidate == NULL) {
        log_error("Could not create protocol candidate for protocol %s without ALPN",
                  protocol_options->protocol_impls->protocols[i]->name);
        return -1;
      }

      ct_candidate_node_t* candidate_node = candidate_node_new(
        NODE_TYPE_PROTOCOL,
        parent_data->local_endpoint,
        NULL,
        candidate,
        parent_data->transport_properties
      );
      ct_protocol_candidate_free(candidate);  // candidate_node_new copies it

      if (candidate_node == NULL) {
        log_error("Could not create PROTOCOL node data");
        return -1;
      }
      g_node_append_data(parent, candidate_node);
    }
  }
  return 0;
}




int branch_by_remote(GNode* parent, const ct_remote_endpoint_t* remote_ep) {
  struct ct_candidate_node_t* parent_data = (struct ct_candidate_node_t*)parent->data;
  if (parent_data->type != NODE_TYPE_PROTOCOL) {
    log_error("branch_by_remote called on non-PROTOCOL node");
    return -1;
  }
  log_trace("Expanding node of type PROTOCOL to ENDPOINT nodes");
  ct_remote_endpoint_t* resolved_remote_endpoints = NULL;
  size_t num_found_remote = 0;

  // Resolve the remote endpoint (hostname to IP address).
  int rc = ct_remote_endpoint_resolve(remote_ep, &resolved_remote_endpoints, &num_found_remote);
  if (rc != 0) {
    log_error("Error resolving remote endpoint");
    return rc;
  }

  for (size_t i = 0; i < num_found_remote; i++) {
    // Create a leaf node for each resolved IP address.
    ct_candidate_node_t* leaf_node_data = candidate_node_new(
      NODE_TYPE_ENDPOINT,
      parent_data->local_endpoint,
      &resolved_remote_endpoints[i],
      parent_data->protocol_candidate,
      parent_data->transport_properties
    );
    g_node_append_data(parent, leaf_node_data);
  }

  // Clean up the allocated memory for the list of remote endpoints.
  if (resolved_remote_endpoints != NULL) {
    log_trace("Freeing list of remote endpoints after building leaf nodes");
    free(resolved_remote_endpoints);
  }
  return 0;
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
  log_info("Creating candidate node tree from preconnection");
  if (precon == NULL) {
    log_error("NULL preconnection provided");
    return NULL;
  }

  GNode* root_node = build_candidate_tree(precon);
  if (root_node == NULL) {
    log_error("Could not build candidate tree");
    return NULL;
  }

  prune_candidate_tree(root_node, preconnection_get_transport_properties(precon)->selection_properties);

  log_info("Candidate tree has been pruned, extracting leaf nodes");

  GArray *root_array = g_array_new(false, false, sizeof(ct_candidate_node_t));

  log_trace("Fetching leaf nodes from candidate tree");

  // Get deep copy of leaf candidate nodes and insert them in array
  g_node_traverse(root_node, G_IN_ORDER, G_TRAVERSE_LEAVES, -1, get_deep_copy_leaf_nodes, root_array);

  log_trace("Freeing undesirable nodes from candidate tree");

  // Free data owned by tree
  g_node_traverse(root_node, G_IN_ORDER, G_TRAVERSE_ALL, -1, free_candidate_node, NULL);

  g_node_destroy(root_node);

  log_trace("Sorting candidates based in desirability");
  g_array_sort_with_data(root_array, compare_prefer_and_avoid_preferences, (gpointer)&preconnection_get_transport_properties(precon)->selection_properties);

  if (root_array->len > 0) {
    log_trace("Most desirable candidate protocol is: %s", (g_array_index(root_array, ct_candidate_node_t, 0)).protocol_candidate->protocol_impl->name);
  }
  else {
    log_warn("No candidate nodes found after pruning");
  }

  return root_array;
}


void free_candidate_array(GArray* candidate_array) {
  for (guint i = 0; i < candidate_array->len; i++) {
    const ct_candidate_node_t candidate_node = g_array_index(candidate_array, ct_candidate_node_t, i);
    ct_local_endpoint_free(candidate_node.local_endpoint);
    ct_remote_endpoint_free(candidate_node.remote_endpoint);
    ct_protocol_candidate_free(candidate_node.protocol_candidate);
  }
  g_array_free(candidate_array, true);
}

ct_protocol_candidate_t* ct_protocol_candidate_new(const ct_protocol_impl_t* protocol_impl, const char* alpn) {
  ct_protocol_candidate_t* protocol_candidate = malloc(sizeof(ct_protocol_candidate_t));
  if (protocol_candidate == NULL) {
    log_error("Could not allocate memory for ct_protocol_candidate_t");
    return NULL;
  }
  memset(protocol_candidate, 0, sizeof(ct_protocol_candidate_t));
  if (!protocol_impl) {
    log_error("NULL protocol implementation provided to ct_protocol_candidate_new");
    free(protocol_candidate);
    return NULL;
  }

  protocol_candidate->protocol_impl = protocol_impl;
  if (alpn) {
    protocol_candidate->alpn = strdup(alpn);
    if (!protocol_candidate->alpn) {
      log_error("Could not allocate memory for ALPN string in ct_protocol_candidate_t");
      free(protocol_candidate);
      return NULL;
    }
  }
  return protocol_candidate;
}

void ct_protocol_candidate_free(ct_protocol_candidate_t* protocol_candidate) {
  if (protocol_candidate) {
    if (protocol_candidate->alpn) {
      free(protocol_candidate->alpn);
    }
    free(protocol_candidate);
  }
}

void ct_protocol_options_free(ct_protocol_options_t* protocol_options) {
  if (protocol_options) {
    if (protocol_options->protocol_impls) {
      // protocols array points to static data, don't free it
      free(protocol_options->protocol_impls);
    }
    // alpns points to security_parameters data, don't free it
    free(protocol_options);
  }
}
ct_protocol_candidate_t* ct_protocol_candidate_copy(const ct_protocol_candidate_t* protocol_candidate) {
  if (protocol_candidate == NULL) {
    return NULL;
  }
  return ct_protocol_candidate_new(protocol_candidate->protocol_impl, protocol_candidate->alpn);
}

ct_candidate_node_t* ct_candidate_node_copy(const ct_candidate_node_t* candidate_node) {
  if (candidate_node == NULL) {
    return NULL;
  }
  return candidate_node_new(
    candidate_node->type,
    candidate_node->local_endpoint,
    candidate_node->remote_endpoint,
    candidate_node->protocol_candidate,
    candidate_node->transport_properties
  );
}

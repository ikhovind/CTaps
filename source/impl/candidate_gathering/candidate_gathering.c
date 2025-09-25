#include "candidate_gathering.h"

#include <glib.h>
#include <stdlib.h>
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

struct node_pruning_data {
    SelectionProperties selection_properties;
    GList* undesirable_nodes;
};

// Forward declaration of the function to build the tree recursively
void build_candidate_tree_recursive(GNode* parent_node);

const char* get_generic_interface_type(const char* system_interface_name) {
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
    return NULL;
}

bool protocol_implementation_supports_selection_properties2(
  const ProtocolImplementation* protocol,
  const SelectionProperties* selection_properties) {
  for (int i = 0; i < SELECTION_PROPERTY_END; i++) {
    SelectionProperty desired_value = selection_properties->selection_property[i];
    SelectionProperty protocol_value = protocol->selection_properties.selection_property[i];

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

bool interface_is_compatible(const char* interface_name, const TransportProperties* transport_properties) {
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
        // Unknown interface type, consider it incompatible
        g_list_free(keys);
        return false;
    }
    for (GList* iter = keys; iter != NULL; iter = iter->next) {
        char* key = (char*)iter->data;
        SelectionPreference preference = GPOINTER_TO_INT(g_hash_table_lookup(interface_map, key));
        if (strcmp(key, interface_type) == 0) {
            if (preference == PROHIBIT) {
                g_list_free(keys);
                return false;
            }
        }
        else {
            // If any other interface is set to REQUIRE, this one is incompatible
            if (preference == REQUIRE) {
                g_list_free(keys);
                return false;
            }
        }
    }
    return true;
}



gboolean gather_incompatible_path_nodes(GNode *node, gpointer user_data) {
    log_trace("Traversing candidate tree to gather incompatible path nodes");
    struct candidate_node* node_data = (struct candidate_node*)node->data;
    if (node_data->type == NODE_TYPE_ROOT) {
        return false;
    }
    if (node_data->type != NODE_TYPE_PATH) {
        // No need to traverse further down
        return true;
    }
    struct node_pruning_data* pruning_data = (struct node_pruning_data*)user_data;
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
    const struct candidate_node* node_data = (struct candidate_node*)node->data;
    if (node_data->type == NODE_TYPE_ROOT || node_data->type == NODE_TYPE_PATH) {
        return false;
    }
    if (node_data->type != NODE_TYPE_PROTOCOL) {
        // No need to traverse further down
        return true;
    }

    struct node_pruning_data* pruning_data = (struct node_pruning_data*)user_data;
    if (!protocol_implementation_supports_selection_properties2(node_data->protocol, &node_data->transport_properties->selection_properties)) {
        log_trace("Found incompatible protocol node with protocol %s", node_data->protocol->name);
        pruning_data->undesirable_nodes = g_list_append(pruning_data->undesirable_nodes, node);
    }
    else {
        log_trace("Protocol node with protocol %s is compatible", node_data->protocol->name);
    }
    return false;
}

int prune_candidate_tree(GNode* root, SelectionProperties selection_properties) {
    log_debug("Pruning candidate tree based on selection properties");

    struct node_pruning_data pruning_data = {
        .selection_properties = selection_properties,
        .undesirable_nodes = NULL
    };

    // step 1 prune paths (local endpoints at the PATH level)
    log_trace("About to gather incompatible path nodes");
    g_node_traverse(root, G_LEVEL_ORDER, G_TRAVERSE_NON_LEAVES, -1, gather_incompatible_path_nodes, &pruning_data);

    log_trace("About to gather incompatible protocol nodes");
    g_node_traverse(root, G_LEVEL_ORDER, G_TRAVERSE_NON_LEAVES, -1, gather_incompatible_protocol_nodes, &pruning_data);

    for (GList* iter = pruning_data.undesirable_nodes; iter != NULL; iter = iter->next) {
        GNode* node_to_remove = (GNode*)iter->data;
        g_node_destroy(node_to_remove);
    }
    return 0;
}

int sort_candidate_tree(GNode* root, SelectionProperties selection_properties) {

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
struct candidate_node* candidate_node_new(NodeType type, const LocalEndpoint* local_ep,
                                           const RemoteEndpoint* remote_ep,
                                           const ProtocolImplementation* proto,
                                           const TransportProperties* props) {
    log_info("Creating new candidate node of type %d", type);
    struct candidate_node* node = malloc(sizeof(struct candidate_node));
    if (node == NULL) {
        log_error("Could not allocate memory for candidate_node");
        return NULL;
    }
    node->type = type;
    node->score = 0;
    node->local_endpoint = local_ep;
    node->remote_endpoint = remote_ep;
    node->protocol = proto;
    node->transport_properties = props;
    return node;
}

/**
 * @brief Creates the root node of the candidate tree from a Preconnection.
 * * This function takes a fully configured Preconnection and uses its
 * properties to create the root node of a candidate tree. The root node
 * represents the highest level of abstraction before branching begins.
 *
 * @param precon A pointer to a valid Preconnection object.
 * @return A GNode representing the root of the candidate tree, or NULL on error.
 */
GNode* create_root_candidate_node(const Preconnection* precon) {
    log_info("Creating root candidate node from preconnection");
    if (precon == NULL) {
        log_error("NULL preconnection provided");
        return NULL;
    }

    // 1. Create a new candidate_node struct for the root
    struct candidate_node* root_data = candidate_node_new(
        NODE_TYPE_ROOT,
        &precon->local,
        (precon->num_remote_endpoints > 0) ? &precon->remote_endpoints[0] : NULL,
        NULL, // Protocol is selected in a later stage
        &precon->transport_properties
    );
    log_info("About to check precon");
    log_info("precon->local.interface_name: %s", precon->local.interface_name);

    if (root_data == NULL) {
        log_error("Could not create root candidate node data");
        return NULL; // Handle memory allocation failure
    }

    // 2. Create and return a GNode with the root data
    log_trace("Creating root candidate node");
    GNode* root_node = g_node_new(root_data);

    log_trace("Building candidate tree recursively from root");
    build_candidate_tree_recursive(root_node);

    log_trace("Successfully built candidate tree, pruning");
    prune_candidate_tree(root_node, precon->transport_properties.selection_properties);

    return root_node;
}


/**
 * @brief Expands the candidate tree from a root node for racing.
 *
 * This function takes the root node of the candidate tree and populates it
 * with all possible combinations of local endpoints, protocols, and resolved
 * remote endpoints, following the hierarchical structure described in RFC 9623.
 * The resulting tree contains leaf nodes that represent fully-specified
 * connection candidates ready for racing.
 *
 * @param root_node The root GNode containing the initial preconnection data.
 */
void expand_candidate_tree_for_racing(GNode* root_node) {
    if (root_node == NULL) {
        return;
    }
    build_candidate_tree_recursive(root_node);
}

/**
 * @brief Recursively builds the candidate tree by applying the branching logic.
 *
 * @param parent_node The current node to expand.
 */
void build_candidate_tree_recursive(GNode* parent_node) {
    log_info("Expanding candidate tree node of type %d", ((struct candidate_node*)parent_node->data)->type);
    struct candidate_node* parent_data = (struct candidate_node*)parent_node->data;

    // According to RFC 9623, the branching order should be:
    // 1. Network Paths (Local Endpoints)
    // 2. Protocol Options
    // 3. Derived Endpoints (Remote Endpoints via DNS)

    // Step 1: Branch by Network Paths (Local Endpoints)
    if (parent_data->type == NODE_TYPE_ROOT) {
        log_trace("Expanding node of type ROOT to PATH nodes");
        LocalEndpoint* local_endpoint_list = NULL;
        size_t num_found_local = 0;

        // Resolve the local endpoint. The `local_endpoint_resolve` function
        // will find all available interfaces when the interface is not specified.
        local_endpoint_resolve(parent_data->local_endpoint, &local_endpoint_list, &num_found_local);
        log_trace("Found %zu local endpoints, adding as children to ROOT node", num_found_local);

        for (size_t i = 0; i < num_found_local; i++) {
            // Create a child node for each local endpoint found.
            struct candidate_node* path_node_data = candidate_node_new(
                NODE_TYPE_PATH,
                malloc(sizeof(LocalEndpoint)), // TODO - perhaps this would be better as a non-pointer
                parent_data->remote_endpoint,
                NULL, // Protocol not yet specified
                parent_data->transport_properties
            );
            memcpy((void*)path_node_data->local_endpoint, (void*)&local_endpoint_list[i], sizeof(LocalEndpoint));
            g_node_append_data(parent_node, path_node_data);

            // print interface name of last_child:

            // Recurse to the next level of the tree.
            build_candidate_tree_recursive(g_node_last_child(parent_node));
        }

        // Clean up the allocated memory for the list of local endpoints
        if (local_endpoint_list != NULL) {
            log_trace("Freeing list of local endpoints after building path nodes");
            // free(local_endpoint_list[0].interface_name);
            // free(local_endpoint_list[0].service);
            free(local_endpoint_list);
        }
    }

    // Step 2: Branch by Protocols, they are pruned later
    else if (parent_data->type == NODE_TYPE_PATH) {
        log_trace("Expanding node of type PATH to PROTOCOL nodes");
        int num_found_protocols = 0;

        // Get all protocols that fit the selection properties.
        ProtocolImplementation **candidate_stacks = get_supported_protocols();
        // TODO this is bug
        num_found_protocols = get_num_protocols(); // Assume one protocol for demonstration purposes.

        for (int i = 0; i < num_found_protocols; i++) {
            // Create a child node for each supported protocol.
            struct candidate_node* proto_node_data = candidate_node_new(
                NODE_TYPE_PROTOCOL,
                parent_data->local_endpoint,
                parent_data->remote_endpoint,
                candidate_stacks[i],
                parent_data->transport_properties
            );
            log_info("proto_node_daata local endpoint interface_name: %s", proto_node_data->local_endpoint->interface_name);

            g_node_append_data(parent_node, proto_node_data);

            // Recurse to the next level of the tree.
            build_candidate_tree_recursive(g_node_last_child(parent_node));
        }
    }

    // Step 3: Branch by Resolved Endpoints (DNS Lookup)
    else if (parent_data->type == NODE_TYPE_PROTOCOL) {
        log_trace("Expanding node of type PROTOCOL to ENDPOINT nodes");
        RemoteEndpoint* resolved_remote_endpoints = NULL;
        size_t num_found_remote = 0;

        // Resolve the remote endpoint (hostname to IP address).
        remote_endpoint_resolve(parent_data->remote_endpoint, &resolved_remote_endpoints, &num_found_remote);

        for (size_t i = 0; i < num_found_remote; i++) {
            // Create a leaf node for each resolved IP address.
            struct candidate_node* leaf_node_data = candidate_node_new(
                NODE_TYPE_ENDPOINT,
                parent_data->local_endpoint,
                malloc(sizeof(RemoteEndpoint)), // TODO - perhaps this would be better as a non-pointer
                parent_data->protocol,
                parent_data->transport_properties
            );
            memcpy((void*)leaf_node_data->remote_endpoint, (void*)&resolved_remote_endpoints[i], sizeof(RemoteEndpoint));
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
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

// Forward declaration of the function to build the tree recursively
void build_candidate_tree_recursive(GNode* parent_node);


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

    if (root_data == NULL) {
        log_error("Could not create root candidate node data");
        return NULL; // Handle memory allocation failure
    }

    // 2. Create and return a GNode with the root data
    log_trace("Creating root candidate node");
    GNode* root_node = g_node_new(root_data);

    log_trace("Building candidate tree recursively from root");
    build_candidate_tree_recursive(root_node);
    log_trace("Successfully built candidate tree, returning root node");
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
                &resolved_remote_endpoints[i],
                parent_data->protocol,
                parent_data->transport_properties
            );
            g_node_append_data(parent_node, leaf_node_data);
        }

        // Clean up the allocated memory for the list of remote endpoints.
        if (resolved_remote_endpoints != NULL) {
            log_trace("Freeing list of remote endpoints after building leaf nodes");
            free(resolved_remote_endpoints);
        }
    }
}
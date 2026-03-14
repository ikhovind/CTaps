#include "candidate_gathering.h"

#include "connection/preconnection.h"
#include "ctaps.h"
#include "ctaps_internal.h"
#include "endpoint/local_endpoint.h"
#include "endpoint/remote_endpoint.h"
#include <assert.h>
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
    const ct_protocol_impl_t* const* protocols;
    size_t num_protocols;
} ct_protocol_impl_array_t;

typedef struct ct_leaf_gathering_context_s {
    ct_gather_context_t* gather_context;
    GArray* leaf_nodes;
} ct_leaf_gathering_context_t;

/**
  * Represents all protocol options for candidate gathering.
  *
  * branch_by_protocol function will use this to expand PATH nodes into PROTOCOL nodes.
  * Creating a PROTOCOL node for each combination, where appropriate. Add future options
  * as members in this struct and modify branch_by_protocol accordingly.
  */
typedef struct ct_protocol_options_s {
    ct_protocol_impl_array_t protocol_arr; // List of supported protocol implementations
    ct_string_array_t alpns;               // e.g., ALPN strings
} ct_protocol_options_t;

ct_remote_resolve_call_context_t*
ct_remote_resolve_call_context_new(GNode* root_node, ct_gather_context_t* gather_context);
void ct_remote_resolve_call_context_free(ct_remote_resolve_call_context_t* context);

ct_protocol_options_t* ct_protocol_options_new(const ct_preconnection_t* precon);

ct_protocol_candidate_t* ct_protocol_candidate_new(const ct_protocol_impl_t* protocol_impl,
                                                   const char* alpn);

ct_protocol_candidate_t*
ct_protocol_candidate_copy(const ct_protocol_candidate_t* protocol_candidate);

ct_candidate_node_t* ct_candidate_node_copy(const ct_candidate_node_t* candidate_node);

void ct_protocol_options_free(ct_protocol_options_t* protocol_options);

ct_protocol_impl_array_t* ct_protocol_impl_array_new(void);

void build_candidate_tree_is_complete_cb(ct_gather_context_t* gather_context);

int ct_build_candidate_tree(ct_gather_context_t* gather_context);
int ct_branch_by_path(GNode* parent, const ct_local_endpoint_t* local_ep);
int branch_by_protocol_options(GNode* parent, ct_protocol_options_t* protocol_options);
int ct_branch_by_remote(GNode* parent, const ct_remote_endpoint_t* remote_ep,
                     ct_remote_resolve_call_context_t* resolve_call_context);

ct_protocol_options_t* ct_protocol_options_new(const ct_preconnection_t* precon) {
    ct_protocol_options_t* options = malloc(sizeof(ct_protocol_options_t));
    if (!options) {
        log_error("Could not allocate memory for ct_protocol_options_t");
        return NULL;
    }
    memset(options, 0, sizeof(ct_protocol_options_t));
    // TODO is this cast necessary or can we make the options contain const?
    if (precon->security_parameters) {
        options->alpns.strings = (char**)ct_security_parameters_get_alpns(
            precon->security_parameters, &options->alpns.num_strings);
    }
    options->protocol_arr.protocols = ct_supported_protocols;
    options->protocol_arr.num_protocols = ct_num_protocols;
    return options;
}

gboolean free_data_in_tree_node_and_children(GNode* node, gpointer user_data) {
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
    log_debug("Getting generic interface type for system interface name: %s",
              system_interface_name);
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
    const ct_protocol_impl_t* protocol, const ct_selection_properties_t* selection_properties) {
    for (int i = 0; i < SELECTION_PROPERTY_END; i++) {
        ct_selection_property_t desired_value = selection_properties->list[i];
        ct_selection_property_t protocol_value = protocol->selection_properties.list[i];

        if (desired_value.type == TYPE_PREFERENCE) {
            if (desired_value.value.simple_preference == REQUIRE &&
                protocol_value.value.simple_preference == PROHIBIT) {
                log_trace("Protocol %s does not support required property: %s", protocol->name,
                          desired_value.name);
                return false;
            }
            if (desired_value.value.simple_preference == PROHIBIT &&
                protocol_value.value.simple_preference == REQUIRE) {
                log_trace("Protocol %s requires prohibited property: %s", protocol->name,
                          desired_value.name);
                return false;
            }
        }
    }
    return true;
}

bool interface_is_compatible(const char* interface_name,
                             const ct_transport_properties_t* transport_properties) {
    log_trace("Checking if interface %s is compatible with transport properties", interface_name);
    // iterate over the interface preferences
    ct_preference_set_t preference_map =
        transport_properties->selection_properties.list[INTERFACE].value.preference_set_val;
    if (preference_map.num_combinations == 0) {
        // No preferences set, all interfaces are compatible
        return true;
    }
    if (strcmp(interface_name, "any") == 0) {
        return true;
    }
    // iterate each value in the hash table
    const char* interface_type = get_generic_interface_type(interface_name);
    if (!interface_type) {
        log_trace("Could not determine generic interface type for %s", interface_name);
        // Unknown interface type, consider it incompatible
        return false;
    }
    log_trace("Checking compatibility for generic interface type: %s", interface_type);
    log_debug("Interface preference map has %zu combinations", preference_map.num_combinations);
    for (size_t i = 0; i < preference_map.num_combinations; i++) {
        char* key = preference_map.combinations[i].value;
        ct_selection_preference_enum_t preference = preference_map.combinations[i].preference;
        log_trace("Preference for interface type %s is %d", key, preference);
        if (strcmp(key, interface_type) == 0) {
            if (preference == PROHIBIT) {
                log_trace("Interface %s is prohibited", interface_name);
                return false;
            }
        } else {
            // If any other interface is set to REQUIRE, this one is incompatible
            if (preference == REQUIRE) {
                log_trace("Interface %s is incompatible due to %s being required", interface_name,
                          key);
                return false;
            }
        }
    }
    log_trace("Interface %s is compatible", interface_name);
    return true;
}

gboolean gather_incompatible_path_nodes(GNode* node, gpointer user_data) {
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

    if (ct_local_endpoint_get_interface_name(node_data->local_endpoint) != NULL) {
        interface_name = ct_local_endpoint_get_interface_name(node_data->local_endpoint);
    }
    if (!interface_is_compatible(interface_name, node_data->transport_properties)) {
        log_trace("Found incompatible path node with interface %s", interface_name);
        pruning_data->undesirable_nodes = g_list_append(pruning_data->undesirable_nodes, node);
    } else {
        log_trace("Path node with interface %s is compatible", interface_name);
    }
    return false;
}

gboolean gather_incompatible_protocol_nodes(GNode* node, gpointer user_data) {
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

    log_trace("Checking protocol node with protocol %s",
              node_data->protocol_candidate->protocol_impl->name);
    ct_node_pruning_data_t* pruning_data = (ct_node_pruning_data_t*)user_data;
    if (!protocol_implementation_supports_selection_properties(
            node_data->protocol_candidate->protocol_impl,
            &node_data->transport_properties->selection_properties)) {
        log_trace("Found incompatible protocol node with protocol %s",
                  node_data->protocol_candidate->protocol_impl->name);
        pruning_data->undesirable_nodes = g_list_append(pruning_data->undesirable_nodes, node);
    } else {
        log_trace("Protocol node with protocol %s is compatible",
                  node_data->protocol_candidate->protocol_impl->name);
    }
    return false;
}

/*
 * @brief Remove and free each node and its children from the tree they belong to
 *
 * @param undesirable_nodes A list of GNode* that should be removed from the tree.
 *
 * @return the number of nodes removed from the tree (including children of removed nodes)
 */
size_t remove_nodes_from_tree(GList* undesirable_nodes) {
    if (!undesirable_nodes) {
        return 0;
    }
    size_t num_removed = 0;
    for (GList* current_node_list = undesirable_nodes; current_node_list != NULL;
         current_node_list = current_node_list->next) {
        GNode* node_to_remove = (GNode*)current_node_list->data;
        num_removed += g_node_n_nodes(node_to_remove, G_TRAVERSE_ALL);

        g_node_traverse(node_to_remove, G_IN_ORDER, G_TRAVERSE_ALL, -1,
                        free_data_in_tree_node_and_children, NULL);
        // First remove it from the tree, to avoid trying to free it twice, when later freeing a parent
        g_node_unlink(node_to_remove);
        g_node_destroy(node_to_remove);
    }
    return num_removed;
}

/**
 * @brief Prunes the candidate tree by removing nodes that are incompatible with the selection properties.
 *
 * @param root The root of the candidate tree to prune.
 * @param selection_properties The selection properties to use for pruning.
 *
 * @return the number of candidates remaining after pruning
 */
void prune_candidate_tree(GNode* root, ct_selection_properties_t selection_properties) {
    if (!root) {
        log_error("Cannot prune candidate tree: root is NULL");
        return;
    }
    log_debug("Pruning candidate tree based on selection properties");

    ct_node_pruning_data_t pruning_data = {
        .selection_properties = selection_properties,
        .undesirable_nodes = NULL // This is fince since g_list_append handles initialization
    };

    // Prune from the bottom up, otherwise we may remove a parent first, then try to remove the child
    log_trace("About to gather incompatible protocol nodes");
    g_node_traverse(root, G_LEVEL_ORDER, G_TRAVERSE_ALL, -1,
                    gather_incompatible_protocol_nodes, &pruning_data);

    log_trace("About to gather incompatible path nodes");
    g_node_traverse(root, G_LEVEL_ORDER, G_TRAVERSE_NON_LEAVES, -1, gather_incompatible_path_nodes,
                    &pruning_data);

    log_trace("Total nodes in tree before pruning: %d", g_node_n_nodes(root, G_TRAVERSE_ALL));
    remove_nodes_from_tree(pruning_data.undesirable_nodes);
    g_list_free(pruning_data.undesirable_nodes);
}

gint compare_prefer_and_avoid_preferences(gconstpointer a, gconstpointer b,
                                          gpointer desired_selection_properties) {
    const ct_candidate_node_t* candidate_a = (const ct_candidate_node_t*)a;
    const ct_candidate_node_t* candidate_b = (const ct_candidate_node_t*)b;
    const ct_selection_properties_t* selection_properties =
        (const ct_selection_properties_t*)desired_selection_properties;

    log_trace("In candidate sorting - comparing two candidate nodes with protocol %s and %s based on prefer and avoid "
              "selection properties", candidate_a->protocol_candidate->protocol_impl->name, candidate_b->protocol_candidate->protocol_impl->name);


    // order the branches according to the preferred Properties and use any avoided Properties as a tiebreaker
    int a_prefer_score = 0;
    int a_avoid_score = 0;
    for (int i = 0; i < SELECTION_PROPERTY_END; i++) {
        if (selection_properties->list[i].type == TYPE_PREFERENCE) {
            if (selection_properties->list[i].value.simple_preference == PREFER) {
                log_trace("Found PREFER property: %s", selection_properties->list[i].name);
                // If A can provide this property then bump its score
                if (candidate_a->protocol_candidate->protocol_impl->selection_properties.list[i]
                        .value.simple_preference != PROHIBIT) {
                    log_trace("%s can fulfill PREFER preference for property: %s", candidate_a->protocol_candidate->protocol_impl->name, selection_properties->list[i].name);
                    a_prefer_score++;
                }
                // But if B can provide it too, then do not bump it after all
                // If A cannot provide but B can, then A's score should decrease
                if (candidate_b->protocol_candidate->protocol_impl->selection_properties.list[i]
                        .value.simple_preference != PROHIBIT) {
                    log_trace("%s can fulfill PREFER preference for property: %s", candidate_b->protocol_candidate->protocol_impl->name, selection_properties->list[i].name);
                    a_prefer_score--;
                }
            } else if (selection_properties->list[i].value.simple_preference == AVOID) {
                log_trace("Found AVOID property: %s", selection_properties->list[i].name);

                if (candidate_a->protocol_candidate->protocol_impl->selection_properties.list[i]
                        .value.simple_preference != REQUIRE) {
                    log_trace("%s can fulfill AVOID preference for property: %s", candidate_a->protocol_candidate->protocol_impl->name, selection_properties->list[i].name);
                    a_avoid_score++;
                }
                if (candidate_b->protocol_candidate->protocol_impl->selection_properties.list[i]
                        .value.simple_preference != REQUIRE) {
                    log_trace("%s can fulfill AVOID preference for property: %s", candidate_b->protocol_candidate->protocol_impl->name, selection_properties->list[i].name);
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
struct ct_candidate_node_t* candidate_node_new(ct_node_type_enum_t type,
                                               const ct_local_endpoint_t* local_ep,
                                               const ct_remote_endpoint_t* remote_ep,
                                               const ct_protocol_candidate_t* proto,
                                               const ct_transport_properties_t* props) {
    log_trace("Creating new candidate node of type %d", type);
    ct_candidate_node_t* node = malloc(sizeof(struct ct_candidate_node_t));
    if (!node) {
        log_error("Could not allocate memory for ct_candidate_node_t");
        return NULL;
    }
    memset(node, 0, sizeof(struct ct_candidate_node_t));
    node->type = type;
    node->score = 0;
    if (local_ep) {
        node->local_endpoint = ct_local_endpoint_deep_copy(local_ep);
        if (!node->local_endpoint) {
            log_error("Could not copy local endpoint for ct_candidate_node_t");
            free(node);
            return NULL;
        }
    }

    if (remote_ep) {
        node->remote_endpoint = ct_remote_endpoint_deep_copy(remote_ep);
        if (!node->remote_endpoint) {
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

gboolean get_deep_copy_leaf_nodes(GNode* node, gpointer user_data) {
    log_trace("Iterating candidate tree for getting candidate leaf nodes");
    ct_leaf_gathering_context_t* context = (ct_leaf_gathering_context_t*)user_data;
    GArray* node_array = context->leaf_nodes;

    /*
    If the tree looks like this, we only want the leaf nodes from the *bottom*
    layer, but the type of that layer depends on whether we are in local-only mode or not.
    So filter based on that here
              h
             / \
            d   l
           / \   \
          b   f   n   <- Undesirable leaf nodes (wrong type)
         / \   \
        a   c   g <- Desirable leaf nodes 
    */

    if (context->gather_context->local_only) {
        if (((ct_candidate_node_t*)node->data)->type != NODE_TYPE_PROTOCOL) {
            log_trace("Skipping non-PROTOCOL node in local-only gather: node type %d",
                      ((ct_candidate_node_t*)node->data)->type);
            return false;
        }
    }
    else {
        if (((ct_candidate_node_t*)node->data)->type != NODE_TYPE_ENDPOINT) {
            log_trace("Skipping non-ENDPOINT node in normal gather: node type %d",
                      ((ct_candidate_node_t*)node->data)->type);
            return false;
        }
    }
    ct_candidate_node_t* candidate_node = ct_candidate_node_copy(node->data);
    log_trace("Found leaf candidate node of type %d, adding to output array",
              ((ct_candidate_node_t*)node->data)->type);
    g_array_append_val(node_array, *candidate_node);
    free(candidate_node);

    return false;
}

static gboolean collect_leaves(GNode* node, gpointer user_data) {
    GList** leaves = (GList**)user_data;
    if (G_NODE_IS_LEAF(node)) {
        *leaves = g_list_append(*leaves, node);
    }
    return false;
}

int ct_build_candidate_tree(ct_gather_context_t* gather_context) {
    const ct_preconnection_t* precon = gather_context->preconnection;
    struct ct_candidate_node_t* root = candidate_node_new(
        NODE_TYPE_ROOT, NULL, NULL, NULL, ct_preconnection_get_transport_properties(precon));

    if (!root) {
        log_error("Could not create root candidate node data");
        return -ENOMEM;
    }

    GNode* root_node = g_node_new(root);

    if (!root_node) {
        log_error("Could not create root GNode for candidate tree");
        free(root);
        return -ENOMEM;
    }

    gather_context->root_node = root_node;

    size_t num_local_eps = 0;
    const ct_local_endpoint_t* local_eps =
        ct_preconnection_get_local_endpoints(precon, &num_local_eps);
    ct_local_endpoint_t* ephemeral_local_ep = NULL;
    if (num_local_eps == 0) {
        log_debug("No local endpoints specified in preconnection, using ephemeral local endpoint "
                  "for candidate tree building");
        ephemeral_local_ep = ct_local_endpoint_new();
        num_local_eps = 1;
        local_eps = ephemeral_local_ep;
    } else {
        log_debug("Found %zu local endpoints in preconnection for candidate tree building",
                  num_local_eps);
    }

    log_debug("Branching candidate tree by path for %zu local endpoints", num_local_eps);
    for (size_t i = 0; i < num_local_eps; i++) {
        int rc = ct_branch_by_path(root_node, &local_eps[i]);
        if (rc != 0) {
            log_error("Error branching by path for local endpoint at index %zu: %d", i, rc);
            if (ephemeral_local_ep) {
                ct_local_endpoint_free(ephemeral_local_ep);
            }
            g_node_traverse(root_node, G_IN_ORDER, G_TRAVERSE_ALL, -1,
                            free_data_in_tree_node_and_children, NULL);
            g_node_destroy(root_node);
            return rc;
        }
    }
    if (ephemeral_local_ep) {
        ct_local_endpoint_free(ephemeral_local_ep);
    }

    GList* leaves = NULL;
    g_node_traverse(root_node, G_IN_ORDER, G_TRAVERSE_LEAVES, -1, collect_leaves, &leaves);

    ct_protocol_options_t* protocol_options = ct_protocol_options_new(precon);
    if (!protocol_options) {
        log_error("Could not create protocol options for branching by protocol options");
        g_node_traverse(root_node, G_IN_ORDER, G_TRAVERSE_ALL, -1,
                        free_data_in_tree_node_and_children, NULL);
        g_node_destroy(root_node);
        return -ENOMEM;
    }
    log_debug("Branching by protocol options when building candidate tree");
    for (GList* iter = leaves; iter != NULL; iter = iter->next) {
        GNode* leaf_node = (GNode*)iter->data;
        branch_by_protocol_options(leaf_node, protocol_options);
    }
    ct_protocol_options_free(protocol_options);

    g_list_free(leaves);
    leaves = NULL;

    g_node_traverse(root_node, G_IN_ORDER, G_TRAVERSE_LEAVES, -1, collect_leaves, &leaves);
    // For listeners we do not need remote endpoints: stop here and treat
    // PROTOCOL nodes as the final candidates.
    if (gather_context->local_only) {
        log_debug("local_only mode: skipping remote endpoint branching, PROTOCOL nodes are leaves");
        g_list_free(leaves);
        build_candidate_tree_is_complete_cb(gather_context);
        return 0;
    }

    size_t num_remote_endpoints = 0;
    const ct_remote_endpoint_t* remote_endpoints = ct_preconnection_get_remote_endpoints(precon, &num_remote_endpoints);

    // If we have gotten here then we are *not* happy with a local-only, so we need
    // to check if we actually have any remote endpoints to branch by, if not we are
    // in a failure state.
    if (leaves == NULL || num_remote_endpoints == 0) {
        log_debug("No candidate leaf nodes found after branching by protocol options, finishing "
                  "candidate tree building");
        gather_context->failed = true;
        build_candidate_tree_is_complete_cb(gather_context);
        if (leaves) {
            g_list_free(leaves);
        }
        return 0;
    }

    log_debug("Branching by remote endpoints for %zu leaf nodes and %zu remote endpoints per node",
              g_list_length(leaves), num_remote_endpoints);
    gather_context->pending_resolutions = g_list_length(leaves) * num_remote_endpoints;
    for (GList* iter = leaves; iter != NULL; iter = iter->next) {
        GNode* leaf_node = (GNode*)iter->data;
        for (size_t remote_ix = 0; remote_ix < num_remote_endpoints; remote_ix++) {
            ct_remote_resolve_call_context_t* context =
                ct_remote_resolve_call_context_new(leaf_node, gather_context);
            if (!context) {
                log_error("Could not create context for remote endpoint resolution");
                gather_context->failed = true;
                gather_context->pending_resolutions = gather_context->num_in_flight;
                if (gather_context->pending_resolutions == 0) {
                    build_candidate_tree_is_complete_cb(gather_context);
                }

                g_list_free(leaves);
                return -ENOMEM;
            }
            gather_context->num_in_flight++;
            int rc = ct_branch_by_remote(leaf_node, &remote_endpoints[remote_ix], context);
            if (rc != 0) {
                gather_context->num_in_flight--;
                log_error("Error branching by remote endpoint: %d", rc);
                gather_context->failed = true;
                gather_context->pending_resolutions = gather_context->num_in_flight;
                if (gather_context->pending_resolutions == 0) {
                    build_candidate_tree_is_complete_cb(gather_context);
                }
                ct_remote_resolve_call_context_free(context);
                g_list_free(leaves);

                return rc;
            }
        }
    }
    g_list_free(leaves);

    return 0;
}

int ct_branch_by_path(GNode* parent, const ct_local_endpoint_t* local_ep) {
    struct ct_candidate_node_t* parent_data = (struct ct_candidate_node_t*)parent->data;
    assert(parent_data->type == NODE_TYPE_ROOT);

    // Resolve the local endpoint. The `ct_local_endpoint_resolve` function
    // will find all available interfaces when the interface is not specified.
    size_t num_found_local = 0;
    ct_local_endpoint_t* local_endpoint_list =
        ct_local_endpoint_resolve(local_ep, &num_found_local);
    log_trace("Found %zu local endpoints, adding as children to ROOT node", num_found_local);

    for (size_t i = 0; i < num_found_local; i++) {
        // Create a child node for each local endpoint found.
        struct ct_candidate_node_t* path_node_data =
            candidate_node_new(NODE_TYPE_PATH, &local_endpoint_list[i], NULL,
                               NULL, // Protocol not yet specified
                               parent_data->transport_properties);
        if (!path_node_data) {
            log_error("Could not create PATH node data");
            ct_local_endpoints_free(local_endpoint_list, num_found_local);
            return -ENOMEM;
        }
        g_node_append_data(parent, path_node_data);
    }
    ct_local_endpoints_free(local_endpoint_list, num_found_local);
    return 0;
}

int branch_by_protocol_options(GNode* parent, ct_protocol_options_t* protocol_options) {
    struct ct_candidate_node_t* parent_data = (struct ct_candidate_node_t*)parent->data;
    assert(parent_data->type == NODE_TYPE_PATH);

    log_trace("Expanding node of type PATH to PROTOCOL nodes");

    for (size_t i = 0; i < protocol_options->protocol_arr.num_protocols; i++) {
        if (ct_protocol_supports_alpn(protocol_options->protocol_arr.protocols[i]) &&
            protocol_options->alpns.num_strings > 0) {
            // If the current protocol supports ALPN, create a candidate for each ALPN value
            // Picoquic does support passing multiple ALPNs as a single connection attempt,
            // but not if we want to support 0-rtt because then the alpn has to be passed to
            // the picoquic_create invocation, which only takes a single value.
            // It was therefore decided to create separate
            // candidates for each ALPN value, as the added overhead is assumed to not be too high
            for (size_t j = 0; j < protocol_options->alpns.num_strings; j++) {
                log_trace("Checking QUIC protocol against selection properties");
                ct_protocol_candidate_t* candidate =
                    ct_protocol_candidate_new(protocol_options->protocol_arr.protocols[i],
                                              protocol_options->alpns.strings[j]);
                if (!candidate) {
                    log_error("Could not create protocol candidate for protocol %s with ALPN %s",
                              protocol_options->protocol_arr.protocols[i]->name,
                              protocol_options->alpns.strings[j]);
                    return -1;
                }

                // Create a child node for each supported protocol.
                ct_candidate_node_t* candidate_node = candidate_node_new(NODE_TYPE_PROTOCOL,
                                                                         parent_data->local_endpoint,
                                                                         NULL,
                                                                         candidate,
                                                                         parent_data->transport_properties);
                ct_protocol_candidate_free(candidate); // candidate_node_new copies it
                if (!candidate_node) {
                    log_error("Could not create PROTOCOL node data");
                    return -1;
                }
                g_node_append_data(parent, candidate_node);
            }
        } else {
            log_trace("Protocol %s does not support ALPN or no ALPN values provided, creating single candidate without ALPN",
                      protocol_options->protocol_arr.protocols[i]->name);
            // If the current protocol does not support ALPN, just create a single candidate without any alpn
            ct_protocol_candidate_t* candidate =
                ct_protocol_candidate_new(protocol_options->protocol_arr.protocols[i], NULL);
            if (!candidate) {
                log_error("Could not create protocol candidate for protocol %s without ALPN",
                          protocol_options->protocol_arr.protocols[i]->name);
                return -1;
            }

            ct_candidate_node_t* candidate_node =
                candidate_node_new(NODE_TYPE_PROTOCOL, parent_data->local_endpoint, NULL, candidate,
                                   parent_data->transport_properties);
            ct_protocol_candidate_free(candidate); // candidate_node_new copies it

            if (!candidate_node) {
                log_error("Could not create PROTOCOL node data");
                return -1;
            }
            g_node_append_data(parent, candidate_node);
        }
    }
    return 0;
}

void ct_remote_endpoint_resolve_cb(ct_remote_endpoint_t* remote_endpoint, size_t out_count,
                                   ct_remote_resolve_call_context_t* context) {
    log_debug("Received resolved remote endpoint with %zu addresses", out_count);
    GNode* parent = context->parent_node;
    ct_candidate_node_t* parent_data = (ct_candidate_node_t*)parent->data;

    for (size_t i = 0; i < out_count; i++) {
        // Create a leaf node for each resolved IP address.
        ct_candidate_node_t* leaf_node_data =
            candidate_node_new(NODE_TYPE_ENDPOINT, parent_data->local_endpoint, &remote_endpoint[i],
                               parent_data->protocol_candidate, parent_data->transport_properties);
        g_node_append_data(parent, leaf_node_data);
    }

    // Clean up the allocated memory for the list of remote endpoints.
    if (remote_endpoint) {
        log_trace("Freeing list of remote endpoints after building leaf nodes");
        free(remote_endpoint);
    }

    context->gather_context->num_in_flight--;
    context->gather_context->pending_resolutions--;
    if (context->gather_context->pending_resolutions == 0) {
        log_trace("All remote endpoint resolutions complete");
        build_candidate_tree_is_complete_cb(context->gather_context);
    }
    free(context);
}

int ct_branch_by_remote(GNode* parent, const ct_remote_endpoint_t* remote_ep,
                     ct_remote_resolve_call_context_t* context) {
    struct ct_candidate_node_t* parent_data = (struct ct_candidate_node_t*)parent->data;
    assert(parent_data->type == NODE_TYPE_PROTOCOL);
    log_trace("Expanding node of type PROTOCOL to ENDPOINT nodes");

    // Resolve the remote endpoint (hostname to IP address).
    int rc = ct_remote_endpoint_resolve(remote_ep, context);
    if (rc != 0) {
        log_error("Error resolving remote endpoint");
        return rc;
    }

    return 0;
}

void build_candidate_tree_is_complete_cb(ct_gather_context_t* gather_context) {
    if (gather_context->failed) {
        log_error("Candidate tree building failed, not proceeding to pruning and callback");
        gather_context->gathering_callbacks.candidate_node_array_ready_cb(
            NULL, gather_context->gathering_callbacks.context);
        free(gather_context);
        return;
    }
    GNode* root_node = gather_context->root_node;
    const ct_preconnection_t* precon = gather_context->preconnection;
    prune_candidate_tree(root_node,
                         ct_preconnection_get_transport_properties(precon)->selection_properties);

    log_info("Candidate tree has been pruned, extracting leaf nodes");

    GArray* root_array = g_array_new(false, false, sizeof(ct_candidate_node_t));

    log_trace("Fetching leaf nodes from candidate tree");

    ct_leaf_gathering_context_t leaf_context = {
        .gather_context = gather_context,
        .leaf_nodes = root_array,
    };
    // Get deep copy of leaf candidate nodes and insert them in array
    g_node_traverse(root_node, G_IN_ORDER, G_TRAVERSE_LEAVES, -1, get_deep_copy_leaf_nodes,
                    &leaf_context);

    log_trace("Freeing undesirable nodes from candidate tree");

    // Free data owned by tree (all non-leaf nodes)
    g_node_traverse(root_node, G_IN_ORDER, G_TRAVERSE_ALL, -1, free_data_in_tree_node_and_children,
                    NULL);

    g_node_destroy(root_node);

    log_trace("Sorting candidates based in desirability");
    g_array_sort_with_data(
        root_array, compare_prefer_and_avoid_preferences,
        (gpointer)&ct_preconnection_get_transport_properties(precon)->selection_properties);

    if (root_array->len > 0) {
        log_trace("Most desirable candidate protocol is: %s",
                  (g_array_index(root_array, ct_candidate_node_t, 0))
                      .protocol_candidate->protocol_impl->name);
    } else {
        log_warn("No candidate nodes found after pruning");
    }
    gather_context->gathering_callbacks.candidate_node_array_ready_cb(
        root_array, gather_context->gathering_callbacks.context);
    free(gather_context);
}

int ct_get_ordered_candidate_nodes(const ct_preconnection_t* precon,
                                ct_candidate_gathering_callbacks_t callbacks) {
    log_info("Creating candidate node array from preconnection: %p", (void*)precon);

    ct_gather_context_t* gather_context = malloc(sizeof(ct_gather_context_t));
    if (!gather_context) {
        log_error("Could not allocate memory for gather context");
        return -ENOMEM;
    }
    memset(gather_context, 0, sizeof(ct_gather_context_t));
    gather_context->gathering_callbacks = callbacks;
    gather_context->pending_resolutions = 0;
    gather_context->num_in_flight = 0;
    gather_context->root_node = NULL;
    gather_context->preconnection = precon;
    gather_context->local_only = false;

    // This is async and calls the ready function on completion or failure
    int rc = ct_build_candidate_tree(gather_context);
    if (rc != 0) {
        free(gather_context);
        log_error("Could not build candidate tree");
        return rc;
    }
    return 0;
}

int ct_get_ordered_local_candidate_nodes(const ct_preconnection_t* precon,
                                         ct_candidate_gathering_callbacks_t callbacks) {
    log_info("Creating local-only candidate node array from preconnection: %p (listener path)",
             (void*)precon);

    ct_gather_context_t* gather_context = malloc(sizeof(ct_gather_context_t));
    if (!gather_context) {
        log_error("Could not allocate memory for gather context");
        return -ENOMEM;
    }
    memset(gather_context, 0, sizeof(ct_gather_context_t));
    gather_context->gathering_callbacks = callbacks;
    gather_context->pending_resolutions = 0;
    gather_context->num_in_flight = 0;
    gather_context->root_node = NULL;
    gather_context->preconnection = precon;
    gather_context->local_only = true;

    // Synchronous in the local-only case (no DNS resolution), but we go through
    // the same tree-building path for consistency.
    int rc = ct_build_candidate_tree(gather_context);
    if (rc != 0) {
        free(gather_context);
        log_error("Could not build local candidate tree");
        return rc;
    }
    return 0;
}

void free_candidate_array(GArray* candidate_array) {
    for (guint i = 0; i < candidate_array->len; i++) {
        const ct_candidate_node_t candidate_node =
            g_array_index(candidate_array, ct_candidate_node_t, i);
        ct_local_endpoint_free(candidate_node.local_endpoint);
        if (candidate_node.remote_endpoint) {
            ct_remote_endpoint_free(candidate_node.remote_endpoint);
        }
        ct_protocol_candidate_free(candidate_node.protocol_candidate);
    }
    g_array_free(candidate_array, true);
}

ct_protocol_candidate_t* ct_protocol_candidate_new(const ct_protocol_impl_t* protocol_impl,
                                                   const char* alpn) {
    ct_protocol_candidate_t* protocol_candidate = malloc(sizeof(ct_protocol_candidate_t));
    if (!protocol_candidate) {
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
        // alpns points to security_parameters data, don't free it
        free(protocol_options);
    }
}
ct_protocol_candidate_t*
ct_protocol_candidate_copy(const ct_protocol_candidate_t* protocol_candidate) {
    if (!protocol_candidate) {
        return NULL;
    }
    return ct_protocol_candidate_new(protocol_candidate->protocol_impl, protocol_candidate->alpn);
}

ct_candidate_node_t* ct_candidate_node_copy(const ct_candidate_node_t* candidate_node) {
    if (!candidate_node) {
        return NULL;
    }
    return candidate_node_new(candidate_node->type, candidate_node->local_endpoint,
                              candidate_node->remote_endpoint, candidate_node->protocol_candidate,
                              candidate_node->transport_properties);
}

void ct_remote_resolve_call_context_free(ct_remote_resolve_call_context_t* context) {
    free(context);
}

ct_remote_resolve_call_context_t*
ct_remote_resolve_call_context_new(GNode* root_node, ct_gather_context_t* gather_context) {
    ct_remote_resolve_call_context_t* context = malloc(sizeof(ct_remote_resolve_call_context_t));
    if (!context) {
        log_error("Could not allocate memory for ct_remote_resolve_call_context_t");
        return NULL;
    }
    memset(context, 0, sizeof(ct_remote_resolve_call_context_t));
    context->parent_node = root_node;
    context->gather_context = gather_context;
    return context;
}

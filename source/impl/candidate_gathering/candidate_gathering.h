#ifndef CANDIDATE_GATHERING_H
#define CANDIDATE_GATHERING_H

#include <connections/preconnection/preconnection.h>
#include <glib/gnode.h>

#include "endpoints/local/local_endpoint.h"
#include "endpoints/remote/remote_endpoint.h"
#include "protocols/protocol_interface.h"
#include "transport_properties/transport_properties.h"

// Enum to specify what type of node we are.
typedef enum {
  NODE_TYPE_ROOT = 0,
  NODE_TYPE_PATH,
  NODE_TYPE_PROTOCOL,
  NODE_TYPE_ENDPOINT,
} NodeType;

struct candidate_node {
  NodeType type;
  int score;

  const LocalEndpoint* local_endpoint;
  const RemoteEndpoint* remote_endpoint;
  const ProtocolImplementation* protocol;

  const TransportProperties* transport_properties;
};

GNode* create_root_candidate_node(const Preconnection* precon);

#endif //CANDIDATE_GATHERING_H
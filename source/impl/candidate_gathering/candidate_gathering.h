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

typedef struct CandidateNode {
  NodeType type;
  int score;

  LocalEndpoint* local_endpoint;
  RemoteEndpoint* remote_endpoint;
  const ProtocolImplementation* protocol;

  const TransportProperties* transport_properties;
} CandidateNode;

GArray* get_ordered_candidate_nodes(const Preconnection* precon);

void free_candidate_array(GArray* candidate_array);

#endif //CANDIDATE_GATHERING_H
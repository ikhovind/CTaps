#ifndef PROTOCOL_INTERFACE_H
#define PROTOCOL_INTERFACE_H
#include "transport_properties/selection_properties/selection_properties.h"

#include "message/message.h"

struct Connection;

typedef struct {
    SelectionPreference values[SELECTION_PROPERTY_END];
} ProtocolFeatures;

typedef struct ProtocolImplementation{
    const char* name;
    ProtocolFeatures features;
    int (*init)();
    int (*send)(struct Connection*, Message*);
    int (*receive)(struct Connection*, Message*);
    void (*close)(struct Connection*);
} ProtocolImplementation;

#endif //PROTOCOL_INTERFACE_H

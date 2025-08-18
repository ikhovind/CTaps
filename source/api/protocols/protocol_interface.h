#ifndef PROTOCOL_INTERFACE_H
#define PROTOCOL_INTERFACE_H
#include "connections/connection/connection.h"

typedef struct {
    SelectionPreference values[SELECTION_PROPERTY_END];
} ProtocolFeatures;

typedef struct {
    const char* name;
    ProtocolFeatures features;
    int (*init)();
    int (*send)(Connection*, Message*);
    int (*receive)(Connection*, char*);
    void (*close)(Connection*);
} ProtocolImplementation;

#endif //PROTOCOL_INTERFACE_H

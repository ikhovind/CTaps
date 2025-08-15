//
// Created by ikhovind on 12.08.25.
//

#ifndef PRECONNECTION_H
#define PRECONNECTION_H

#include "transport_properties/transport_properties.h"

typedef struct {
    TransportProperties transport_properties;
} Preconnection;

void preconnection_init(Preconnection *preconnection, TransportProperties transport_properties);

#endif //PRECONNECTION_H

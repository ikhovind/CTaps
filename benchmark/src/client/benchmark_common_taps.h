#include "ctaps.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>


extern int json_only_mode;

void on_connection_ready(ct_connection_t* connection);

void on_establishment_error(ct_connection_t* connection);

#include "uuid_util.h"
#include "stdint.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/random.h>
#include <time.h>

// This is only used for logging/demultiplexing incoming
// messages so not security-sensitive, therefore just
// roll our own instead of pulling in a full UUID library.
//
// Glib has uuid_string_random but that allocates memory.
void generate_uuid_string(char* uuid_str) {
    assert(uuid_str);
    uint8_t uuid[16];
    if (getrandom(uuid, 16, 0) != 16) {
        // Fallback: time + counter
        static uint64_t counter = 0;
        uint64_t t = (uint64_t)time(NULL);
        uint64_t c = counter++;
        memcpy(uuid,     &t, 8);
        memcpy(uuid + 8, &c, 8);
    }

    uuid[6] = (uuid[6] & 0x0FU) | 0x40U; // version 4
    uuid[8] = (uuid[8] & 0x3FU) | 0x80U; // variant 1
    snprintf(uuid_str, 37,
        "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
        uuid[0], uuid[1], uuid[2],  uuid[3],
        uuid[4], uuid[5], uuid[6],  uuid[7],
        uuid[8], uuid[9], uuid[10], uuid[11],
        uuid[12],uuid[13],uuid[14], uuid[15]);
}

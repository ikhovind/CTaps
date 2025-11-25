#ifndef UUID_UTIL_H
#define UUID_UTIL_H

/**
 * @brief Generate a UUID and store it as a string.
 *
 * Generates a new UUID and converts it to string format (36 characters + null terminator).
 *
 * @param[out] uuid_str Character array to store the UUID string (must be at least 37 bytes)
 */
void generate_uuid_string(char* uuid_str);

#endif // UUID_UTIL_H

#include "file_generator.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

int generate_test_file(const char *filename, size_t size) {
    FILE *fp = fopen(filename, "wb");
    if (!fp) {
        perror("Failed to create file");
        return -1;
    }

    unsigned char buffer[4096];
    for (size_t i = 0; i < sizeof(buffer); i++) {
        buffer[i] = (unsigned char)(rand() % 256);
    }

    size_t remaining = size;
    while (remaining > 0) {
        size_t to_write = (remaining < sizeof(buffer)) ? remaining : sizeof(buffer);
        size_t written = fwrite(buffer, 1, to_write, fp);
        if (written != to_write) {
            perror("Failed to write to file");
            fclose(fp);
            return -1;
        }
        remaining -= written;
    }

    fclose(fp);
    printf("Generated %s (%zu bytes)\n", filename, size);
    return 0;
}

int verify_file_size(const char *filename, size_t expected_size) {
    struct stat st;
    if (stat(filename, &st) != 0) {
        perror("Failed to stat file");
        return -1;
    }

    if ((size_t)st.st_size != expected_size) {
        fprintf(stderr, "File size mismatch: expected %zu, got %ld\n",
                expected_size, st.st_size);
        return -1;
    }

    return 0;
}

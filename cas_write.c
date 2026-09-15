/* cas_write.c : write a file to CAS format and print the hash */
/* made by a machine. PUBLIC DOMAIN */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "smolvfs/cas.h"

int
main(int argc, char *argv[])
{
    if (argc < 3) {
        fprintf(stderr, "usage: %s <infile> <store>\n", argv[0]);
        return 1;
    }

    const char *infile = argv[1];
    const char *store = argv[2];

    FILE *in = fopen(infile, "rb");
    if (!in) {
        perror("open input");
        return 1;
    }

    fseek(in, 0, SEEK_END);
    size_t datalen = ftell(in);
    rewind(in);

    unsigned char *data = malloc(datalen);
    if (!data) {
        perror("malloc");
        fclose(in);
        return 1;
    }

    if (fread(data, 1, datalen, in) != datalen) {
        perror("read");
        free(data);
        fclose(in);
        return 1;
    }
    fclose(in);

    struct cas *cas = cas_new(store);
    if (!cas) {
        fprintf(stderr, "cas_new failed\n");
        free(data);
        return 1;
    }

    char hash_out[CAS_HASH_HEX + 1];
    int rc = cas_put(cas, data, datalen, hash_out);
    cas_free(cas);
    free(data);

    if (rc != CAS_OK) {
        fprintf(stderr, "cas_put failed: %s\n", cas_strerror(rc));
        return 1;
    }

    printf("%s\n", hash_out);
    return 0;
}

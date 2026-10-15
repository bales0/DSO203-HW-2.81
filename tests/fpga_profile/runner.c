#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "amx.h"
int main(int argc, char **argv) {
    if (argc != 2) return 10;
    FILE *file = fopen(argv[1], "rb");
    if (!file) return 11;
    AMX_HEADER header;
    if (fread(&header, sizeof header, 1, file) != 1) return 12;
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    rewind(file);
    if (size < (long)sizeof header || header.stp <= 0) return 13;
    size_t capacity = header.stp > size ? (size_t)header.stp : (size_t)size;
    unsigned char *data = calloc(1, capacity);
    if (!data) return 14;
    if (fread(data, 1, (size_t)size, file) != (size_t)size) return 15;
    fclose(file);
    AMX vm;
    memset(&vm, 0, sizeof vm);
    int status = amx_Init(&vm, data);
    if (!status) status = amx_Register(&vm, NULL, 0);
    cell result = -1, *checks = NULL;
    if (!status) status = amx_Exec(&vm, &result, AMX_EXEC_MAIN);
    if (!status) status = amx_FindPubVar(&vm, "checks", &checks);
    printf("FPGA profile tests: VM=%d failures=%ld checks=%ld\n",
           status, (long)result, checks ? (long)*checks : -1L);
    int passed = !status && result == 0 && checks && *checks == 92;
    amx_Cleanup(&vm);
    free(data);
    return passed ? 0 : 1;
}

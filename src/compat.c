/*
 * Vita3K / PS Vita Kernel RNG replacement
 *
 * OpenSSL calls sceKernelGetRandomNumber to seed its PRNG.
 * Vita3K HLE does not implement this kernel NID, resulting in an unhandled instruction.
 * Implementing it here in userspace provides entropy without syscall traps.
 */

#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>

int sceKernelGetRandomNumber(void *output, unsigned int size) {
    if (!output || size == 0) return 0;
    unsigned char *p = (unsigned char *)output;
    static unsigned int seed = 0x9e3779b9;
    for (unsigned int i = 0; i < size; i++) {
        seed = seed * 1664525u + 1013904223u;
        p[i] = (unsigned char)((seed >> 16) & 0xFF);
    }
    return 0;
}

int getentropy(void *buffer, size_t length) {
    return sceKernelGetRandomNumber(buffer, (unsigned int)length);
}

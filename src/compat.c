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

/*
 * libvita2d's built-in utf8_to_ucs2() only decodes 1-, 2-, and 3-byte UTF-8
 * sequences, splitting 4-byte UTF-8 emojis (U+10000..U+10FFFF) into four raw
 * bytes that render as four .notdef tofu boxes. Wrapping utf8_to_ucs2 passes the
 * full 32-bit Unicode codepoint to FreeType's Format 12 UCS-4 cmap.
 */
int __wrap_utf8_to_ucs2(const char *utf8, unsigned int *character) {
    if (!utf8 || !character) return 1;
    const unsigned char *u = (const unsigned char *)utf8;

    if ((u[0] & 0xF8) == 0xF0 &&
        (u[1] & 0xC0) == 0x80 &&
        (u[2] & 0xC0) == 0x80 &&
        (u[3] & 0xC0) == 0x80) {
        *character = ((unsigned int)(u[0] & 0x07) << 18) |
                     ((unsigned int)(u[1] & 0x3F) << 12) |
                     ((unsigned int)(u[2] & 0x3F) << 6)  |
                      (unsigned int)(u[3] & 0x3F);
        return 4;
    }

    if ((u[0] & 0xF0) == 0xE0 &&
        (u[1] & 0xC0) == 0x80 &&
        (u[2] & 0xC0) == 0x80) {
        *character = ((unsigned int)(u[0] & 0x0F) << 12) |
                     ((unsigned int)(u[1] & 0x3F) << 6)  |
                      (unsigned int)(u[2] & 0x3F);
        return 3;
    }

    if ((u[0] & 0xE0) == 0xC0 &&
        (u[1] & 0xC0) == 0x80) {
        *character = ((unsigned int)(u[0] & 0x1F) << 6) |
                      (unsigned int)(u[1] & 0x3F);
        return 2;
    }

    *character = (unsigned int)u[0];
    return 1;
}

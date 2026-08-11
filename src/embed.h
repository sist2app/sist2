#ifndef SIST2_EMBED_H
#define SIST2_EMBED_H

#include <stddef.h>

// Embeds a file as a null-terminated const char array using the assembler's
// .incbin directive. <name>_size is the file size, excluding the null
// terminator. The compiler does not track .incbin'd files in its depfile
// output; CMake declares them via OBJECT_DEPENDS instead.
#define EMBED_FILE(name, path) \
    __asm__( \
            ".pushsection .rodata\n" \
            ".balign 8\n" \
            ".global " #name "\n" \
            #name ":\n" \
            ".incbin \"" path "\"\n" \
            #name "_end:\n" \
            ".byte 0\n" \
            ".balign 8\n" \
            ".global " #name "_size\n" \
            #name "_size:\n" \
            ".quad " #name "_end - " #name "\n" \
            ".popsection\n" \
    ); \
    extern const char name[]; \
    extern const size_t name##_size

#endif

#ifndef SIST2_EMBED_H
#define SIST2_EMBED_H

#include <stddef.h>

// COFF holds read-only data in .rdata, and its assembler has no section stack, so the section
// is switched back to .text by hand instead.
#ifdef _WIN32
#define EMBED_SECTION_BEGIN ".section .rdata,\"dr\"\n"
#define EMBED_SECTION_END ".text\n"
#else
#define EMBED_SECTION_BEGIN ".pushsection .rodata\n"
#define EMBED_SECTION_END ".popsection\n"
#endif

// Embeds a file as a null-terminated const char array using the assembler's
// .incbin directive. <name>_size is the file size, excluding the null
// terminator. The compiler does not track .incbin'd files in its depfile
// output; CMake declares them via OBJECT_DEPENDS instead.
#define EMBED_FILE(name, path) \
    __asm__( \
            EMBED_SECTION_BEGIN \
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
            EMBED_SECTION_END \
    ); \
    extern const char name[]; \
    extern const size_t name##_size

#endif

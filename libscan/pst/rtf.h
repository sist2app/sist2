#ifndef SCAN_RTF_H
#define SCAN_RTF_H

#include <stddef.h>

/**
 * The text of an RTF document, which is what a message written in RTF — or an HTML one Outlook
 * stored as RTF with the markup in \* destinations — carries instead of a plain body. Control
 * words are dropped, and so is every group that holds no readable text. Returns a NUL-terminated
 * string of at most max_size bytes, which the caller frees.
 */
char *rtf_to_text(const char *rtf, size_t size, size_t max_size, size_t *out_size);

#endif

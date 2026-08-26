// Coverage-guided target for the RTF reader that message bodies out of an Outlook mailbox go
// through. See tests/README.md for how to build and run it.

#include "libscan/pst/rtf.h"

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define MAX_SIZE (64 * 1024)

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    size_t text_size = 0;
    char *text = rtf_to_text((const char *) data, size, MAX_SIZE, &text_size);

    // The invariants: a NUL-terminated string, no longer than it was asked for, whose length is
    // the size it reports
    assert(text != NULL);
    assert(text[text_size] == '\0');
    assert(strlen(text) == text_size);
    assert(text_size <= MAX_SIZE);

    free(text);

    return 0;
}

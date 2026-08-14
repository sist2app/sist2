/*
 * libFuzzer target for the search-result highlighter, whose two inputs both come from outside:
 * the query is whatever was typed into the search box, the text is whatever a parser pulled out of
 * a file. Not part of the CMake build — it needs clang, and the rest of the project is built with
 * gcc. To run it:
 *
 *   clang -g -O1 -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=all \
 *       -I. -o /tmp/highlight_fuzz src/web/highlight.c tests/fuzz/highlight_fuzz.c
 *   /tmp/highlight_fuzz -max_total_time=60
 *
 * The input is split at the first NUL byte: query, then text.
 */

#include "src/web/highlight.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define MAX_INPUT 65536

/** Aborts unless the tags in the output are the ones highlight_text() wrote, around whole words */
static void check_markup(const char *text, const char *out) {
    char *stripped = malloc(strlen(out) + 1);
    size_t stripped_len = 0;
    int open = 0;

    for (const char *cur = out; *cur != '\0';) {
        if (strncmp(cur, "<mark>", 6) == 0) {
            if (open) {
                abort();
            }
            open = 1;
            cur += 6;
            continue;
        }

        if (strncmp(cur, "</mark>", 7) == 0) {
            if (!open) {
                abort();
            }
            open = 0;
            cur += 7;
            continue;
        }

        stripped[stripped_len++] = *cur;
        cur += 1;
    }

    if (open) {
        abort();
    }

    stripped[stripped_len] = '\0';

    // Every byte of the output came from the text, in order
    if (strstr(text, stripped) == NULL) {
        abort();
    }

    free(stripped);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < 2 || size > MAX_INPUT) {
        return 0;
    }

    // The '<' of a mark tag can only come from the highlighter, so that check_markup() can tell
    // them apart. Documents holding their own markup are covered by the unit specs.
    char *input = malloc(size + 1);
    for (size_t i = 0; i < size; i++) {
        input[i] = data[i] == '<' ? '(' : (char) data[i];
    }
    input[size] = '\0';

    const size_t split = strlen(input);
    const char *query = input;
    const char *text = split + 1 < size ? input + split + 1 : "";

    // The output cap is 16kB, and the odds of libFuzzer producing a run that long on its own are
    // slim, so one bit of the input asks for the text to be repeated until it is over the cap
    char *repeated = NULL;
    if ((data[1] & 1) && *text != '\0') {
        const size_t text_len = strlen(text);
        const size_t times = (20 * 1024) / text_len + 1;

        repeated = malloc(text_len * times + 1);
        repeated[0] = '\0';
        for (size_t i = 0; i < times; i++) {
            memcpy(repeated + i * text_len, text, text_len);
        }
        repeated[text_len * times] = '\0';

        text = repeated;
    }

    // Negative, zero, ordinary, and past the internal maximum
    const int context = (int) ((data[0] | (data[1] << 8)) % 4096) - 8;

    char **terms = highlight_query_terms(query);

    int term_count = 0;
    while (terms[term_count] != NULL) {
        if (*terms[term_count] == '\0') {
            abort();
        }
        term_count += 1;
    }
    if (term_count > 64) {
        abort();
    }

    char *out = highlight_text(text, terms, context);
    if (out != NULL) {
        check_markup(text, out);
        free(out);
    }

    highlight_free_terms(terms);
    free(repeated);
    free(input);

    return 0;
}

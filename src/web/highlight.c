#include "highlight.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#define MAX_TERMS 64
#define DEFAULT_CONTEXT_WORDS 30
#define MAX_CONTEXT_WORDS 1000

// Only words count towards the window, so a document that is one long run of punctuation would
// otherwise be copied whole, once per hit on the page
#define MAX_HIGHLIGHT_BYTES 16384

int fts_is_word_byte(unsigned char c) {
    return isalnum(c) || c >= 0x80 || c == '_';
}

int fts_is_fts5_operator(const char *word, size_t len) {
    return (len == 3 && memcmp(word, "AND", 3) == 0)
           || (len == 2 && memcmp(word, "OR", 2) == 0)
           || (len == 3 && memcmp(word, "NOT", 3) == 0)
           || (len == 4 && memcmp(word, "NEAR", 4) == 0);
}

char **highlight_query_terms(const char *query) {
    char **terms = calloc(MAX_TERMS + 1, sizeof(char *));
    int count = 0;

    if (query == NULL) {
        return terms;
    }

    const char *cur = query;

    while (*cur != '\0' && count < MAX_TERMS) {
        if (!fts_is_word_byte((unsigned char) *cur)) {
            cur += 1;
            continue;
        }

        const char *word = cur;
        while (fts_is_word_byte((unsigned char) *cur)) {
            cur += 1;
        }

        size_t len = cur - word;

        // `name:term` filters the column, and the column name is not part of the query
        if (*cur == ':') {
            cur += 1;
            continue;
        }

        if (fts_is_fts5_operator(word, len)) {
            continue;
        }

        const int prefix = *cur == '*';

        char *term = malloc(len + prefix + 1);
        for (size_t i = 0; i < len; i++) {
            term[i] = (char) tolower((unsigned char) word[i]);
        }
        if (prefix) {
            term[len] = '*';
        }
        term[len + prefix] = '\0';

        terms[count++] = term;
    }

    return terms;
}

void highlight_free_terms(char **terms) {
    if (terms == NULL) {
        return;
    }

    for (int i = 0; terms[i] != NULL; i++) {
        free(terms[i]);
    }

    free(terms);
}

static int word_matches(const char *word, size_t len, char *const *terms) {
    for (int i = 0; terms[i] != NULL; i++) {
        size_t term_len = strlen(terms[i]);
        const int prefix = term_len > 0 && terms[i][term_len - 1] == '*';

        if (prefix) {
            term_len -= 1;
        }

        if (term_len == 0) {
            continue;
        }

        if (prefix ? len >= term_len : len == term_len) {
            if (strncasecmp(word, terms[i], term_len) == 0) {
                return 1;
            }
        }
    }

    return 0;
}

typedef struct {
    char *buf;
    size_t len;
    size_t capacity;
} out_buf_t;

/** Appends, and returns 0 once the output has reached its cap */
static int out_append(out_buf_t *out, const char *data, size_t len) {
    if (out->len + len > MAX_HIGHLIGHT_BYTES) {
        return 0;
    }

    while (out->len + len + 1 > out->capacity) {
        out->capacity *= 2;
        out->buf = realloc(out->buf, out->capacity);
    }

    memcpy(out->buf + out->len, data, len);
    out->len += len;
    out->buf[out->len] = '\0';

    return 1;
}

char *highlight_text(const char *text, char *const *terms, int context_words) {
    if (text == NULL || *text == '\0' || terms == NULL) {
        return NULL;
    }

    if (context_words <= 0) {
        context_words = DEFAULT_CONTEXT_WORDS;
    } else if (context_words > MAX_CONTEXT_WORDS) {
        context_words = MAX_CONTEXT_WORDS;
    }

    // Where the window starts: a third of it before the first match, so the match is not the very
    // first word of the snippet
    const int lead = context_words / 3;
    const char *ring[MAX_CONTEXT_WORDS / 3 + 1];
    int ring_len = 0;
    int ring_next = 0;

    const char *start = text;
    const char *cur = text;

    while (*cur != '\0') {
        if (!fts_is_word_byte((unsigned char) *cur)) {
            cur += 1;
            continue;
        }

        const char *word = cur;
        while (fts_is_word_byte((unsigned char) *cur)) {
            cur += 1;
        }

        if (word_matches(word, cur - word, terms)) {
            start = ring_len == 0 ? word : ring[ring_len < lead + 1 ? 0 : ring_next];
            break;
        }

        if (lead > 0) {
            ring[ring_next] = word;
            ring_next = (ring_next + 1) % (lead + 1);
            if (ring_len < lead + 1) {
                ring_len += 1;
            }
        }
    }

    out_buf_t out = {.buf = malloc(512), .len = 0, .capacity = 512};
    out.buf[0] = '\0';

    cur = start;
    int words = 0;

    while (*cur != '\0' && words < context_words) {
        if (!fts_is_word_byte((unsigned char) *cur)) {
            if (!out_append(&out, cur, 1)) {
                break;
            }
            cur += 1;
            continue;
        }

        const char *word = cur;
        while (fts_is_word_byte((unsigned char) *cur)) {
            cur += 1;
        }

        // The word and the tags around it go in together, so the output never ends mid-markup
        const size_t word_len = cur - word;
        const int matched = word_matches(word, word_len, terms);

        if (out.len + word_len + (matched ? 13 : 0) > MAX_HIGHLIGHT_BYTES) {
            break;
        }

        if (matched) {
            out_append(&out, "<mark>", 6);
            out_append(&out, word, word_len);
            out_append(&out, "</mark>", 7);
        } else {
            out_append(&out, word, word_len);
        }

        words += 1;
    }

    if (out.len == 0) {
        free(out.buf);
        return NULL;
    }

    return out.buf;
}

#ifndef SIST2_HIGHLIGHT_H
#define SIST2_HIGHLIGHT_H

#include <stddef.h>

/**
 * The terms of an fts5 query, lowercased, in the order they appear. Operators, column filters and
 * the quotes around phrases are dropped; a term written with a trailing '*' keeps it, and matches
 * as a prefix.
 *
 * Returns a NULL-terminated array; free with highlight_free_terms().
 */
char **highlight_query_terms(const char *query);

void highlight_free_terms(char **terms);

/**
 * A window of text with every occurrence of a term wrapped in <mark></mark>, at most
 * context_words words long, starting a few words before the first match. Returns NULL when there
 * is nothing to show. Caller frees.
 */
char *highlight_text(const char *text, char *const *terms, int context_words);

#endif

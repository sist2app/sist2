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

/**
 * Word characters, as close to fts5's unicode61 tokenizer as byte comparisons get: every non-ASCII
 * byte belongs to a word, so UTF-8 sequences stay whole. Case folding is ASCII-only, so a query
 * for "CAFÉ" does not highlight "café" the way the tokenizer that matched it would have.
 */
int fts_is_word_byte(unsigned char c);

int fts_is_fts5_operator(const char *word, size_t len);

void highlight_free_terms(char **terms);

/**
 * A window of text with every occurrence of a term wrapped in <mark></mark>, at most
 * context_words words long, starting a few words before the first match. Returns NULL when there
 * is nothing to show. Caller frees.
 */
char *highlight_text(const char *text, char *const *terms, int context_words);

/**
 * The page offsets a scan wrote for a paginated document ("0,31,1036"), as code point offsets into
 * its text. Returns NULL when the document has none; free the array when done.
 */
size_t *highlight_parse_page_breaks(const char *csv, int *count);

/**
 * The 1-based page a marked-up fragment of the text was taken from, or 0 when the fragment is not
 * part of it. The <mark> tags a highlighter added are ignored.
 */
int highlight_fragment_page(const char *text, const char *fragment, const size_t *breaks, int break_count);

#endif

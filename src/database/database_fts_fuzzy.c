#include "database.h"

#include <ctype.h>

#include "src/ctx.h"

#include "libscan/util.h"
#include "src/web/highlight.h"

// Shorter words are all one or two edits away from each other, so correcting them turns a search
// into a synonym for everything
#define VOCAB_MIN_LENGTH 4
// Past this, a word is a hash, a base64 blob or a chemical name; none of them is ever misspelled
#define VOCAB_MAX_LENGTH 24

#define FUZZY_MIN_TERM_LENGTH 4
/** Alternatives added per term, on top of the word the user typed */
#define FUZZY_MAX_SUGGESTIONS 3
/** Terms of one query that are expanded; the rest are matched as they were typed */
#define FUZZY_MAX_TERMS 8
// spellfix1 charges 100 per edit, so this is roughly "up to two edits"
#define FUZZY_MAX_DISTANCE 200

#define STR_(x) #x
#define STR(x) STR_(x)

/**
 * Words worth keeping in the vocabulary. A number is never misspelled, it is looked up: correcting
 * a year or a part number to the ones an edit away from it only ever costs results, and so do the
 * digits inside a long word, which mean an identifier or a hash rather than a word.
 */
#define VOCAB_FILTER \
    " length(v.term) BETWEEN " STR(VOCAB_MIN_LENGTH) " AND " STR(VOCAB_MAX_LENGTH) \
    " AND v.term GLOB '*[^0-9]*'" \
    " AND NOT (length(v.term) >= 8 AND v.term GLOB '*[0-9]*')"

static void fuzzy_exec(database_t *db, const char *sql) {
    CRASH_IF_NOT_SQLITE_OK(sqlite3_exec(db->db, sql, NULL, NULL, NULL));
}

/**
 * The vocabulary tables. They are not part of the schema a search index is created with: their
 * presence is what says a fuzzy search has something to correct against, and --skip-spellfix
 * leaves the index without them. Created against the attached database by name, because the
 * connection that builds them has the source index as its main database.
 */
static void create_vocab_tables(database_t *db) {
    fuzzy_exec(db,
               "CREATE VIRTUAL TABLE IF NOT EXISTS fts.search_vocab USING fts5vocab(search, row);"
               "CREATE VIRTUAL TABLE IF NOT EXISTS fts.vocab USING spellfix1;"
               "CREATE TABLE IF NOT EXISTS fts.vocab_term ("
               "   term TEXT PRIMARY KEY,"
               "   id INTEGER NOT NULL UNIQUE"
               ");");
}

int database_fts_has_vocab(database_t *db) {
    // pragma_table_list, not sqlite_master: the connection that builds a search index has it
    // attached rather than open as its main database
    return database_fts_scalar(db, "SELECT count(*) FROM pragma_table_list WHERE name = 'vocab_term'", 0) > 0;
}

/**
 * Leaves the search index without a vocabulary. Dropping a table takes its shadow tables with it:
 * deleting the rows one by one means a statement compiled per word, which is what spellfix1 does
 * for every write.
 */
void database_fts_drop_vocab(database_t *db) {
    fuzzy_exec(db, "DROP TABLE IF EXISTS fts.vocab;"
                   "DROP TABLE IF EXISTS fts.vocab_term;");
}

void database_fts_build_vocab(database_t *db, int rebuild, int prune) {

    if (rebuild) {
        database_fts_drop_vocab(db);
    }

    create_vocab_tables(db);

    LOG_DEBUG("database_fts_fuzzy.c", "Updating fuzzy search vocabulary");

    // Terms are inserted with the rowid they get written down under, so the words that leave the
    // corpus later can be deleted by rowid: spellfix1 indexes its rows by phonetic key, and
    // finding one by word means reading all of them.
    fuzzy_exec(db,
               "CREATE TEMP TABLE vocab_new AS"
               " SELECT v.term AS term, v.doc AS cnt,"
               "  (SELECT COALESCE(max(id), 0) FROM vocab_term) + row_number() OVER (ORDER BY v.term) AS id"
               " FROM search_vocab v"
               " WHERE" VOCAB_FILTER
               "  AND NOT EXISTS (SELECT 1 FROM vocab_term t WHERE t.term = v.term);"
               ""
               "INSERT INTO vocab (rowid, word, rank) SELECT id, term, cnt FROM vocab_new;"
               "INSERT INTO vocab_term (term, id) SELECT term, id FROM vocab_new;");

    const long long added = database_fts_scalar(db, "SELECT count(*) FROM vocab_new", 0);

    fuzzy_exec(db, "DROP TABLE vocab_new;");

    long long removed = 0;

    // Reading the term list is most of what this costs, and a word can only have left the corpus
    // when a document that was already indexed was rewritten or deleted
    if (prune) {
        fuzzy_exec(db,
                   "CREATE TEMP TABLE vocab_gone AS"
                   " SELECT id FROM vocab_term t"
                   " WHERE NOT EXISTS (SELECT 1 FROM search_vocab v WHERE v.term = t.term);"
                   ""
                   "DELETE FROM vocab WHERE rowid IN (SELECT id FROM vocab_gone);"
                   "DELETE FROM vocab_term WHERE id IN (SELECT id FROM vocab_gone);");

        removed = database_fts_scalar(db, "SELECT count(*) FROM vocab_gone", 0);

        fuzzy_exec(db, "DROP TABLE vocab_gone;");
    }

    LOG_INFOF("database_fts_fuzzy.c", "Fuzzy search vocabulary: %lld words (+%lld, -%lld)",
              database_fts_scalar(db, "SELECT count(*) FROM vocab_term", 0), added, removed);
}

static int is_near_operator(const char *word, size_t len) {
    return len == 4 && memcmp(word, "NEAR", 4) == 0;
}

/** The first byte at or after cur that is not whitespace */
static char next_significant(const char *cur) {
    while (isspace((unsigned char) *cur)) {
        cur += 1;
    }

    return *cur;
}

/** Every alternative spelling of word that is in the vocabulary, most likely first */
static char **suggestions(sqlite3_stmt *stmt, const char *word, size_t len, int *count) {
    char **words = calloc(FUZZY_MAX_SUGGESTIONS + 1, sizeof(char *));
    *count = 0;

    sqlite3_bind_text(stmt, 1, word, (int) len, SQLITE_STATIC);

    while (sqlite3_step(stmt) == SQLITE_ROW && *count < FUZZY_MAX_SUGGESTIONS) {
        const char *suggestion = (const char *) sqlite3_column_text(stmt, 0);

        // The word the user typed is matched as it was typed, whether or not it is in the corpus
        if (suggestion == NULL || (strlen(suggestion) == len && memcmp(suggestion, word, len) == 0)) {
            continue;
        }

        words[(*count)++] = strdup(suggestion);
    }

    sqlite3_reset(stmt);

    return words;
}

static void free_suggestions(char **words) {
    for (int i = 0; words[i] != NULL; i++) {
        free(words[i]);
    }
    free(words);
}


/** `term` becomes `("term" OR "trem" OR "tern")` — quoted, so no alternative can be an operator */
static void write_expanded_term(dyn_buffer_t *buf, const char *word, size_t len, char **words) {
    dyn_buffer_append_string(buf, "(\"");
    dyn_buffer_write(buf, word, len);
    dyn_buffer_write_char(buf, '"');

    for (int i = 0; words[i] != NULL; i++) {
        dyn_buffer_append_string(buf, " OR \"");
        dyn_buffer_append_string(buf, words[i]);
        dyn_buffer_write_char(buf, '"');
    }

    dyn_buffer_write_char(buf, ')');
}

/**
 * fts5 joins adjacent phrases with an implicit AND, but two adjacent *expressions* have no
 * production: a parenthesised group next to a phrase, a column filter, a NEAR() or another group
 * is a syntax error. The operator the query left out is written back in wherever an expansion
 * lands beside something the tokenizer would otherwise have concatenated.
 */
static void write_implicit_and(dyn_buffer_t *buf) {
    if (buf->cur > 0 && !isspace((unsigned char) buf->buf[buf->cur - 1])) {
        dyn_buffer_write_char(buf, ' ');
    }

    dyn_buffer_append_string(buf, "AND ");
}

/**
 * The query, with every plain word replaced by itself and the words of the vocabulary that are a
 * spelling mistake away from it. Only the positions where fts5 accepts a parenthesised group are
 * touched: a phrase, a column name, a `{...}` column list, a `^` initial-token term, a NEAR
 * argument, a prefix term, an argument of `+` and an operator all mean something an alternative
 * would break, and fts5 answers a query that puts a group in any of those places with a syntax
 * error.
 *
 * The result is what fts5 matches on *and* what the excerpts are marked from, so a hit on a
 * corrected spelling is highlighted the way an exact one is.
 */
char *database_fts_fuzzy_expand(database_t *db, const char *query) {

    if (query == NULL || !database_fts_has_vocab(db)) {
        return NULL;
    }

    sqlite3_stmt *stmt;
    CRASH_IF_NOT_SQLITE_OK(sqlite3_prepare_v2(
            db->db,
            "SELECT word FROM vocab"
            " WHERE word MATCH ?1 AND top = " STR(FUZZY_MAX_SUGGESTIONS) " + 1"
            "  AND scope = 4 AND distance <= " STR(FUZZY_MAX_DISTANCE),
            -1, &stmt, NULL));

    dyn_buffer_t buf = dyn_buffer_create();

    int expanded = 0;
    int in_phrase = FALSE;
    // `{name path}`: the words of a column list are column names
    int in_column_list = FALSE;
    // The parenthesis depth, and the depth of the NEAR argument list that is open, if any
    int depth = 0;
    int near_depth = 0;
    // NEAR was the last word read, so the parenthesis that follows opens its arguments
    int after_near = FALSE;
    // `^term` matches the first token of a column, and takes a term rather than a group
    int after_caret = FALSE;
    // `one + two` concatenates two phrases, and takes a phrase on either side
    int after_plus = FALSE;
    // The last thing written completes an expression, and whether it is a group this added
    int prev_value = FALSE;
    int prev_group = FALSE;
    const char *cur = query;

    while (*cur != '\0') {
        if (*cur == '"') {
            if (!in_phrase && prev_value && prev_group) {
                write_implicit_and(&buf);
            }
            in_phrase = !in_phrase;
            // The phrase is a value once its closing quote is written
            prev_value = !in_phrase;
            prev_group = FALSE;
            after_caret = FALSE;
            after_near = FALSE;
            after_plus = FALSE;
            dyn_buffer_write_char(&buf, *cur++);
            continue;
        }
        if (in_phrase) {
            dyn_buffer_write_char(&buf, *cur++);
            continue;
        }
        if (!fts_is_word_byte((unsigned char) *cur)) {
            switch (*cur) {
                case '{':
                    if (prev_value && prev_group) {
                        write_implicit_and(&buf);
                    }
                    in_column_list = TRUE;
                    prev_value = FALSE;
                    prev_group = FALSE;
                    break;
                case '}':
                    in_column_list = FALSE;
                    prev_value = FALSE;
                    prev_group = FALSE;
                    break;
                case '(':
                    // The parenthesis of a NEAR() belongs to the word before it
                    if (!after_near && prev_value && prev_group) {
                        write_implicit_and(&buf);
                    }
                    depth += 1;
                    if (after_near && near_depth == 0) {
                        near_depth = depth;
                    }
                    prev_value = FALSE;
                    prev_group = FALSE;
                    break;
                case ')':
                    if (near_depth == depth) {
                        near_depth = 0;
                    }
                    if (depth > 0) {
                        depth -= 1;
                    }
                    prev_value = TRUE;
                    prev_group = FALSE;
                    break;
                case '^':
                    if (prev_value && prev_group) {
                        write_implicit_and(&buf);
                    }
                    prev_value = FALSE;
                    prev_group = FALSE;
                    break;
                case ':':
                case '+':
                    prev_value = FALSE;
                    prev_group = FALSE;
                    break;
                default:
                    break;
            }

            if (!isspace((unsigned char) *cur)) {
                after_caret = *cur == '^';
                after_plus = *cur == '+';
                if (*cur != '(') {
                    after_near = FALSE;
                }
            }

            dyn_buffer_write_char(&buf, *cur++);
            continue;
        }

        const char *word = cur;
        while (fts_is_word_byte((unsigned char) *cur)) {
            cur += 1;
        }
        const size_t len = cur - word;

        const int caret = after_caret;
        const int plus = after_plus;
        const int near_word = is_near_operator(word, len);
        after_caret = FALSE;
        after_plus = FALSE;
        after_near = near_word;

        if (fts_is_fts5_operator(word, len)) {
            // NEAR() is an expression of its own; AND, OR and NOT join the ones around them
            if (near_word && prev_value && prev_group) {
                write_implicit_and(&buf);
            }
            prev_value = FALSE;
            prev_group = FALSE;
            dyn_buffer_write(&buf, word, len);
            continue;
        }

        // `name : term` and `{name path} : term` filter columns, `term*` is a prefix, `^term`, the
        // arguments of NEAR() and either side of `+` are terms rather than expressions
        const int plain = next_significant(cur) != ':' && next_significant(cur) != '+'
                          && *cur != '*' && !caret && !plus
                          && !in_column_list && near_depth == 0;

        int count = 0;
        char **words = NULL;

        if (plain && utf8nlen(word, len) >= FUZZY_MIN_TERM_LENGTH && expanded < FUZZY_MAX_TERMS) {
            words = suggestions(stmt, word, len, &count);
        }

        if (prev_value && (prev_group || count > 0) && !in_column_list && near_depth == 0) {
            write_implicit_and(&buf);
        }

        if (count == 0) {
            dyn_buffer_write(&buf, word, len);
            prev_group = FALSE;
        } else {
            write_expanded_term(&buf, word, len, words);
            expanded += 1;
            prev_group = TRUE;
        }

        prev_value = TRUE;

        if (words != NULL) {
            free_suggestions(words);
        }
    }

    sqlite3_finalize(stmt);

    dyn_buffer_write_char(&buf, '\0');

    return buf.buf;
}

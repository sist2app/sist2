#include "rtf.h"

#include <ctype.h>

#include "../scan.h"

/* Parameters are 32bit at most, and one written past that is not a number anyone meant */
#define RTF_MAX_PARAMETER 0xffffffL

/* \ucN says how many characters follow a \uN escape as a substitute; a real one is a handful */
#define RTF_MAX_UC 32

#define RTF_APPEND(codepoint)                                              \
    do {                                                                   \
        if (text_buffer_append_char(&tex, (codepoint)) == TEXT_BUF_FULL) { \
            goto done;                                                     \
        }                                                                  \
    } while (0)

/** Neither past the last codepoint nor one of the surrogates, which are not characters */
static int is_codepoint(long value) {
    return value > 0 && value <= 0x10ffff && (value < 0xd800 || value > 0xdfff);
}

static int is_skipped_rtf_destination(const char *word) {
    static const char *destinations[] = {"fonttbl", "colortbl", "stylesheet", "listtable",
                                         "listoverridetable", "info", "pict", "object", "themedata",
                                         "datastore", "latentstyles", "generator", "xmlnstbl",
                                         "rsidtbl", "mmathPr", "wgrffmtfilter", NULL};

    for (int i = 0; destinations[i] != NULL; i++) {
        if (strcmp(word, destinations[i]) == 0) {
            return TRUE;
        }
    }

    return FALSE;
}

/*
 * The text of an RTF body, which is what a message written in RTF — or an HTML one Outlook stored
 * as RTF with the markup in \* destinations — carries instead of a plain body. Control words are
 * dropped, and so is every group that holds no readable text.
 */
char *rtf_to_text(const char *rtf, size_t size, size_t max_size, size_t *out_size) {
    text_buffer_t tex = text_buffer_create((long) max_size);

    int depth = 0;
    int skip_depth = -1;
    // Characters a \uN escape is followed by as a substitute for readers that cannot read it
    int skip_fallback = 0;
    int uc = 1;
    int group_start = FALSE;

    for (size_t i = 0; i < size && rtf[i] != '\0';) {
        const unsigned char c = (unsigned char) rtf[i];

        if (c == '{') {
            depth += 1;
            group_start = TRUE;
            i += 1;
            continue;
        }

        if (c == '}') {
            if (skip_depth >= 0 && depth <= skip_depth) {
                skip_depth = -1;
            }
            depth -= 1;
            group_start = FALSE;
            i += 1;
            continue;
        }

        if (c == '\\') {
            i += 1;
            if (i >= size) {
                break;
            }

            if (rtf[i] == '*') {
                if (skip_depth < 0) {
                    skip_depth = depth;
                }
                i += 1;
                group_start = FALSE;
                continue;
            }

            if (!isalpha((unsigned char) rtf[i])) {
                if (rtf[i] == '\'' && i + 2 < size) {
                    char hex[3] = {rtf[i + 1], rtf[i + 2], '\0'};
                    const int value = (int) strtol(hex, NULL, 16);
                    if (skip_depth < 0 && skip_fallback == 0) {
                        // The codepage is not known here; the bytes are read as latin-1
                        RTF_APPEND(value);
                    }
                    if (skip_fallback > 0) {
                        skip_fallback -= 1;
                    }
                    i += 3;
                } else {
                    if (skip_depth < 0) {
                        RTF_APPEND((unsigned char) rtf[i]);
                    }
                    i += 1;
                }
                group_start = FALSE;
                continue;
            }

            char word[33];
            size_t word_len = 0;
            while (i < size && isalpha((unsigned char) rtf[i])) {
                if (word_len < sizeof(word) - 1) {
                    word[word_len++] = rtf[i];
                }
                i += 1;
            }
            word[word_len] = '\0';

            int has_parameter = FALSE;
            int negative = FALSE;
            long parameter = 0;
            if (i < size && rtf[i] == '-') {
                negative = TRUE;
                i += 1;
            }
            while (i < size && isdigit((unsigned char) rtf[i])) {
                has_parameter = TRUE;
                // Parameters are 32bit at most; anything longer is not a number anyone wrote
                if (parameter <= RTF_MAX_PARAMETER) {
                    parameter = parameter * 10 + (rtf[i] - '0');
                }
                i += 1;
            }
            if (i < size && rtf[i] == ' ') {
                i += 1;
            }

            if (group_start && is_skipped_rtf_destination(word) && skip_depth < 0) {
                skip_depth = depth;
            } else if (strcmp(word, "par") == 0 || strcmp(word, "line") == 0 ||
                       strcmp(word, "row") == 0) {
                if (skip_depth < 0) {
                    RTF_APPEND('\n');
                }
            } else if (strcmp(word, "tab") == 0 || strcmp(word, "cell") == 0) {
                if (skip_depth < 0) {
                    RTF_APPEND('\t');
                }
            } else if (strcmp(word, "u") == 0 && has_parameter) {
                // A negative parameter is the codepoint as a signed 16bit integer
                const long codepoint = negative ? 65536 - parameter : parameter;

                if (skip_depth < 0 && is_codepoint(codepoint)) {
                    RTF_APPEND((int) codepoint);
                }
                skip_fallback = uc;
            } else if (strcmp(word, "uc") == 0 && has_parameter) {
                uc = negative ? 0 : (int) (parameter > RTF_MAX_UC ? RTF_MAX_UC : parameter);
            }

            group_start = FALSE;
            continue;
        }

        if (skip_depth < 0 && c != '\r' && c != '\n') {
            if (skip_fallback > 0) {
                skip_fallback -= 1;
            } else {
                RTF_APPEND(c);
            }
        }

        group_start = FALSE;
        i += 1;
    }

    done:
    text_buffer_terminate_string(&tex);

    // The buffer stops on the codepoint that crosses max_size rather than before it
    size_t text_size = tex.dyn_buffer.cur - 1;
    if (text_size > max_size) {
        text_size = max_size;
        while (text_size > 0 && ((unsigned char) tex.dyn_buffer.buf[text_size] & 0xc0) == 0x80) {
            text_size -= 1;
        }
        tex.dyn_buffer.buf[text_size] = '\0';
    }

    *out_size = text_size;
    return tex.dyn_buffer.buf;
}

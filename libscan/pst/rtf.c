#include "rtf.h"

#include <ctype.h>

#include "../scan.h"

/* Parameters are 32bit at most, and one written past that is not a number anyone meant */
#define RTF_MAX_PARAMETER 0xffffffL

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
    int group_start = FALSE;

    for (size_t i = 0; i < size && rtf[i] != '\0';) {
        const char c = rtf[i];

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
                        text_buffer_append_char(&tex, value);
                    }
                    if (skip_fallback > 0) {
                        skip_fallback -= 1;
                    }
                    i += 3;
                } else {
                    if (skip_depth < 0) {
                        text_buffer_append_char(&tex, rtf[i]);
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
                    text_buffer_append_char(&tex, '\n');
                }
            } else if (strcmp(word, "tab") == 0 || strcmp(word, "cell") == 0) {
                if (skip_depth < 0) {
                    text_buffer_append_char(&tex, '\t');
                }
            } else if (strcmp(word, "u") == 0 && has_parameter) {
                // A negative parameter is the codepoint as a signed 16bit integer
                const long codepoint = negative ? 65536 - parameter : parameter;

                if (skip_depth < 0 && is_codepoint(codepoint)) {
                    text_buffer_append_char(&tex, (int) codepoint);
                }
                skip_fallback = 1;
            } else if (strcmp(word, "uc") == 0 && has_parameter) {
                skip_fallback = 0;
            }

            group_start = FALSE;
            continue;
        }

        if (skip_depth < 0 && c != '\r' && c != '\n') {
            if (skip_fallback > 0) {
                skip_fallback -= 1;
            } else if (text_buffer_append_char(&tex, c) == TEXT_BUF_FULL) {
                break;
            }
        }

        group_start = FALSE;
        i += 1;
    }

    text_buffer_terminate_string(&tex);

    *out_size = tex.dyn_buffer.cur - 1;
    return tex.dyn_buffer.buf;
}

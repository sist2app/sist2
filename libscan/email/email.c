#include "email.h"

#include <envelope.h>

#include "../util.h"

#define EMAIL_MAX_MEM ((size_t) 256 * 1024 * 1024)
#define EMAIL_MAX_DECODED ((size_t) 64 * 1024 * 1024)

/* Mail inside mail inside mail: past this the message was built to make the scan never end */
#define MAX_EMAIL_DEPTH 16

/** Body of one part, read by the sub-document parser through a vfile */
typedef struct {
    const char *data;
    size_t size;
    size_t cursor;
} part_data_t;

static int email_read(vfile_t *f, void *buf, size_t size) {
    part_data_t *part = f->mem;

    const size_t to_read = MIN(size, part->size - part->cursor);
    memcpy(buf, part->data + part->cursor, to_read);
    part->cursor += to_read;

    if (to_read > 0 && f->calculate_checksum) {
        f->has_checksum = TRUE;
        safe_digest_update(f->sha1_ctx, buf, to_read);
    }

    return (int) to_read;
}

/* Media type detection reads the head of the part and then rewinds; those bytes are digested by
 * the read that follows the rewind, not here, so that they are not digested twice */
static int email_read_rewindable(vfile_t *f, void *buf, size_t size) {
    part_data_t *part = f->mem;

    const size_t to_read = MIN(size, part->size - part->cursor);
    memcpy(buf, part->data + part->cursor, to_read);
    part->cursor += to_read;

    return (int) to_read;
}

static void email_reset(vfile_t *f) {
    part_data_t *part = f->mem;
    part->cursor = 0;
}

static void email_close(vfile_t *f) {
    if (f->sha1_ctx != NULL) {
        EVP_DigestFinal_ex(f->sha1_ctx, f->sha1_digest, NULL);
        EVP_MD_CTX_free(f->sha1_ctx);
        f->sha1_ctx = NULL;
    }
}

/** How many messages or archives had to be opened to reach this file */
static int email_depth(const char *filepath) {
    int depth = 0;

    for (const char *p = filepath; (p = strstr(p, "#/")) != NULL; p += 2) {
        depth += 1;
    }

    return depth;
}

static int is_container(const ev_part *part) {
    return strncmp(part->content_type, "multipart/", 10) == 0 ||
           strcmp(part->content_type, "message/rfc822") == 0;
}

/** Index one past the last descendant of part i, which the flat depth-first list keeps contiguous */
static size_t subtree_end(const ev_message *msg, size_t i) {
    const size_t count = ev_part_count(msg);
    const size_t depth = ev_part_at(msg, i)->depth;

    size_t end = i + 1;
    while (end < count && ev_part_at(msg, end)->depth > depth) {
        end += 1;
    }

    return end;
}

/** Lower is preferred as the body of a multipart/alternative */
static int alternative_rank(const ev_part *part) {
    if (strcmp(part->content_type, "text/plain") == 0) {
        return 0;
    }
    if (part->is_text) {
        return 1;
    }
    if (strncmp(part->content_type, "multipart/", 10) == 0) {
        return 2;
    }
    return 3;
}

/*
 * A multipart/alternative holds the same message written several times over — indexing every
 * branch of one would store the text twice and count the html markup as a second copy. Only the
 * most readable branch is kept; the others are marked skipped, descendants included.
 */
static void skip_redundant_alternatives(const ev_message *msg, char *skipped) {
    const size_t count = ev_part_count(msg);

    for (size_t i = 0; i < count; i++) {
        if (skipped[i] || strcmp(ev_part_at(msg, i)->content_type, "multipart/alternative") != 0) {
            continue;
        }

        const size_t end = subtree_end(msg, i);
        const size_t child_depth = ev_part_at(msg, i)->depth + 1;

        size_t best = end;
        int best_rank = 0;

        for (size_t j = i + 1; j < end; j = subtree_end(msg, j)) {
            if (ev_part_at(msg, j)->depth != child_depth) {
                break;
            }

            const int rank = alternative_rank(ev_part_at(msg, j));
            if (best == end || rank < best_rank) {
                best = j;
                best_rank = rank;
            }
        }

        for (size_t j = i + 1; j < end; j = subtree_end(msg, j)) {
            if (ev_part_at(msg, j)->depth != child_depth) {
                break;
            }
            if (j == best) {
                continue;
            }

            for (size_t k = j; k < subtree_end(msg, j); k++) {
                skipped[k] = TRUE;
            }
        }
    }
}

/** A part carrying the text of the message, rather than something attached to it */
static int is_body_part(const ev_part *part) {
    return part->is_text && part->filename == NULL && part->disposition != EV_DISPOSITION_ATTACHMENT;
}

static void append_header_meta(document_t *doc, const ev_message *msg, const char *name,
                               enum metakey meta_key) {
    const char *value = ev_header(msg, name);

    if (value != NULL && *value != '\0') {
        APPEND_UTF8_META(doc, meta_key, value);
    }
}

static int append_header_text(text_buffer_t *tex, const ev_message *msg, const char *name) {
    const char *value = ev_header(msg, name);

    if (value == NULL || *value == '\0') {
        return 0;
    }

    return text_buffer_append_string0(tex, name) ||
           text_buffer_append_string0(tex, ": ") ||
           text_buffer_append_string0(tex, value) ||
           text_buffer_append_string0(tex, "\n");
}

/*
 * The headers people search mail by are not in the body, so they are indexed with it. From and
 * Subject are also stored as their own fields.
 */
static void append_content(scan_email_ctx_t *ctx, document_t *doc, const ev_message *msg,
                           const char *skipped) {
    if (ctx->content_size <= 0) {
        return;
    }

    text_buffer_t tex = text_buffer_create(ctx->content_size);

    int full = append_header_text(&tex, msg, "From") ||
               append_header_text(&tex, msg, "To") ||
               append_header_text(&tex, msg, "Cc") ||
               append_header_text(&tex, msg, "Subject") ||
               append_header_text(&tex, msg, "Date");

    const size_t count = ev_part_count(msg);
    for (size_t i = 0; i < count && !full; i++) {
        const ev_part *part = ev_part_at(msg, i);

        if (skipped[i] || !is_body_part(part) || part->size == 0) {
            continue;
        }

        if (strcmp(part->content_type, "text/html") == 0) {
            // Text parts are NUL-terminated by libenvelope
            full = text_buffer_append_markup(&tex, part->data);
        } else {
            full = text_buffer_append_string(&tex, part->data, part->size);
        }
    }

    text_buffer_terminate_string(&tex);

    if (tex.dyn_buffer.cur > 1) {
        meta_line_t *meta_content = malloc(sizeof(meta_line_t) + tex.dyn_buffer.cur);
        meta_content->key = MetaContent;
        memcpy(meta_content->str_val, tex.dyn_buffer.buf, tex.dyn_buffer.cur);
        APPEND_META(doc, meta_content);
    }

    text_buffer_destroy(&tex);
}

/** Name of a sub-document, with anything that would forge a path in it replaced */
static void sanitize_name(const char *name, char *buf, size_t buf_size) {
    size_t len = strlen(name);

    if (len >= buf_size) {
        len = buf_size - 1;
    }

    for (size_t i = 0; i < len; i++) {
        const unsigned char c = (unsigned char) name[i];
        buf[i] = (c == '/' || c < 0x20 || c == 0x7f) ? '_' : name[i];
    }
    buf[len] = '\0';
}

static parse_job_t *sub_job_create(scan_email_ctx_t *ctx, vfile_t *f, document_t *doc) {
    parse_job_t *sub_job = calloc(1, sizeof(parse_job_t));

    sub_job->vfile.read = email_read;
    sub_job->vfile.read_rewindable = email_read_rewindable;
    sub_job->vfile.reset = email_reset;
    sub_job->vfile.close = email_close;
    sub_job->vfile.is_fs_file = FALSE;
    sub_job->vfile.log = ctx->log;
    sub_job->vfile.logf = ctx->logf;
    sub_job->vfile.calculate_checksum = f->calculate_checksum;
    sub_job->vfile.mtime = f->mtime;
    strcpy(sub_job->parent, doc->filepath);

    return sub_job;
}

/** Hands one part of a message to the parser as a document of its own */
static void sub_job_submit(scan_email_ctx_t *ctx, vfile_t *f, parse_job_t *sub_job,
                           const char *name, const char *data, size_t size) {
    char safe_name[PATH_MAX];
    sanitize_name(name, safe_name, sizeof(safe_name));

    const int filepath_len = snprintf(sub_job->filepath, sizeof(sub_job->filepath), "%s#/%s",
                                      f->filepath, safe_name);
    if (filepath_len < 0 || filepath_len >= (int) sizeof(sub_job->filepath)) {
        CTX_LOG_ERRORF(f->filepath, "Skipped %s, path too long", safe_name);
        return;
    }
    strcpy(sub_job->vfile.filepath, sub_job->filepath);
    sub_job->base = (int) (strrchr(sub_job->filepath, '/') - sub_job->filepath) + 1;

    const char *dot = strrchr(sub_job->filepath, '.');
    if (dot != NULL && (dot - sub_job->filepath) > (long) strlen(f->filepath)) {
        sub_job->ext = (int) (dot - sub_job->filepath + 1);
    } else {
        // No extension of its own: the media type comes from the content instead
        sub_job->ext = (int) strlen(sub_job->filepath);
    }

    part_data_t part_data = {.data = data, .size = size, .cursor = 0};

    sub_job->vfile.mem = &part_data;
    sub_job->vfile.st_size = size;
    sub_job->vfile.has_checksum = FALSE;
    sub_job->vfile.read_offset = 0;
    sub_job->vfile.digested_bytes = 0;
    sub_job->vfile.sha1_ctx = EVP_MD_CTX_new();
    EVP_DigestInit(sub_job->vfile.sha1_ctx, EVP_sha1());

    ctx->parse(sub_job);

    sub_job->vfile.close(&sub_job->vfile);
}

static void parse_attachments(scan_email_ctx_t *ctx, vfile_t *f, document_t *doc,
                              const ev_message *msg, const char *skipped) {
    parse_job_t *sub_job = sub_job_create(ctx, f, doc);

    const size_t count = ev_part_count(msg);
    char name[PATH_MAX];

    for (size_t i = 0; i < count; i++) {
        const ev_part *part = ev_part_at(msg, i);

        if (skipped[i] || is_container(part) || is_body_part(part) || part->size == 0) {
            continue;
        }

        if (part->filename != NULL) {
            snprintf(name, sizeof(name), "%s", part->filename);
        } else {
            snprintf(name, sizeof(name), "part-%zu", i);
        }

        sub_job_submit(ctx, f, sub_job, name, part->data, part->size);
    }

    free(sub_job);
}

scan_code_t parse_email(scan_email_ctx_t *ctx, vfile_t *f, document_t *doc) {

    size_t buf_len;
    char *buf = read_all(f, &buf_len);
    if (buf == NULL) {
        CTX_LOG_ERROR(f->filepath, "read_all() failed");
        return SCAN_ERR_READ;
    }

    const ev_options opts = {
            .max_mem = EMAIL_MAX_MEM,
            .max_decoded = EMAIL_MAX_DECODED,
    };
    ev_message *msg = NULL;
    const ev_status status = ev_parse(buf, buf_len, &opts, &msg);
    free(buf);

    if (status != EV_OK) {
        CTX_LOG_WARNINGF(f->filepath, "ev_parse() failed: %s", ev_strerror(status));
        return SCAN_ERR_READ;
    }

    append_header_meta(doc, msg, "subject", MetaTitle);
    append_header_meta(doc, msg, "from", MetaAuthor);

    char *skipped = calloc(MAX(ev_part_count(msg), (size_t) 1), 1);
    skip_redundant_alternatives(msg, skipped);

    append_content(ctx, doc, msg, skipped);

    if (email_depth(f->filepath) < MAX_EMAIL_DEPTH) {
        parse_attachments(ctx, f, doc, msg, skipped);
    } else {
        CTX_LOG_ERRORF(f->filepath, "Attachments skipped, messages nested more than %d deep",
                       MAX_EMAIL_DEPTH);
    }

    free(skipped);
    ev_free(msg);

    return SCAN_OK;
}

scan_code_t parse_mbox(scan_email_ctx_t *ctx, vfile_t *f, document_t *doc) {

    if (email_depth(f->filepath) >= MAX_EMAIL_DEPTH) {
        CTX_LOG_ERRORF(f->filepath, "Skipped, mailboxes nested more than %d deep", MAX_EMAIL_DEPTH);
        return SCAN_OK;
    }

    size_t buf_len;
    char *buf = read_all(f, &buf_len);
    if (buf == NULL) {
        CTX_LOG_ERROR(f->filepath, "read_all() failed");
        return SCAN_ERR_READ;
    }

    // An open mbox reads from the buffer as it goes, so it stays alive until ev_mbox_free()
    ev_mbox *mbox = NULL;
    const ev_status status = ev_mbox_open(buf, buf_len, NULL, &mbox);

    if (status != EV_OK) {
        CTX_LOG_WARNINGF(f->filepath, "ev_mbox_open() failed: %s", ev_strerror(status));
        free(buf);
        return SCAN_ERR_READ;
    }

    parse_job_t *sub_job = sub_job_create(ctx, f, doc);

    const size_t count = ev_mbox_count(mbox);
    char name[64];

    for (size_t i = 0; i < count; i++) {
        const char *data;
        size_t size;

        if (!ev_mbox_raw(mbox, i, &data, &size) || size == 0) {
            continue;
        }

        // The .eml extension dispatches the message straight back to parse_email()
        snprintf(name, sizeof(name), "message-%zu.eml", i);

        // Unquoting never lengthens a message, so its own size always holds the result
        char *message = malloc(size);
        if (message == NULL) {
            CTX_LOG_ERRORF(f->filepath, "Skipped message %zu, out of memory", i);
            continue;
        }

        const size_t message_len = ev_mbox_unquote(data, size, message);

        sub_job_submit(ctx, f, sub_job, name, message, message_len);

        free(message);
    }

    free(sub_job);
    ev_mbox_free(mbox);
    free(buf);

    return SCAN_OK;
}

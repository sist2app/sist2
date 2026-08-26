#include "sub_document.h"

#include "util.h"

/** Body of one sub-document, read by its parser through a vfile */
typedef struct {
    const char *data;
    size_t size;
    size_t cursor;
} sub_document_data_t;

static int sub_document_read(vfile_t *f, void *buf, size_t size) {
    sub_document_data_t *document = f->mem;

    const size_t to_read = MIN(size, document->size - document->cursor);
    memcpy(buf, document->data + document->cursor, to_read);
    document->cursor += to_read;

    if (to_read > 0 && f->calculate_checksum) {
        f->has_checksum = TRUE;
        safe_digest_update(f->sha1_ctx, buf, to_read);
    }

    return (int) to_read;
}

/* Media type detection reads the head of the document and then rewinds; those bytes are digested
 * by the read that follows the rewind, not here, so that they are not digested twice */
static int sub_document_read_rewindable(vfile_t *f, void *buf, size_t size) {
    sub_document_data_t *document = f->mem;

    const size_t to_read = MIN(size, document->size - document->cursor);
    memcpy(buf, document->data + document->cursor, to_read);
    document->cursor += to_read;

    return (int) to_read;
}

static void sub_document_reset(vfile_t *f) {
    sub_document_data_t *document = f->mem;
    document->cursor = 0;
}

static void sub_document_close(vfile_t *f) {
    if (f->sha1_ctx != NULL) {
        EVP_DigestFinal_ex(f->sha1_ctx, f->sha1_digest, NULL);
        EVP_MD_CTX_free(f->sha1_ctx);
        f->sha1_ctx = NULL;
    }
}

int sub_document_depth(const char *filepath) {
    int depth = 0;

    for (const char *p = filepath; (p = strstr(p, "#/")) != NULL; p += 2) {
        depth += 1;
    }

    return depth;
}

void sub_document_sanitize_name(const char *name, char *buf, size_t buf_size) {
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

parse_job_t *sub_document_job_create(vfile_t *f, const char *parent, log_callback_t log,
                                     logf_callback_t logf) {
    parse_job_t *sub_job = calloc(1, sizeof(parse_job_t));

    sub_job->vfile.read = sub_document_read;
    sub_job->vfile.read_rewindable = sub_document_read_rewindable;
    sub_job->vfile.reset = sub_document_reset;
    sub_job->vfile.close = sub_document_close;
    sub_job->vfile.is_fs_file = FALSE;
    sub_job->vfile.log = log;
    sub_job->vfile.logf = logf;
    sub_job->vfile.calculate_checksum = f->calculate_checksum;
    sub_job->vfile.mtime = f->mtime;
    strcpy(sub_job->parent, parent);

    return sub_job;
}

int sub_document_submit(parse_callback_t parse, vfile_t *f, parse_job_t *sub_job, const char *name,
                        const char *data, size_t size) {
    const int filepath_len = snprintf(sub_job->filepath, sizeof(sub_job->filepath), "%s#/%s",
                                      f->filepath, name);
    if (filepath_len < 0 || filepath_len >= (int) sizeof(sub_job->filepath)) {
        f->logf(f->filepath, LEVEL_ERROR, "Skipped %s, path too long", name);
        return FALSE;
    }
    strcpy(sub_job->vfile.filepath, sub_job->filepath);
    sub_job->base = (int) (strrchr(sub_job->filepath, '/') - sub_job->filepath) + 1;

    // Only the last path component is its own name; every one before it belongs to a parent
    const char *dot = strrchr(sub_job->filepath + sub_job->base, '.');
    if (dot != NULL) {
        sub_job->ext = (int) (dot - sub_job->filepath + 1);
    } else {
        // No extension of its own: the media type comes from the content instead
        sub_job->ext = (int) strlen(sub_job->filepath);
    }

    sub_document_data_t document_data = {.data = data, .size = size, .cursor = 0};

    sub_job->vfile.mem = &document_data;
    sub_job->vfile.st_size = size;
    sub_job->vfile.has_checksum = FALSE;
    sub_job->vfile.read_offset = 0;
    sub_job->vfile.digested_bytes = 0;
    sub_job->vfile.sha1_ctx = EVP_MD_CTX_new();
    EVP_DigestInit(sub_job->vfile.sha1_ctx, EVP_sha1());

    parse(sub_job);

    sub_job->vfile.close(&sub_job->vfile);

    return TRUE;
}

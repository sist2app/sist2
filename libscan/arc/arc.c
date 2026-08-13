#include "arc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <pcre.h>

#define MAX_DECOMPRESSED_SIZE_RATIO 40.0

static const char *COMPRESSION_EXTENSIONS[] = {
        ".gz", ".tgz", ".bz2", ".tbz2", ".xz", ".txz", ".zst", ".tzst",
        ".lz", ".lz4", ".lzma", ".lzo", ".Z", ".uu", ".rpm"
};

/**
 * Name of the single member of a compressed stream: the file name without its compression
 * extension, so that the member keeps the extension the parsers dispatch on.
 */
static void arc_raw_entry_name(const char *filepath, char *buf, size_t buf_size) {
    const char *name = strrchr(filepath, '/');
    name = name == NULL ? filepath : name + 1;

    size_t name_len = strlen(name);

    for (size_t i = 0; i < sizeof(COMPRESSION_EXTENSIONS) / sizeof(COMPRESSION_EXTENSIONS[0]); i++) {
        size_t ext_len = strlen(COMPRESSION_EXTENSIONS[i]);

        if (name_len > ext_len && strcmp(name + name_len - ext_len, COMPRESSION_EXTENSIONS[i]) == 0) {
            name_len -= ext_len;
            break;
        }
    }

    if (name_len >= buf_size) {
        name_len = buf_size - 1;
    }

    memcpy(buf, name, name_len);
    buf[name_len] = '\0';
}

void arc_close(struct vfile *f) {
    if (f->sha1_ctx != NULL) {
        EVP_DigestFinal_ex(f->sha1_ctx, f->sha1_digest, NULL);
        EVP_MD_CTX_free(f->sha1_ctx);
        f->sha1_ctx = NULL;
    }

    if (f->rewind_buffer != NULL) {
        free(f->rewind_buffer);
        f->rewind_buffer = NULL;
        f->rewind_buffer_size = 0;
        f->rewind_buffer_cursor = 0;
    }
}


int arc_read(struct vfile *f, void *buf, size_t size) {

    int bytes_copied = 0;

    if (f->rewind_buffer_size != 0) {
        if (size > (size_t) f->rewind_buffer_size) {
            memcpy(buf, (char *) f->rewind_buffer + f->rewind_buffer_cursor, f->rewind_buffer_size);

            bytes_copied = f->rewind_buffer_size;
            size -= f->rewind_buffer_size;
            buf = (char *) buf + f->rewind_buffer_size;
            f->rewind_buffer_size = 0;
        } else {
            memcpy(buf, (char *) f->rewind_buffer + f->rewind_buffer_cursor, size);
            f->rewind_buffer_size -= (int) size;
            f->rewind_buffer_cursor += (int) size;

            return (int) size;
        }
    }

    size_t bytes_read = archive_read_data(f->arc, buf, size);

    if (bytes_read != 0 && bytes_read <= size && f->calculate_checksum) {
        f->has_checksum = TRUE;

        safe_digest_update(f->sha1_ctx, (unsigned char *) buf, bytes_read);
    }

    if (bytes_read != size && archive_errno(f->arc) != 0) {
        const char *error_str = archive_error_string(f->arc);
        if (error_str != NULL) {
            f->logf(f->filepath, LEVEL_ERROR, "Error reading archive file: %s", error_str);
        }
        return -1;
    }

    return (int) bytes_read + bytes_copied;
}

int arc_read_rewindable(struct vfile *f, void *buf, size_t size) {

    if (f->rewind_buffer != NULL) {
        fprintf(stderr, "Allocated rewind buffer more than once for %s", f->filepath);
        exit(-1);
    }

    size_t bytes_read = archive_read_data(f->arc, buf, size);

    if (bytes_read != size && archive_errno(f->arc) != 0) {
        const char *error_str = archive_error_string(f->arc);
        if (error_str != NULL) {
            f->logf(f->filepath, LEVEL_ERROR, "Error reading archive file: %s", error_str);
        }
        return -1;
    }

    // The parser reads these bytes again from the rewind buffer, where they are not digested
    if (bytes_read != 0 && f->calculate_checksum) {
        f->has_checksum = TRUE;
        safe_digest_update(f->sha1_ctx, (unsigned char *) buf, bytes_read);
    }

    f->rewind_buffer = malloc(size);
    f->rewind_buffer_size = (int) size;
    f->rewind_buffer_cursor = 0;
    memcpy(f->rewind_buffer, buf, size);

    return (int) bytes_read;
}

int arc_open(scan_arc_ctx_t *ctx, vfile_t *f, struct archive **a, arc_data_t *arc_data, int allow_recurse) {
    arc_data->f = f;

    if (f->is_fs_file) {
        *a = archive_read_new();
        archive_read_support_filter_all(*a);
        archive_read_support_format_all(*a);
        // Not covered by _all(): a compressed stream that is not an archive has a single member
        archive_read_support_format_raw(*a);
        if (ctx->passphrase[0] != 0) {
            archive_read_add_passphrase(*a, ctx->passphrase);
        }

        return archive_read_open_filename(*a, f->filepath, ARC_BUF_SIZE);
    } else if (allow_recurse) {
        *a = archive_read_new();
        archive_read_support_filter_all(*a);
        archive_read_support_format_all(*a);
        // Not covered by _all(): a compressed stream that is not an archive has a single member
        archive_read_support_format_raw(*a);
        if (ctx->passphrase[0] != 0) {
            archive_read_add_passphrase(*a, ctx->passphrase);
        }

        return archive_read_open(
                *a, arc_data,
                vfile_open_callback,
                vfile_read_callback,
                vfile_close_callback
        );
    } else {
        return ARC_SKIPPED;
    }
}

static __thread int sub_strings[30];
#define EXCLUDED(str) (pcre_exec(exclude, exclude_extra, str, strlen(str), 0, 0, sub_strings, sizeof(sub_strings)) >= 0)

scan_code_t parse_archive(scan_arc_ctx_t *ctx, vfile_t *f, document_t *doc, pcre *exclude, pcre_extra *exclude_extra) {

    struct archive *a = NULL;
    struct archive_entry *entry = NULL;

    arc_data_t arc_data;
    arc_data.f = f;

    int ret = arc_open(ctx, f, &a, &arc_data, ctx->mode == ARC_MODE_RECURSE);
    if (ret == ARC_SKIPPED) {
        return SCAN_OK;
    }

    if (ret != ARCHIVE_OK) {
        CTX_LOG_ERRORF(f->filepath, "(arc.c) [%d] %s", ret, archive_error_string(a));
        archive_read_free(a);
        return SCAN_ERR_READ;
    }

    if (ctx->mode == ARC_MODE_LIST) {
        dyn_buffer_t buf = dyn_buffer_create();

        while (archive_read_next_header(a, &entry) == ARCHIVE_OK) {
            if (S_ISREG(archive_entry_stat(entry)->st_mode)) {
                const char *utf8_name = archive_entry_pathname_utf8(entry);
                const char *file_path = utf8_name == NULL ? archive_entry_pathname(entry) : utf8_name;

                dyn_buffer_append_string(&buf, file_path);
                dyn_buffer_write_char(&buf, ' ');
            }
        }
        dyn_buffer_write_char(&buf, '\0');

        meta_line_t *meta_list = malloc(sizeof(meta_line_t) + buf.cur);
        meta_list->key = MetaContent;
        strcpy(meta_list->str_val, buf.buf);
        APPEND_META(doc, meta_list);
        dyn_buffer_destroy(&buf);

    } else {

        parse_job_t *sub_job = calloc(1, sizeof(parse_job_t));

        sub_job->vfile.close = arc_close;
        sub_job->vfile.read = arc_read;
        sub_job->vfile.read_rewindable = arc_read_rewindable;
        sub_job->vfile.reset = NULL;
        sub_job->vfile.arc = a;
        sub_job->vfile.is_fs_file = FALSE;
        sub_job->vfile.rewind_buffer_size = 0;
        sub_job->vfile.rewind_buffer = NULL;
        sub_job->vfile.log = ctx->log;
        sub_job->vfile.logf = ctx->logf;
        sub_job->vfile.has_checksum = FALSE;
        sub_job->vfile.calculate_checksum = f->calculate_checksum;
        strcpy(sub_job->parent, doc->filepath);

        char raw_name[PATH_MAX];

        while (archive_read_next_header(a, &entry) == ARCHIVE_OK) {
            struct stat entry_stat = *archive_entry_stat(entry);
            sub_job->vfile.st_size = entry_stat.st_size;
            sub_job->vfile.mtime = (int) entry_stat.st_mtim.tv_sec;

            // A compressed stream that is not an archive has a single member; its name, size and
            // mtime are only known when the compression header carries them
            if (archive_format(a) == ARCHIVE_FORMAT_RAW) {
                if (!archive_entry_size_is_set(entry)) {
                    sub_job->vfile.st_size = f->st_size;
                }
                if (!archive_entry_mtime_is_set(entry)) {
                    sub_job->vfile.mtime = f->mtime;
                }
            }

            if (S_ISREG(entry_stat.st_mode)) {

                const char *entry_name = archive_entry_pathname_utf8(entry);
                if (entry_name == NULL) {
                    entry_name = archive_entry_pathname(entry);
                }

                // The name the raw format falls back to when the compression header has none
                if (archive_format(a) == ARCHIVE_FORMAT_RAW && strcmp(entry_name, "data") == 0) {
                    arc_raw_entry_name(f->filepath, raw_name, sizeof(raw_name));
                    entry_name = raw_name;
                }

                int filepath_len = snprintf(sub_job->filepath, sizeof(sub_job->filepath), "%s#/%s",
                                            f->filepath, entry_name);
                if (filepath_len < 0 || filepath_len >= (int) sizeof(sub_job->filepath)) {
                    CTX_LOG_ERRORF("arc.c", "Skipped %s, path too long", f->filepath);
                    continue;
                }
                strcpy(sub_job->vfile.filepath, sub_job->filepath);
                sub_job->base = (int) (strrchr(sub_job->filepath, '/') - sub_job->filepath) + 1;

                double decompressed_size_ratio = (double) sub_job->vfile.st_size / (double) f->st_size;
                if (decompressed_size_ratio > MAX_DECOMPRESSED_SIZE_RATIO) {
                    CTX_LOG_ERRORF("arc.c", "Skipped %s, possible zip bomb (decompressed_size_ratio=%f)",
                                   sub_job->filepath,
                                   decompressed_size_ratio);
                    continue;
                }

                if ((archive_entry_is_encrypted(entry) || archive_entry_is_data_encrypted(entry) ||
                     archive_entry_is_metadata_encrypted(entry)) && ctx->passphrase[0] == 0) {
                    // Is encrypted but no password is specified, skip
                    CTX_LOG_ERRORF("arc.c", "Skipped %s, archive is encrypted but no passphrase is supplied",
                                   doc->filepath);
                    break;
                }

                // Handle excludes
                if (exclude != NULL && EXCLUDED(sub_job->filepath)) {
                    CTX_LOG_DEBUGF("arc.c", "Excluded: %s", sub_job->filepath);
                    continue;
                }

                char *p = strrchr(sub_job->filepath, '.');
                if (p != NULL && (p - sub_job->filepath) > (long) strlen(f->filepath)) {
                    sub_job->ext = (int) (p - sub_job->filepath + 1);
                } else {
                    sub_job->ext = (int) strlen(sub_job->filepath);
                }

                // sub_job is reused for every entry
                sub_job->vfile.has_checksum = FALSE;
                sub_job->vfile.read_offset = 0;
                sub_job->vfile.digested_bytes = 0;
                sub_job->vfile.sha1_ctx = EVP_MD_CTX_new();
                EVP_DigestInit(sub_job->vfile.sha1_ctx, EVP_sha1());

                ctx->parse(sub_job);

                sub_job->vfile.close(&sub_job->vfile);
            }
        }

        free(sub_job);
    }

    archive_read_free(a);
    return SCAN_OK;
}

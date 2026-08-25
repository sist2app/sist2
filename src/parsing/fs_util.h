#ifndef SIST2_FS_UTIL_H
#define SIST2_FS_UTIL_H

#include "src/sist.h"
#include <openssl/evp.h>

#define CLOSE_FILE(f) if ((f).close != NULL) {(f).close(&(f));};

static inline int fs_read(struct vfile *f, void *buf, size_t size) {
    if (f->fd == -1) {
        f->fd = sist_open(f->filepath, O_RDONLY | O_BINARY);
        if (f->fd == -1) {
            return -1;
        }

        if (f->calculate_checksum) {
            f->sha1_ctx = EVP_MD_CTX_new();
            EVP_DigestInit_ex(f->sha1_ctx, EVP_sha1(), NULL);
        }
    }

    int ret = (int) read(f->fd, buf, size);

    if (ret > 0) {
        if (f->calculate_checksum && f->read_offset + ret > f->digested_bytes) {
            const int digested_head = (int) (f->digested_bytes - f->read_offset);
            const int offset = digested_head > 0 ? digested_head : 0;

            f->has_checksum = TRUE;
            safe_digest_update(f->sha1_ctx, (unsigned char *) buf + offset, ret - offset);
            f->digested_bytes = f->read_offset + ret;
        }

        f->read_offset += ret;
    }

    return ret;
}

static inline void fs_close(struct vfile *f) {
    if (f->fd != -1) {
        if (f->sha1_ctx != NULL) {
            EVP_DigestFinal_ex(f->sha1_ctx, f->sha1_digest, NULL);
            EVP_MD_CTX_free(f->sha1_ctx);
            f->sha1_ctx = NULL;
        }
        close(f->fd);
        f->fd = -1;
    }
}

static inline void fs_reset(struct vfile *f) {
    if (f->fd != -1) {
        lseek(f->fd, 0, SEEK_SET);
        f->read_offset = 0;
    }
}

#endif

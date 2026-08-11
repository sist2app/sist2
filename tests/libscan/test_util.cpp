#include "test_util.h"
#include <gtest/gtest.h>

#include <unistd.h>
#include <fcntl.h>

#define FILE_NOT_FOUND_ERR "Could not file, did you clone the test files repo?"


int fs_read(struct vfile *f, void *buf, size_t size) {

    if (f->fd == -1) {
        f->fd = open(f->filepath, O_RDONLY);
        if (f->fd == -1) {
            return -1;
        }
    }

    return (int) read(f->fd, buf, size);
}

//Note: No out of bounds check
int mem_read(vfile_t *f, void *buf, size_t size) {
    memcpy(buf, f->_test_data, size);
    f->_test_data = (char *) f->_test_data + size;
    return 0;
}

void fs_close(vfile_t *f) {
    if (f->fd != -1) {
        close(f->fd);
    }
}

void load_doc_file(const char *filepath, vfile_t *f, document_t *doc) {
    doc->meta_head = nullptr;
    doc->meta_tail = nullptr;
    load_file(filepath, f);
}

void load_doc_mem(void *mem, size_t mem_len, vfile_t *f, document_t *doc) {
    doc->meta_head = nullptr;
    doc->meta_tail = nullptr;
    load_mem(mem, mem_len, f);
}

void cleanup(document_t *doc, vfile_t *f) {
    destroy_doc(doc);
    CLOSE_FILE((*f))
}

void load_file(const char *filepath, vfile_t *f) {
    struct stat info = {};
    stat(filepath, &info);

    f->mtime = (int)info.st_mtim.tv_sec;
    f->st_size = info.st_size;

    f->fd = open(filepath, O_RDONLY);

    if (f->fd == -1) {
        FAIL() << FILE_NOT_FOUND_ERR;
    }

    memcpy(f->filepath, filepath, sizeof(f->filepath));
    f->read = fs_read;
    f->close = fs_close;
    f->is_fs_file = TRUE;
    f->calculate_checksum = TRUE;
    f->has_checksum = FALSE;
}

void load_mem(void *mem, size_t size, vfile_t *f) {
    memcpy(f->filepath, "_mem_", strlen("_mem_"));
    f->_test_data = mem;
    f->st_size = size;
    f->read = mem_read;
    f->close = nullptr;
    f->is_fs_file = TRUE;
}

meta_line_t *get_meta(document_t *doc, metakey key) {
    return get_meta_from(doc->meta_head, key);
}

meta_line_t *get_meta_from(meta_line_t *meta, metakey key) {
    while (meta != nullptr) {
        if (meta->key == key) {
            return meta;
        }
        meta = meta->next;
    }
    return nullptr;
}

void destroy_doc(document_t *doc) {
    meta_line_t *meta = doc->meta_head;
    while (meta != nullptr) {
        meta_line_t *tmp = meta;
        meta = tmp->next;
        free(tmp);
    }
}

void fuzz_buffer(char *buf, size_t *buf_len, int width, int n, int trunc_p) {
    for (int i = 0; i < n; i++) {

        size_t offset = rand() % (*buf_len - width - 1);

        if (rand() % 100 < trunc_p) {
            *buf_len = MAX(offset, 1000);
            continue;
        }

        for (int disp = 0; disp < width; disp++) {
            buf[offset + disp] = (int8_t) rand();
        }
    }
}

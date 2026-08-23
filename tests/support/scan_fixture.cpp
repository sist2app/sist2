#include "scan_fixture.h"

#include <algorithm>
#include <filesystem>

#include <fcntl.h>
#include <unistd.h>

#define FILE_NOT_FOUND_ERR "Could not open test file, did you clone the test files repo?"

std::string test_file(const std::string &relative_path) {
    return std::string(SIST2_TEST_FILES_DIR) + "/" + relative_path;
}

std::vector<std::string> corpus_files() {
    namespace fs = std::filesystem;

    const fs::path root(SIST2_TEST_FILES_DIR);
    std::vector<std::string> files;

    if (!fs::is_directory(root)) {
        return files;
    }

    for (const auto &entry: fs::recursive_directory_iterator(root)) {
        if (entry.is_regular_file()) {
            files.push_back(fs::relative(entry.path(), root).string());
        }
    }

    std::sort(files.begin(), files.end());

    return files;
}

const char *tessdata_path() {
    static const char *cached = nullptr;

    if (cached != nullptr) {
        return cached;
    }

    const char *candidates[] = {
            getenv("TESSDATA_PREFIX"),
            "./tessdata",
            "/usr/share/tesseract-ocr/5/tessdata",
            "/usr/share/tesseract-ocr/4.00/tessdata",
            "/usr/share/tessdata",
    };

    for (const char *candidate: candidates) {
        if (candidate == nullptr) {
            continue;
        }

        char traineddata[PATH_MAX];
        snprintf(traineddata, sizeof(traineddata), "%s/eng.traineddata", candidate);

        if (access(traineddata, R_OK) == 0) {
            cached = candidate;
            return cached;
        }
    }

    fprintf(stderr, "WARNING: eng.traineddata not found, the OCR tests will fail. "
                    "Set TESSDATA_PREFIX to a tessdata folder.\n");
    cached = "./tessdata";
    return cached;
}

bool tesseract_has_language(const char *lang) {
    const char *path = tessdata_path();

    if (path == nullptr) {
        return false;
    }

    return std::filesystem::exists(std::filesystem::path(path) / (std::string(lang) + ".traineddata"));
}

void noop_log(const char *, int, char *) {
    // noop
}

void noop_logf(const char *, int, char *, ...) {
    // noop
}

scan_text_ctx_t make_text_ctx(long content_size) {
    scan_text_ctx_t ctx = {};

    ctx.content_size = content_size;
    ctx.log = noop_log;
    ctx.logf = noop_logf;

    return ctx;
}

scan_ebook_ctx_t make_ebook_ctx(long content_size, int fast_epub_parse) {
    scan_ebook_ctx_t ctx = {};

    ctx.content_size = content_size;
    ctx.tesseract_lang = "eng";
    ctx.tesseract_path = tessdata_path();
    ctx.tn_size = 500;
    ctx.tn_qscale = 2;
    ctx.enable_tn = TRUE;
    ctx.fast_epub_parse = fast_epub_parse;
    ctx.log = noop_log;
    ctx.logf = noop_logf;

    return ctx;
}

scan_comic_ctx_t make_comic_ctx(int tn_size, int enable_tn) {
    scan_comic_ctx_t ctx = {};

    ctx.tn_size = tn_size;
    ctx.tn_qscale = 2;
    ctx.enable_tn = enable_tn;
    ctx.log = noop_log;
    ctx.logf = noop_logf;

    return ctx;
}

scan_media_ctx_t make_media_ctx(int tn_size, int tn_count) {
    scan_media_ctx_t ctx = {};

    ctx.tn_size = tn_size;
    ctx.tn_count = tn_count;
    ctx.tn_qscale = 2;
    ctx.max_media_buffer = (long) 2000 * (long) 1024 * (long) 1024;
    ctx.log = noop_log;
    ctx.logf = noop_logf;

    return ctx;
}

scan_ooxml_ctx_t make_ooxml_ctx(long content_size, int enable_tn) {
    scan_ooxml_ctx_t ctx = {};

    ctx.content_size = content_size;
    ctx.enable_tn = enable_tn;
    ctx.log = noop_log;
    ctx.logf = noop_logf;

    return ctx;
}

scan_mobi_ctx_t make_mobi_ctx(long content_size) {
    scan_mobi_ctx_t ctx = {};

    ctx.content_size = content_size;
    ctx.log = noop_log;
    ctx.logf = noop_logf;

    return ctx;
}

scan_raw_ctx_t make_raw_ctx(int tn_size, int enable_tn) {
    scan_raw_ctx_t ctx = {};

    ctx.tn_size = tn_size;
    ctx.tn_qscale = 5;
    ctx.enable_tn = enable_tn;
    ctx.log = noop_log;
    ctx.logf = noop_logf;

    return ctx;
}

scan_msdoc_ctx_t make_msdoc_ctx(long content_size) {
    scan_msdoc_ctx_t ctx = {};

    ctx.content_size = content_size;
    ctx.log = noop_log;
    ctx.logf = noop_logf;

    return ctx;
}

scan_wpd_ctx_t make_wpd_ctx(long content_size) {
    scan_wpd_ctx_t ctx = {};

    ctx.content_size = content_size;
    ctx.log = noop_log;
    ctx.logf = noop_logf;

    return ctx;
}

scan_json_ctx_t make_json_ctx(long content_size) {
    scan_json_ctx_t ctx = {};

    ctx.content_size = content_size;
    ctx.log = noop_log;
    ctx.logf = noop_logf;

    return ctx;
}

scan_font_ctx_t make_font_ctx(int enable_tn) {
    scan_font_ctx_t ctx = {};

    ctx.enable_tn = enable_tn;
    ctx.log = noop_log;
    ctx.logf = noop_logf;

    return ctx;
}

/*
 * parse_callback_t is a plain function pointer with no user-data argument, so the sub-document
 * parser of the test that is currently running is stashed here for the trampoline to find.
 */
namespace {
    struct ArcState {
        SubDocCollector *collector;
        SubDocParser parser;
    };

    thread_local ArcState arc_state;

    void arc_parse_trampoline(parse_job_t *job) {
        if (arc_state.collector == nullptr || !arc_state.parser) {
            return;
        }

        arc_state.parser(job, arc_state.collector->create());
    }
}

scan_arc_ctx_t make_arc_ctx(archive_mode_t mode, const char *passphrase) {
    scan_arc_ctx_t ctx = {};

    ctx.mode = mode;
    ctx.parse = arc_parse_trampoline;
    ctx.log = noop_log;
    ctx.logf = noop_logf;

    if (passphrase != nullptr) {
        strncpy(ctx.passphrase, passphrase, sizeof(ctx.passphrase) - 1);
    }

    return ctx;
}

scan_email_ctx_t make_email_ctx(long content_size) {
    scan_email_ctx_t ctx = {};

    ctx.content_size = content_size;
    ctx.parse = arc_parse_trampoline;
    ctx.log = noop_log;
    ctx.logf = noop_logf;

    return ctx;
}

meta_line_t *get_meta(const document_t *doc, metakey key) {
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

size_t thumbnail_size(const document_t *doc) {
    size_t size = 0;

    for (meta_line_t *meta = doc->meta_head; meta != nullptr; meta = meta->next) {
        if (meta->key == MetaThumbnail) {
            size += meta->size;
        }
    }

    return size;
}

int thumbnail_meta_count(const document_t *doc) {
    int count = 0;

    for (meta_line_t *meta = doc->meta_head; meta != nullptr; meta = meta->next) {
        if (meta->key == MetaThumbnail) {
            count += 1;
        }
    }

    return count;
}

void destroy_doc(document_t *doc) {
    meta_line_t *meta = doc->meta_head;
    while (meta != nullptr) {
        meta_line_t *tmp = meta;
        meta = tmp->next;
        free(tmp);
    }
    doc->meta_head = nullptr;
    doc->meta_tail = nullptr;
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

/* Fixtures */

static int fs_read(struct vfile *f, void *buf, size_t size) {
    if (f->fd == -1) {
        f->fd = open(f->filepath, O_RDONLY);
        if (f->fd == -1) {
            return -1;
        }
    }

    return (int) read(f->fd, buf, size);
}

static void fs_close(vfile_t *f) {
    if (f->fd != -1) {
        close(f->fd);
        f->fd = -1;
    }
}

namespace {
    /** Cursor into the buffer passed to ScanTest::load_mem(), stored in vfile_t::_test_data */
    struct MemCursor {
        const char *cur;
        const char *end;
    };
}

static int mem_read(vfile_t *f, void *buf, size_t size) {
    auto *cursor = (MemCursor *) f->_test_data;

    const size_t remaining = cursor->end - cursor->cur;
    const size_t to_read = MIN(size, remaining);

    memcpy(buf, cursor->cur, to_read);
    cursor->cur += to_read;

    return (int) to_read;
}

void ScanTest::load(const std::string &relative_path) {
    load_path(test_file(relative_path));
}

void ScanTest::load_path(const std::string &path) {
    struct stat info = {};
    stat(path.c_str(), &info);

    memset(&f, 0, sizeof(f));

    f.mtime = (int) info.st_mtim.tv_sec;
    f.st_size = info.st_size;

    f.fd = open(path.c_str(), O_RDONLY);

    if (f.fd == -1) {
        FAIL() << FILE_NOT_FOUND_ERR << " (" << path << ")";
    }

    ASSERT_LT(path.size(), sizeof(f.filepath));
    strcpy(f.filepath, path.c_str());

    f.read = fs_read;
    f.close = fs_close;
    f.log = noop_log;
    f.logf = noop_logf;
    f.is_fs_file = TRUE;
    f.calculate_checksum = TRUE;
    f.has_checksum = FALSE;
}

void ScanTest::load_mem(const void *buf, size_t len) {
    memset(&f, 0, sizeof(f));

    // Freed by TearDown(), through the same _test_data pointer
    auto *cursor = new MemCursor{(const char *) buf, (const char *) buf + len};

    strcpy(f.filepath, "_mem_");
    f._test_data = cursor;
    f.st_size = len;
    f.read = mem_read;
    f.close = nullptr;
    f.log = noop_log;
    f.logf = noop_logf;
    f.is_fs_file = TRUE;
}

void ScanTest::TearDown() {
    destroy_doc(&doc);

    if (f.close != nullptr) {
        f.close(&f);
    } else if (f.read == mem_read) {
        delete (MemCursor *) f._test_data;
        f._test_data = nullptr;
    }
}

void ScanTest::reset() {
    TearDown();

    doc = {};
    f = {};
}

const char *ScanTest::content() const {
    meta_line_t *meta_content = meta(MetaContent);

    if (meta_content == nullptr) {
        ADD_FAILURE() << "Document has no MetaContent";
        return "";
    }

    return meta_content->str_val;
}

SubDocCollector::~SubDocCollector() {
    for (document_t *doc: docs_) {
        destroy_doc(doc);
        delete doc;
    }
}

document_t *SubDocCollector::create() {
    docs_.push_back(new document_t{});
    return docs_.back();
}

document_t *SubDocCollector::at(size_t index) const {
    EXPECT_LT(index, docs_.size()) << "No sub-document at index " << index;

    if (index >= docs_.size()) {
        static document_t empty = {};
        return &empty;
    }

    return docs_[index];
}

document_t *SubDocCollector::last() const {
    EXPECT_FALSE(docs_.empty()) << "No sub-document was parsed";

    return at(docs_.empty() ? 0 : docs_.size() - 1);
}

void ArcTest::recurse_into(SubDocParser parser) {
    arc_state.collector = &sub_docs;
    arc_state.parser = std::move(parser);
}

void ArcTest::TearDown() {
    arc_state.collector = nullptr;
    arc_state.parser = nullptr;

    ScanTest::TearDown();
}

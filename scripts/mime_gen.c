// Generates mime_generated.c from mime.csv. Compiled and run at build time
// (see CMakeLists.txt). Mime ids are persisted in .sist2 index files: the id
// assignment scheme (counter in sorted-mime order + flag bits) must not change,
// and a mime added to the csv has to carry a '+' so that it is numbered after
// every mime that was there before it rather than in its sorted position.

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_MIMES 1024
#define MAX_EXTS_PER_MIME 64
#define MAX_EXTS 8192

typedef struct {
    char *name;
    char *clean;
    char *exts[MAX_EXTS_PER_MIME];
    int ext_count;
    int noparse;
    int added;
    char id[64];
} mime_t;

static mime_t mimes[MAX_MIMES];
static mime_t *sorted_mimes[MAX_MIMES];
static int mime_count;

// Index of the major type is the mime id's 0x000F0000 nibble (MAJOR_MIME in mime.h)
static const char *const MAJOR_MIMES[] = {
        "sist2", "model", "example", "message", "multipart", "font",
        "video", "audio", "image", "text", "application", "x-epoc"
};

// First matching category supplies the flag bit suffix (masks defined in mime.h).
// The font list omits application/x-ms-compress-szdd and application/x-font-sfn:
// the original generator never flagged them (a missing comma fused the two
// strings) and the flag bits are persisted in index files.
static const struct {
    const char *suffix;
    const char *const *members;
} CATEGORIES[] = {
        {" | 0x40000000", (const char *const []) {  // pdf
                "application/pdf", "application/epub+zip", "application/vnd.ms-xpsdocument", NULL}},
        {" | 0x20000000", (const char *const []) {  // font
                "application/vnd.ms-opentype", "application/x-font-ttf",
                "font/otf", "font/sfnt", "font/woff", "font/woff2", NULL}},
        {" | 0x10000000", (const char *const []) {  // archive format
                "application/x-tar", "application/zip", "application/x-rar", "application/x-arc",
                "application/x-warc", "application/x-7z-compressed", NULL}},
        {" | 0x08000000", (const char *const []) {  // archive filter
                "application/gzip", "application/x-bzip2", "application/x-xz", "application/x-zstd",
                "application/x-lzma", "application/x-lz4", "application/x-lzip", "application/x-lzop", NULL}},
        {" | 0x04000000", (const char *const []) {  // ooxml document
                "application/vnd.openxmlformats-officedocument.wordprocessingml.document",
                "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet",
                "application/vnd.openxmlformats-officedocument.presentationml.presentation", NULL}},
        {" | 0x02000000", (const char *const []) {  // mobi
                "application/x-mobipocket-ebook", "application/vnd.amazon.mobi8-ebook", NULL}},
        {" | 0x01000000", (const char *const []) {  // markup
                "text/xml", "text/html", "text/x-sgml", NULL}},
        {" | 0x00800000", (const char *const []) {  // camera raw
                "image/x-olympus-orf", "image/x-nikon-nef", "image/x-fuji-raf", "image/x-panasonic-raw",
                "image/x-adobe-dng", "image/x-canon-cr2", "image/x-canon-crw", "image/x-dcraw",
                "image/x-kodak-dcr", "image/x-kodak-k25", "image/x-kodak-kdc", "image/x-minolta-mrw",
                "image/x-pentax-pef", "image/x-sigma-x3f", "image/x-sony-arw", "image/x-sony-sr2",
                "image/x-sony-srf", "image/x-epson-erf", NULL}},
};

static void die(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    fprintf(stderr, "mime_gen: ");
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");
    va_end(args);
    exit(1);
}

// Same polynomial and pre/post-conditioning as zlib's crc32(), which the
// runtime lookup side uses
static unsigned long crc32_str(const char *s) {
    unsigned long crc = 0xFFFFFFFFUL;
    for (; *s; s++) {
        crc ^= (unsigned char) *s;
        for (int bit = 0; bit < 8; bit++) {
            crc = (crc >> 1) ^ (0xEDB88320UL & -(crc & 1UL));
        }
    }
    return crc ^ 0xFFFFFFFFUL;
}

static char *clean_name(const char *mime) {
    char *out = strdup(mime);
    for (char *c = out; *c; c++) {
        if (strchr("/.+-", *c) != NULL) {
            *c = '_';
        }
    }
    return out;
}

static void parse_csv(const char *path) {
    FILE *f = fopen(path, "r");
    if (f == NULL) {
        die("cannot open %s", path);
    }

    char line[4096];
    while (fgets(line, sizeof(line), f) != NULL) {
        line[strcspn(line, "\r\n")] = '\0';
        if (line[0] == '\0') {
            continue;
        }
        if (mime_count == MAX_MIMES) {
            die("too many mimes");
        }

        mime_t *m = &mimes[mime_count++];
        m->added = line[0] == '+';
        m->noparse = line[m->added] == '!';

        char *name = line + m->added + m->noparse;
        char *comma = strchr(name, ',');
        if (comma == NULL) {
            die("missing comma: %s", line);
        }
        *comma = '\0';
        m->name = strdup(name);
        m->clean = clean_name(name);

        for (char *ext = strtok(comma + 1, "|"); ext != NULL; ext = strtok(NULL, "|")) {
            while (*ext == ' ') {
                ext++;
            }
            char *end = ext + strlen(ext);
            while (end > ext && end[-1] == ' ') {
                *--end = '\0';
            }
            if (*ext == '\0') {
                continue;
            }
            if (m->ext_count == MAX_EXTS_PER_MIME) {
                die("too many extensions: %s", m->name);
            }
            m->exts[m->ext_count++] = strdup(ext);
        }
    }
    fclose(f);
}

static void check_unique(unsigned long crc, unsigned long *seen, int *seen_count, const char *what) {
    for (int i = 0; i < *seen_count; i++) {
        if (seen[i] == crc) {
            die("CRC32 collision: %s", what);
        }
    }
    seen[(*seen_count)++] = crc;
}

static void check_collisions(void) {
    static unsigned long seen[MAX_EXTS];
    int seen_count = 0;

    for (int i = 0; i < mime_count; i++) {
        for (int j = 0; j < mimes[i].ext_count; j++) {
            check_unique(crc32_str(mimes[i].exts[j]), seen, &seen_count, mimes[i].exts[j]);
        }
    }

    seen_count = 0;
    for (int i = 0; i < mime_count; i++) {
        check_unique(crc32_str(mimes[i].name), seen, &seen_count, mimes[i].name);
    }
}

static int major_id(const char *mime) {
    for (int i = 0; i < (int) (sizeof(MAJOR_MIMES) / sizeof(*MAJOR_MIMES)); i++) {
        size_t len = strlen(MAJOR_MIMES[i]);
        if (strncmp(mime, MAJOR_MIMES[i], len) == 0 && mime[len] == '/') {
            return i;
        }
    }
    die("unknown major mime type: %s", mime);
    return -1;
}

static const char *category_suffix(const char *mime) {
    for (int i = 0; i < (int) (sizeof(CATEGORIES) / sizeof(*CATEGORIES)); i++) {
        for (const char *const *member = CATEGORIES[i].members; *member != NULL; member++) {
            if (strcmp(mime, *member) == 0) {
                return CATEGORIES[i].suffix;
            }
        }
    }
    return NULL;
}

static int cmp_mime_name(const void *a, const void *b) {
    return strcmp((*(mime_t *const *) a)->name, (*(mime_t *const *) b)->name);
}

static void assign_id(mime_t *m, int *cnt) {
    const char *suffix = m->noparse ? " | 0x80000000" : category_suffix(m->name);

    if (suffix == NULL && strcmp(m->name, "application/x-empty") == 0) {
        strcpy(m->id, "1");  // MIME_EMPTY
        return;
    }
    snprintf(m->id, sizeof(m->id), "%d%s",
             (major_id(m->name) << 16) + (*cnt)++, suffix == NULL ? "" : suffix);
}

static void assign_ids(void) {
    for (int i = 0; i < mime_count; i++) {
        sorted_mimes[i] = &mimes[i];
    }
    qsort(sorted_mimes, mime_count, sizeof(*sorted_mimes), cmp_mime_name);

    int cnt = 1;
    for (int i = 0; i < mime_count; i++) {
        if (!sorted_mimes[i]->added) {
            assign_id(sorted_mimes[i], &cnt);
        }
    }

    // A mime numbered in name order would renumber every mime sorted after it, so the ones
    // marked with '+' are numbered after all of those instead, in the order the csv lists them
    for (int i = 0; i < mime_count; i++) {
        if (mimes[i].added) {
            assign_id(&mimes[i], &cnt);
        }
    }
}

static void emit(FILE *o) {
    fprintf(o, "// **Generated by mime_gen.c**\n"
               "#ifndef MIME_GENERATED_C\n"
               "#define MIME_GENERATED_C\n"
               "#include <stdlib.h>\n\n");

    // #defines rather than an enum: ids with the no-parse bit (0x80000000) set
    // do not fit in an int, which ISO C requires for enumerator values
    for (int i = 0; i < mime_count; i++) {
        fprintf(o, "#define %s (%s)\n", sorted_mimes[i]->clean, sorted_mimes[i]->id);
    }

    fprintf(o, "char *mime_get_mime_text(unsigned int mime_id) {switch (mime_id) {\n");
    for (int i = 0; i < mime_count; i++) {
        fprintf(o, "case %s: return \"%s\";\n", mimes[i].clean, mimes[i].name);
    }
    fprintf(o, "default: return NULL;}}\n");

    fprintf(o, "unsigned int mime_extension_lookup(unsigned long extension_crc32) {switch (extension_crc32) {\n");
    for (int i = 0; i < mime_count; i++) {
        for (int j = 0; j < mimes[i].ext_count; j++) {
            fprintf(o, "case %lu:", crc32_str(mimes[i].exts[j]));
        }
        if (mimes[i].ext_count > 0) {
            fprintf(o, "return %s;\n", mimes[i].clean);
        }
    }
    fprintf(o, "default: return 0;}}\n");

    fprintf(o, "unsigned int mime_name_lookup(unsigned long mime_crc32) {switch (mime_crc32) {\n");
    for (int i = 0; i < mime_count; i++) {
        fprintf(o, "case %lu: return %s;\n", crc32_str(mimes[i].name), mimes[i].clean);
    }
    fprintf(o, "default: return 0;}}\n");

    fprintf(o, "unsigned int mime_ids[] = {");
    for (int i = 0; i < mime_count; i++) {
        fprintf(o, "%s,", mimes[i].id);
    }
    fprintf(o, "0};\n");
    fprintf(o, "unsigned int* get_mime_ids() { return mime_ids; }\n");
    fprintf(o, "#endif\n");
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        die("usage: mime_gen <mime.csv> <output.c>");
    }

    parse_csv(argv[1]);
    check_collisions();
    assign_ids();

    char tmp[4096];
    snprintf(tmp, sizeof(tmp), "%s.tmp", argv[2]);
    FILE *o = fopen(tmp, "w");
    if (o == NULL) {
        die("cannot write %s", tmp);
    }
    emit(o);
    fclose(o);

    if (rename(tmp, argv[2]) != 0) {
        die("cannot rename %s to %s", tmp, argv[2]);
    }
    return 0;
}

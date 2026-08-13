#include "magic_util.h"
#include "src/log.h"
#include "mime.h"
#include <magic.h>
#include <pthread.h>
#include "src/embed.h"

EMBED_FILE(magic_database_buffer, MAGIC_MGC_PATH);

// Loading the database compiles every regex it contains, which costs far more than the
// mime lookup. A magic_t is not thread safe, so each thread keeps its own.
static pthread_key_t MagicKey;
static pthread_once_t MagicKeyOnce = PTHREAD_ONCE_INIT;

static void magic_destroy(void *magic) {
    if (magic != NULL) {
        magic_close((magic_t) magic);
    }
}

static void magic_key_init() {
    pthread_key_create(&MagicKey, magic_destroy);
}

static magic_t thread_magic() {
    pthread_once(&MagicKeyOnce, magic_key_init);

    magic_t magic = pthread_getspecific(MagicKey);
    if (magic != NULL) {
        return magic;
    }

    magic = magic_open(MAGIC_MIME_TYPE);

    const char *magic_buffers[1] = {magic_database_buffer,};
    size_t sizes[1] = {magic_database_buffer_size,};

    int load_ret = magic_load_buffers(magic, (void **) &magic_buffers, sizes, 1);

    if (load_ret != 0) {
        LOG_FATALF("parse.c", "Could not load libmagic database: (%d)", load_ret);
    }

    pthread_setspecific(MagicKey, magic);
    return magic;
}

char *magic_buffer_embedded(void *buffer, size_t buffer_size) {

    const char *magic_mime_str = magic_buffer(thread_magic(), buffer, buffer_size);
    char *return_value = NULL;

    if (magic_mime_str != NULL) {
        return_value = malloc(strlen(magic_mime_str) + 1);
        strcpy(return_value, magic_mime_str);
    }

    return return_value;
}

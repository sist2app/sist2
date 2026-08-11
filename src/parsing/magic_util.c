#include "magic_util.h"
#include "src/log.h"
#include "mime.h"
#include <magic.h>
#include "src/embed.h"

EMBED_FILE(magic_database_buffer, MAGIC_MGC_PATH);


char *magic_buffer_embedded(void *buffer, size_t buffer_size) {

    magic_t magic = magic_open(MAGIC_MIME_TYPE);

    const char *magic_buffers[1] = {magic_database_buffer,};
    size_t sizes[1] = {magic_database_buffer_size,};

    // TODO optimisation: check if we can reuse the magic instance
    int load_ret = magic_load_buffers(magic, (void **) &magic_buffers, sizes, 1);

    if (load_ret != 0) {
        LOG_FATALF("parse.c", "Could not load libmagic database: (%d)", load_ret);
    }

    const char *magic_mime_str = magic_buffer(magic, buffer, buffer_size);
    char *return_value = NULL;

    if (magic_mime_str != NULL) {
        return_value = malloc(strlen(magic_mime_str) + 1);
        strcpy(return_value, magic_mime_str);
    }

    magic_close(magic);
    return return_value;
}
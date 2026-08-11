#include "mime.h"
#include <zlib.h>

unsigned int mime_get_mime_by_ext(const char *ext) {
    unsigned char lower[16];
    unsigned char *p = lower;
    int cnt = 0;
    while ((*ext) != '\0' && cnt + 1 < (int) sizeof(lower)) {
        *p++ = tolower(*ext++);
        cnt++;
    }
    *p = '\0';

    unsigned long crc = crc32(0, lower, cnt);

    unsigned int mime = mime_extension_lookup(crc);
    return mime;
}

unsigned int mime_get_mime_by_string(const char *str) {

    const char *ptr = str;
    while (*ptr == ' ' || *ptr == '[') {
        ptr++;
    }

    unsigned long crc = crc32(0, (unsigned char *) ptr, strlen(ptr));

    return mime_name_lookup(crc);
}

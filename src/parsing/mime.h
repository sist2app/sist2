#ifndef SIST2_MIME_H
#define SIST2_MIME_H

#include "../sist.h"

#define MAJOR_MIME(mime_id) (mime_id & 0x000F0000) >> 16

#define MIME_EMPTY 1
#define MIME_SIST2_SIDECAR 2

#define DONT_PARSE 0x80000000
#define SHOULD_PARSE(mime_id) (ScanCtx.fast == 0 && (mime_id & DONT_PARSE) != DONT_PARSE && mime_id != 0)

#define PDF_MASK 0x40000000
#define IS_PDF(mime_id) (mime_id & PDF_MASK) == PDF_MASK

#define FONT_MASK 0x20000000
#define IS_FONT(mime_id) (mime_id & FONT_MASK) == FONT_MASK

#define ARC_MASK 0x10000000
#define IS_ARC(mime_id) (mime_id & ARC_MASK) == ARC_MASK

#define ARC_FILTER_MASK 0x08000000
#define IS_ARC_FILTER(mime_id) (mime_id & ARC_FILTER_MASK) == ARC_FILTER_MASK

#define DOC_MASK 0x04000000
#define IS_DOC(mime_id) (mime_id & DOC_MASK) == DOC_MASK

#define MOBI_MASK 0x02000000
#define IS_MOBI(mime_id) (mime_id & MOBI_MASK) == MOBI_MASK

#define MARKUP_MASK 0x01000000
#define IS_MARKUP(mime_id) (mime_id & MARKUP_MASK) == MARKUP_MASK

#define RAW_MASK 0x00800000
#define IS_RAW(mime_id) (mime_id & RAW_MASK) == RAW_MASK

enum major_mime {
    MimeInvalid = 0,
    MimeModel = 1,
    MimeExample = 2,
    MimeMessage = 3,
    MimeMultipart = 4,
    MimeFont = 5,
    MimeVideo = 6,
    MimeAudio = 7,
    MimeImage = 8,
    MimeText = 9,
    MimeApplication = 10,
};

unsigned int mime_name_lookup(unsigned long mime_crc32);

unsigned int mime_extension_lookup(unsigned long extension_crc32);

const char *mime_get_mime_text(unsigned int);

unsigned int mime_get_mime_by_ext(const char *ext);

unsigned int mime_get_mime_by_string(const char *str);

unsigned int* get_mime_ids();

#endif

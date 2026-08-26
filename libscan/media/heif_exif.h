#ifndef SIST2_HEIF_EXIF_H
#define SIST2_HEIF_EXIF_H

#include "media.h"

/**
 * EXIF tags of a HEIF picture, keyed like the frame metadata of a JPEG. Returns NULL when the
 * file is not HEIF, carries no EXIF item, or the item cannot be read.
 */
AVDictionary *heif_exif_metadata(scan_media_ctx_t *ctx, AVFormatContext *pFormatCtx, document_t *doc);

#endif

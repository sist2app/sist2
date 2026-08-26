#include "heif_exif.h"

#include <inttypes.h>
#include <libavutil/intreadwrite.h>

#define META_BOX MKBETAG('m', 'e', 't', 'a')
#define IINF_BOX MKBETAG('i', 'i', 'n', 'f')
#define INFE_BOX MKBETAG('i', 'n', 'f', 'e')
#define ILOC_BOX MKBETAG('i', 'l', 'o', 'c')
#define IDAT_BOX MKBETAG('i', 'd', 'a', 't')
#define EXIF_ITEM MKBETAG('E', 'x', 'i', 'f')

#define CONSTRUCTION_FILE 0
#define CONSTRUCTION_IDAT 1

/** An APP1 segment is at most 65535 bytes, including its own length and the "Exif\0\0" header */
#define MAX_TIFF_SIZE (65535 - 2 - 6)
#define EXIF_HEADER_SIZE 6

/** Just large enough for the encoder, and the picture the EXIF segment is carried in */
#define BLANK_JPEG_SIZE 8

typedef struct {
    uint32_t type;
    int64_t start;
    int64_t end;
} box_t;

typedef struct {
    int64_t offset;
    int64_t length;
    int construction_method;
} extent_t;

static int is_heif(const AVFormatContext *pFormatCtx) {

    static const char *const brands[] = {"mif1", "msf1", "heic", "heix", "hevc", "hevx", "avif", "avis"};

    const AVDictionaryEntry *major = av_dict_get(pFormatCtx->metadata, "major_brand", NULL, 0);
    const AVDictionaryEntry *compatible = av_dict_get(pFormatCtx->metadata, "compatible_brands", NULL, 0);

    for (int i = 0; i < (int) (sizeof(brands) / sizeof(brands[0])); i++) {
        if (major != NULL && strncmp(major->value, brands[i], 4) == 0) {
            return TRUE;
        }
        if (compatible != NULL && strstr(compatible->value, brands[i]) != NULL) {
            return TRUE;
        }
    }

    return FALSE;
}

static int read_box(AVIOContext *pb, int64_t limit, box_t *box) {

    int64_t offset = avio_tell(pb);
    if (offset < 0 || offset + 8 > limit) {
        return FALSE;
    }

    int64_t size = avio_rb32(pb);
    uint32_t type = avio_rb32(pb);
    int64_t header_size = 8;

    if (size == 1) {
        if (offset + 16 > limit) {
            return FALSE;
        }
        size = (int64_t) avio_rb64(pb);
        header_size = 16;
    } else if (size == 0) {
        size = limit - offset;
    }

    if (size < header_size || size > limit - offset || avio_feof(pb)) {
        return FALSE;
    }

    box->type = type;
    box->start = offset + header_size;
    box->end = offset + size;

    return TRUE;
}

static int find_box(AVIOContext *pb, int64_t start, int64_t end, uint32_t type, box_t *box) {

    if (avio_seek(pb, start, SEEK_SET) < 0) {
        return FALSE;
    }

    while (read_box(pb, end, box)) {
        if (box->type == type) {
            return TRUE;
        }
        if (avio_seek(pb, box->end, SEEK_SET) < 0) {
            break;
        }
    }

    return FALSE;
}

/** Item identifiers start at 1, so 0 means that the file has no EXIF item */
static uint32_t find_exif_item_id(AVIOContext *pb, const box_t *iinf) {

    if (avio_seek(pb, iinf->start, SEEK_SET) < 0) {
        return 0;
    }

    int version = avio_r8(pb);
    avio_skip(pb, 3);
    avio_skip(pb, version == 0 ? 2 : 4);

    box_t infe;
    while (read_box(pb, iinf->end, &infe)) {
        int64_t next = infe.end;

        if (infe.type == INFE_BOX) {
            int infe_version = avio_r8(pb);
            avio_skip(pb, 3);

            // The item type is only written from version 2 on
            if (infe_version >= 2) {
                uint32_t item_id = infe_version == 2 ? avio_rb16(pb) : avio_rb32(pb);
                avio_skip(pb, 2);

                if (avio_rb32(pb) == EXIF_ITEM) {
                    return item_id;
                }
            }
        }

        if (avio_seek(pb, next, SEEK_SET) < 0) {
            break;
        }
    }

    return 0;
}

static int64_t read_uint(AVIOContext *pb, int size) {

    switch (size) {
        case 0:
            return 0;
        case 4:
            return avio_rb32(pb);
        case 8:
            return (int64_t) avio_rb64(pb);
        default:
            return -1;
    }
}

static int find_item_extent(AVIOContext *pb, const box_t *iloc, uint32_t item_id, extent_t *extent) {

    if (avio_seek(pb, iloc->start, SEEK_SET) < 0) {
        return FALSE;
    }

    int version = avio_r8(pb);
    avio_skip(pb, 3);

    int byte = avio_r8(pb);
    int offset_size = byte >> 4;
    int length_size = byte & 0xf;

    byte = avio_r8(pb);
    int base_offset_size = byte >> 4;
    int index_size = version == 1 || version == 2 ? byte & 0xf : 0;

    int64_t item_count = version < 2 ? avio_rb16(pb) : avio_rb32(pb);

    for (int64_t i = 0; i < item_count; i++) {
        if (avio_feof(pb) || avio_tell(pb) >= iloc->end) {
            return FALSE;
        }

        uint32_t id = version < 2 ? avio_rb16(pb) : avio_rb32(pb);
        int construction_method = version == 0 ? CONSTRUCTION_FILE : avio_rb16(pb) & 0xf;
        avio_skip(pb, 2);

        int64_t base_offset = read_uint(pb, base_offset_size);
        int64_t extent_count = avio_rb16(pb);

        if (base_offset < 0 || avio_feof(pb)) {
            return FALSE;
        }

        for (int64_t j = 0; j < extent_count; j++) {
            if (read_uint(pb, index_size) < 0) {
                return FALSE;
            }

            int64_t offset = read_uint(pb, offset_size);
            int64_t length = read_uint(pb, length_size);

            if (offset < 0 || length < 0) {
                return FALSE;
            }

            // An item spread over several extents is not worth reassembling for its EXIF
            if (id == item_id && extent_count == 1 && length > 0) {
                extent->offset = base_offset + offset;
                extent->length = length;
                extent->construction_method = construction_method;
                return TRUE;
            }
        }
    }

    return FALSE;
}

static AVPacket *encode_blank_jpeg(void) {

    AVCodecContext *encoder = alloc_jpeg_encoder(BLANK_JPEG_SIZE, BLANK_JPEG_SIZE, 1);
    if (encoder == NULL) {
        return NULL;
    }

    AVFrame *frame = av_frame_alloc();
    frame->format = encoder->pix_fmt;
    frame->width = BLANK_JPEG_SIZE;
    frame->height = BLANK_JPEG_SIZE;

    AVPacket *packet = NULL;

    if (av_frame_get_buffer(frame, 0) == 0) {
        const ptrdiff_t linesize[4] = {
                frame->linesize[0], frame->linesize[1], frame->linesize[2], frame->linesize[3]
        };
        av_image_fill_black(frame->data, linesize, frame->format, AVCOL_RANGE_JPEG,
                            frame->width, frame->height);

        avcodec_send_frame(encoder, frame);
        avcodec_send_frame(encoder, NULL);

        packet = av_packet_alloc();
        if (avcodec_receive_packet(encoder, packet) != 0 || packet->size < 2) {
            av_packet_free(&packet);
        }
    }

    av_frame_free(&frame);
    avcodec_free_context(&encoder);

    return packet;
}

/**
 * ffmpeg parses EXIF as part of decoding a picture that carries it, so the tags of a HEIF item are
 * read by handing its bytes to the JPEG decoder as the APP1 segment of a blank picture.
 */
static AVDictionary *decode_exif(const uint8_t *tiff, int tiff_len) {

    AVPacket *jpeg = encode_blank_jpeg();
    if (jpeg == NULL) {
        return NULL;
    }

    int segment_size = 2 + EXIF_HEADER_SIZE + tiff_len;
    int size = 2 + 2 + segment_size + (jpeg->size - 2);

    uint8_t *buf = av_malloc(size + AV_INPUT_BUFFER_PADDING_SIZE);
    uint8_t *ptr = buf;

    memcpy(ptr, jpeg->data, 2);
    ptr += 2;
    *ptr++ = 0xFF;
    *ptr++ = 0xE1;
    *ptr++ = (uint8_t) (segment_size >> 8);
    *ptr++ = (uint8_t) (segment_size & 0xFF);
    memcpy(ptr, "Exif\0\0", EXIF_HEADER_SIZE);
    ptr += EXIF_HEADER_SIZE;
    memcpy(ptr, tiff, tiff_len);
    ptr += tiff_len;
    memcpy(ptr, jpeg->data + 2, jpeg->size - 2);
    memset(buf + size, 0, AV_INPUT_BUFFER_PADDING_SIZE);

    av_packet_free(&jpeg);

    const AVCodec *codec = avcodec_find_decoder(AV_CODEC_ID_MJPEG);
    AVCodecContext *decoder = avcodec_alloc_context3(codec);
    decoder->thread_count = 1;

    AVDictionary *metadata = NULL;

    if (avcodec_open2(decoder, codec, NULL) == 0) {
        AVPacket *packet = av_packet_alloc();
        packet->data = buf;
        packet->size = size;

        AVFrame *frame = av_frame_alloc();

        if (avcodec_send_packet(decoder, packet) == 0 && avcodec_receive_frame(decoder, frame) == 0) {
            av_dict_copy(&metadata, frame->metadata, 0);
        }

        av_frame_free(&frame);
        av_packet_free(&packet);
    }

    avcodec_free_context(&decoder);
    av_free(buf);

    return metadata;
}

AVDictionary *heif_exif_metadata(scan_media_ctx_t *ctx, AVFormatContext *pFormatCtx, document_t *doc) {

    AVIOContext *pb = pFormatCtx->pb;

    if (pb == NULL || (pb->seekable & AVIO_SEEKABLE_NORMAL) == 0 || !is_heif(pFormatCtx)) {
        return NULL;
    }

    int64_t file_size = avio_size(pb);
    if (file_size <= 0) {
        return NULL;
    }

    box_t meta;
    if (!find_box(pb, 0, file_size, META_BOX, &meta)) {
        return NULL;
    }

    // meta is a FullBox
    meta.start += 4;
    if (meta.start > meta.end) {
        return NULL;
    }

    box_t iinf;
    if (!find_box(pb, meta.start, meta.end, IINF_BOX, &iinf)) {
        return NULL;
    }

    uint32_t item_id = find_exif_item_id(pb, &iinf);
    if (item_id == 0) {
        return NULL;
    }

    box_t iloc;
    extent_t extent;
    if (!find_box(pb, meta.start, meta.end, ILOC_BOX, &iloc)
        || !find_item_extent(pb, &iloc, item_id, &extent)) {
        return NULL;
    }

    int64_t base = 0;
    int64_t limit = file_size;

    if (extent.construction_method == CONSTRUCTION_IDAT) {
        box_t idat;
        if (!find_box(pb, meta.start, meta.end, IDAT_BOX, &idat)) {
            return NULL;
        }
        base = idat.start;
        limit = idat.end;
    } else if (extent.construction_method != CONSTRUCTION_FILE) {
        return NULL;
    }

    // The payload is a four-byte offset to the TIFF header, then the header itself
    if (extent.length < 5 || base + extent.offset + extent.length > limit) {
        return NULL;
    }

    if (extent.length - 4 > MAX_TIFF_SIZE) {
        CTX_LOG_DEBUGF(doc->filepath, "(heif_exif.c) EXIF item is too large to read: %" PRId64 " bytes", extent.length);
        return NULL;
    }

    uint8_t *payload = malloc(extent.length);

    if (avio_seek(pb, base + extent.offset, SEEK_SET) < 0
        || avio_read(pb, payload, (int) extent.length) != (int) extent.length) {
        free(payload);
        return NULL;
    }

    int64_t tiff_offset = 4 + AV_RB32(payload);
    AVDictionary *metadata = NULL;

    if (tiff_offset < extent.length) {
        metadata = decode_exif(payload + tiff_offset, (int) (extent.length - tiff_offset));
    }

    free(payload);

    return metadata;
}

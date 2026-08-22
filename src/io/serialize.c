#include "src/ctx.h"
#include "serialize.h"
#include "src/parsing/mime.h"
#include "src/worker/sink.h"


char *get_meta_key_text(enum metakey meta_key) {

    switch (meta_key) {
        case MetaContent:
            return "content";
        case MetaWidth:
            return "width";
        case MetaHeight:
            return "height";
        case MetaMediaDuration:
            return "duration";
        case MetaMediaAudioCodec:
            return "audioc";
        case MetaMediaVideoCodec:
            return "videoc";
        case MetaMediaBitrate:
            return "bitrate";
        case MetaArtist:
            return "artist";
        case MetaAlbum:
            return "album";
        case MetaAlbumArtist:
            return "album_artist";
        case MetaGenre:
            return "genre";
        case MetaTitle:
            return "title";
        case MetaMediaComment:
            return "media_comment";
        case MetaFontName:
            return "font_name";
        case MetaExifMake:
            return "exif_make";
        case MetaExifDescription:
            return "exif_description";
        case MetaExifSoftware:
            return "exif_software";
        case MetaExifExposureTime:
            return "exif_exposure_time";
        case MetaExifFNumber:
            return "exif_fnumber";
        case MetaExifFocalLength:
            return "exif_focal_length";
        case MetaExifUserComment:
            return "exif_user_comment";
        case MetaExifIsoSpeedRatings:
            return "exif_iso_speed_ratings";
        case MetaExifModel:
            return "exif_model";
        case MetaExifDateTime:
            return "exif_datetime";
        case MetaAuthor:
            return "author";
        case MetaModifiedBy:
            return "modified_by";
        case MetaPages:
            return "pages";
        case MetaExifGpsLongitudeRef:
            return "exif_gps_longitude_ref";
        case MetaExifGpsLongitudeDMS:
            return "exif_gps_longitude_dms";
        case MetaExifGpsLongitudeDec:
            return "exif_gps_longitude_dec";
        case MetaExifGpsLatitudeRef:
            return "exif_gps_latitude_ref";
        case MetaExifGpsLatitudeDMS:
            return "exif_gps_latitude_dms";
        case MetaExifGpsLatitudeDec:
            return "exif_gps_latitude_dec";
        case MetaChecksum:
            return "checksum";
        default:
        LOG_FATALF("serialize.c", "FIXME: Unknown meta key: %d", meta_key);
    }
}

/**
 * A parser can produce the same field twice - text from more than one place in a media file, say -
 * and a JSON object with the field twice is rejected outright by Elasticsearch. Text is worth
 * keeping, so it is appended; anything else keeps what was found first.
 */
static void add_string_meta(cJSON *json, meta_line_t *meta) {

    const char *key = get_meta_key_text(meta->key);
    cJSON *existing = cJSON_GetObjectItem(json, key);

    if (existing == NULL) {
        cJSON_AddStringToObject(json, key, meta->str_val);
        return;
    }

    if (meta->key != MetaContent) {
        return;
    }

    const char *text = cJSON_GetStringValue(existing);
    char *merged = malloc(strlen(text) + strlen(meta->str_val) + 2);
    sprintf(merged, "%s\n%s", text, meta->str_val);

    cJSON_ReplaceItemInObject(json, key, cJSON_CreateString(merged));
    free(merged);
}

typedef struct {
    meta_line_t *meta_head;
    meta_line_t *meta_tail;
} linked_list_t;


void write_document(document_t *doc) {
    linked_list_t thumbnails_to_write = {.meta_head = NULL, .meta_tail = NULL};

    cJSON *json = cJSON_CreateObject();
    int buffer_size_guess = 8192;

    // Ignore root directory in the file path
    doc->ext = (short) (doc->ext - ScanCtx.index.desc.root_len);
    doc->base = (short) (doc->base - ScanCtx.index.desc.root_len);
    char filepath[PATH_MAX * 3];
    strcpy(filepath, doc->filepath + ScanCtx.index.desc.root_len);

    // Sized before the truncations below, so it covers both the name and the path
    const size_t filepath_escaped_size = str_escape_size(filepath);
    char *filepath_escaped = malloc(filepath_escaped_size);

    cJSON_AddStringToObject(json, "extension", filepath + doc->ext);

    // Remove extension
    if (*(filepath + doc->ext - 1) == '.') {
        *(filepath + doc->ext - 1) = '\0';
    } else {
        *(filepath + doc->ext) = '\0';
    }

    str_escape(filepath_escaped, filepath_escaped_size, filepath + doc->base);

    cJSON_AddStringToObject(json, "name", filepath_escaped);

    if (doc->base > 0) {
        *(filepath + doc->base - 1) = '\0';

        str_escape(filepath_escaped, filepath_escaped_size, filepath);
        cJSON_AddStringToObject(json, "path", filepath_escaped);
    } else {
        cJSON_AddStringToObject(json, "path", "");
    }

    free(filepath_escaped);

    // Metadata
    meta_line_t *meta = doc->meta_head;
    while (meta != NULL) {

        switch (meta->key) {
            case MetaPages:
            case MetaWidth:
            case MetaHeight:
            case MetaMediaDuration:
            case MetaMediaBitrate: {
                cJSON_AddNumberToObject(json, get_meta_key_text(meta->key), (double) meta->long_val);
                buffer_size_guess += 20;
                break;
            }
            case MetaMediaAudioCodec:
            case MetaMediaVideoCodec:
            case MetaContent:
            case MetaArtist:
            case MetaAlbum:
            case MetaAlbumArtist:
            case MetaGenre:
            case MetaFontName:
            case MetaExifMake:
            case MetaExifDescription:
            case MetaExifSoftware:
            case MetaExifExposureTime:
            case MetaExifFNumber:
            case MetaExifFocalLength:
            case MetaExifUserComment:
            case MetaExifIsoSpeedRatings:
            case MetaExifDateTime:
            case MetaExifModel:
            case MetaAuthor:
            case MetaModifiedBy:
            case MetaExifGpsLongitudeDMS:
            case MetaExifGpsLongitudeDec:
            case MetaExifGpsLongitudeRef:
            case MetaExifGpsLatitudeDMS:
            case MetaExifGpsLatitudeDec:
            case MetaExifGpsLatitudeRef:
            case MetaChecksum:
            case MetaMediaComment:
            case MetaTitle: {
                add_string_meta(json, meta);
                buffer_size_guess += (int) strlen(meta->str_val);
                break;
            }
            case MetaThumbnail: {
                // Keep a list of thumbnails to write after we know what the sid is
                APPEND_THUMBNAIL(&thumbnails_to_write, meta->str_val, meta->size);
                break;
            }
            default:
            LOG_FATALF("serialize.c", "Invalid meta key: %x %s", meta->key, get_meta_key_text(meta->key));
        }

        meta_line_t *tmp = meta;
        meta = meta->next;
        free(tmp);
    }

    char *json_str = cJSON_PrintBuffered(json, buffer_size_guess, FALSE);
    cJSON_Delete(json);

    sink_write_document(doc, json_str);
    free(doc);
    free(json_str);

    // Write thumbnails
    meta = thumbnails_to_write.meta_head;
    int index_num = 0;
    while (meta != NULL) {
        sink_write_thumbnail(index_num, meta->str_val, meta->size);

        meta_line_t *tmp = meta;
        meta = meta->next;
        free(tmp);
        index_num += 1;
    }
}
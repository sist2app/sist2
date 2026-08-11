#ifndef SIST2_LIBWPD_C_API_H
#define SIST2_LIBWPD_C_API_H

#include "stdlib.h"

#ifdef __cplusplus
#define EXTERNC extern "C"
#else
#define EXTERNC
#endif

#ifdef __cplusplus
extern "C" {
#endif
#include "../scan.h"
#include "../util.h"
#ifdef __cplusplus
};
#endif


typedef void *wpd_stream_t;

typedef enum {
    C_WPD_CONFIDENCE_NONE = 0,
    C_WPD_CONFIDENCE_UNSUPPORTED_ENCRYPTION,
    C_WPD_CONFIDENCE_SUPPORTED_ENCRYPTION,
    C_WPD_CONFIDENCE_EXCELLENT
} wpd_confidence_t;

typedef enum {
    C_WPD_OK,
    C_WPD_FILE_ACCESS_ERROR,
    C_WPD_PARSE_ERROR,
    C_WPD_UNSUPPORTED_ENCRYPTION_ERROR,
    C_WPD_PASSWORD_MISSMATCH_ERROR,
    C_WPD_OLE_ERROR,
    C_WPD_UNKNOWN_ERROR
} wpd_result_t;


EXTERNC wpd_confidence_t wpd_is_file_format_supported(wpd_stream_t stream);

EXTERNC wpd_stream_t wpd_memory_stream_create(const unsigned char *buf, size_t buf_len);

EXTERNC void wpd_memory_stream_destroy(wpd_stream_t stream);

EXTERNC wpd_result_t wpd_parse(wpd_stream_t ptr, text_buffer_t *tex, document_t *doc);

#endif

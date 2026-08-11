// glibc defines __always_inline in sys/cdefs.h; musl does not
#ifndef __always_inline
#define __always_inline inline __attribute__((__always_inline__))
#endif

#ifndef FALSE
#define FALSE (0)
#define BOOL int
#endif

#ifndef TRUE
#define TRUE (!FALSE)
#endif

#undef MAX
#define MAX(a, b)  (((a) > (b)) ? (a) : (b))

#undef MIN
#define MIN(a, b)  (((a) < (b)) ? (a) : (b))

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#undef ABS
#define ABS(a) (((a) < 0) ? -(a) : (a))

#define SHA1_DIGEST_LENGTH SHA_DIGEST_LENGTH

#define SHA1_STR_LENGTH (SHA1_DIGEST_LENGTH * 2 + 1)
#define MD5_STR_LENGTH (MD5_DIGEST_LENGTH * 2 + 1)

#define APPEND_STR_META(doc, keyname, value) do {\
    {meta_line_t *meta_str = malloc(sizeof(meta_line_t) + strlen(value)); \
    meta_str->key = keyname; \
    strcpy(meta_str->str_val, value); \
    APPEND_META(doc, meta_str);}} while(0)

#define APPEND_LONG_META(doc, keyname, value) do{\
    {meta_line_t *meta_long = malloc(sizeof(meta_line_t)); \
    meta_long->key = keyname; \
    meta_long->long_val = value; \
    APPEND_META(doc, meta_long);}} while(0)

#define APPEND_THUMBNAIL(doc, data, data_len) do{ \
    {meta_line_t *meta_tn = malloc(sizeof(meta_line_t) + (data_len)); \
    meta_tn->key = MetaThumbnail; \
    meta_tn->size = data_len; \
    memcpy(meta_tn->str_val, data, data_len); \
    APPEND_META(doc, meta_tn);}} while(0)


#define APPEND_META(doc, meta) do {\
    (meta)->next = NULL;\
    if ((doc)->meta_head == NULL) {\
        (doc)->meta_head = meta;\
        (doc)->meta_tail = (doc)->meta_head;\
    } else {\
        (doc)->meta_tail->next = meta;\
        (doc)->meta_tail = meta;\
    }}while(0)

#define APPEND_UTF8_META(doc, keyname, str) \
    text_buffer_t tex = text_buffer_create(-1); \
    text_buffer_append_string0(&tex, str); \
    text_buffer_terminate_string(&tex); \
    meta_line_t *meta_tag = malloc(sizeof(meta_line_t) + tex.dyn_buffer.cur); \
    meta_tag->key = keyname; \
    strcpy(meta_tag->str_val, tex.dyn_buffer.buf); \
    APPEND_META(doc, meta_tag); \
    text_buffer_destroy(&tex)

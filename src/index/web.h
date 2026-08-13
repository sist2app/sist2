#ifndef SIST2_WEB_H
#define SIST2_WEB_H

#include "src/sist.h"
#include <mongoose.h>
#include <curl/curl.h>

typedef struct response {
    char *body;
    size_t size;
    int status_code;
} response_t;

typedef struct {
    response_t *resp;
    int done;
} http_ev_data_t;

typedef struct {
    char* data;
    dyn_buffer_t response_buf;
    struct curl_slist *headers;
    CURL *handle;
    CURLM *multi;
    response_t *response;
    int running_handles;
    int done;
    char curl_err_buffer[CURL_ERROR_SIZE + 1];
} subreq_ctx_t;

response_t *web_get(const char *url, int timeout, int insecure);
response_t *web_post(const char * url, const char * data, int insecure);

/** Release the POST handle this thread has been reusing */
void web_thread_cleanup();
void web_post_async_poll(subreq_ctx_t* req);
subreq_ctx_t *web_post_async(const char *url, char *data, int insecure);
response_t *web_put(const char *url, const char *data, int insecure);
response_t *web_delete(const char *url, int insecure);

void free_response(response_t *resp);

#endif

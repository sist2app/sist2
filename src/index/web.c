#include "web.h"
#include "src/sist.h"
#include "src/ctx.h"

#include <mongoose.h>
#include <pthread.h>
#include <curl/curl.h>


size_t write_cb(char *ptr, size_t size, size_t nmemb, void *user_data) {

    size_t real_size = size * nmemb;
    dyn_buffer_t *buf = user_data;
    dyn_buffer_write(buf, ptr, real_size);
    return real_size;
}

void free_response(response_t *resp) {
    if (resp->body != NULL) {
        free(resp->body);
    }
    free(resp);
}

void web_post_async_poll(subreq_ctx_t *req) {
    fd_set fdread;
    fd_set fdwrite;
    fd_set fdexcep;
    int maxfd = -1;

    FD_ZERO(&fdread);
    FD_ZERO(&fdwrite);
    FD_ZERO(&fdexcep);

    CURLMcode mc = curl_multi_fdset(req->multi, &fdread, &fdwrite, &fdexcep, &maxfd);

    if (mc != CURLM_OK) {
        req->done = TRUE;
        return;
    }

    if (maxfd == -1) {
        // no fds ready yet
        return;
    }

    struct timeval timeout = {1, 0};
    int rc = select(maxfd + 1, &fdread, &fdwrite, &fdexcep, &timeout);

    switch (rc) {
        case -1:
            req->done = TRUE;
            break;
        case 0:
            break;
        default:
            curl_multi_perform(req->multi, &req->running_handles);
            break;
    }

    if (req->running_handles == 0) {
        req->done = TRUE;
        req->response->body = req->response_buf.buf;
        req->response->size = req->response_buf.cur;
        curl_easy_getinfo(req->handle, CURLINFO_RESPONSE_CODE, &req->response->status_code);

        if (req->response->status_code == 0) {
            LOG_ERRORF("web.c", "CURL Error: %s", req->curl_err_buffer);
        }

        curl_multi_cleanup(req->multi);
        curl_easy_cleanup(req->handle);
        curl_slist_free_all(req->headers);
        return;
    }
}

subreq_ctx_t *web_post_async(const char *url, char *data, int insecure) {
    subreq_ctx_t *req = calloc(1, sizeof(subreq_ctx_t));
    req->response = calloc(1, sizeof(response_t));
    req->data = data;
    req->response_buf = dyn_buffer_create();

    req->handle = curl_easy_init();
    CURL *curl = req->handle;
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *) (&req->response_buf));
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_POST, 1);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "sist2");
    if (insecure) {
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0);
    }

    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, req->curl_err_buffer);

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data);

    req->multi = curl_multi_init();
    curl_multi_add_handle(req->multi, curl);
    curl_multi_perform(req->multi, &req->running_handles);

    LOG_DEBUGF("web.c", "async request POST %s", url);

    return req;
}

response_t *web_get(const char *url, int timeout, int insecure) {
    response_t *resp = malloc(sizeof(response_t));

    CURL *curl;
    dyn_buffer_t buffer = dyn_buffer_create();

    curl = curl_easy_init();
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *) (&buffer));
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "sist2");
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout);
    if (insecure) {
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0);
    }

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    char err_buffer[CURL_ERROR_SIZE + 1] = {0};
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, err_buffer);

    curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &resp->status_code);

    if (resp->status_code == 0) {
        LOG_ERRORF("web.c", "CURL Error: %s", err_buffer);
    }

    curl_easy_cleanup(curl);
    curl_slist_free_all(headers);

    resp->body = buffer.buf;
    resp->size = buffer.cur;
    return resp;
}

// A bulk request per batch means thousands of them in a row against the same host: the handle is
// kept so that curl can hand the next one an already-open connection.
static __thread CURL *PostHandle = NULL;
static __thread struct curl_slist *PostHeaders = NULL;

void web_thread_cleanup() {
    if (PostHandle != NULL) {
        curl_easy_cleanup(PostHandle);
        curl_slist_free_all(PostHeaders);
        PostHandle = NULL;
        PostHeaders = NULL;
    }
}

response_t *web_post(const char *url, const char *data, int insecure) {

    response_t *resp = malloc(sizeof(response_t));

    dyn_buffer_t buffer = dyn_buffer_create();

    if (PostHandle == NULL) {
        PostHandle = curl_easy_init();
        PostHeaders = curl_slist_append(NULL, "Content-Type: application/json");

        curl_easy_setopt(PostHandle, CURLOPT_WRITEFUNCTION, write_cb);
        curl_easy_setopt(PostHandle, CURLOPT_POST, 1);
        curl_easy_setopt(PostHandle, CURLOPT_USERAGENT, "sist2");
        curl_easy_setopt(PostHandle, CURLOPT_HTTPHEADER, PostHeaders);
    }

    CURL *curl = PostHandle;

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *) (&buffer));
    if (insecure) {
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0);
    }

    char err_buffer[CURL_ERROR_SIZE + 1] = {0};
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, err_buffer);

    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE, (curl_off_t) strlen(data));
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data);

    curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &resp->status_code);

    resp->body = buffer.buf;
    resp->size = buffer.cur;

    if (resp->status_code == 0) {
        LOG_ERRORF("web.c", "CURL Error: %s", err_buffer);
    }

    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, NULL);

    return resp;
}


response_t *web_put(const char *url, const char *data, int insecure) {

    response_t *resp = malloc(sizeof(response_t));

    CURL *curl;
    dyn_buffer_t buffer = dyn_buffer_create();

    curl = curl_easy_init();
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *) (&buffer));
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "sist2");
    curl_easy_setopt(curl, CURLOPT_SHARE, 0);
    curl_easy_setopt(curl, CURLOPT_IPRESOLVE, CURLOPT_DNS_LOCAL_IP4);
    if (insecure) {
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0);
    }

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data);

    curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &resp->status_code);

    curl_easy_cleanup(curl);
    curl_slist_free_all(headers);

    resp->body = buffer.buf;
    resp->size = buffer.cur;
    return resp;
}

response_t *web_delete(const char *url, int insecure) {

    response_t *resp = malloc(sizeof(response_t));

    CURL *curl;
    dyn_buffer_t buffer = dyn_buffer_create();

    curl = curl_easy_init();
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *) (&buffer));
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "sist2");
    if (insecure) {
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0);
    }

    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, "");
    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &resp->status_code);

    curl_easy_cleanup(curl);
    curl_slist_free_all(headers);

    resp->body = buffer.buf;
    resp->size = buffer.cur;
    return resp;
}
#ifndef SIST2_WEB_UTIL_H
#define SIST2_WEB_UTIL_H

#include "src/sist.h"
#include "src/index/elastic.h"
#include "src/ctx.h"
#include <mongoose.h>

#define HTTP_SERVER_HEADER "Server: sist2/" VERSION "\r\n"
// See https://web.dev/coop-coep/
#define HTTP_CROSS_ORIGIN_HEADERS "Cross-Origin-Embedder-Policy: require-corp\r\nCross-Origin-Opener-Policy: same-origin\r\n"

index_t *web_get_index_by_id(int index_id);

database_t *web_get_database(int index_id);

__always_inline
static char *web_address_to_string(struct mg_addr *addr) {
    static char address_to_string_buf[64];

    if (addr->is_ip6) {
        snprintf(address_to_string_buf, sizeof(address_to_string_buf),
            "%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x",
                addr->ip[0], addr->ip[1],
                addr->ip[2], addr->ip[3],
                addr->ip[4], addr->ip[5],
                addr->ip[6], addr->ip[7],
                addr->ip[8], addr->ip[9],
                addr->ip[10], addr->ip[11],
                addr->ip[12], addr->ip[13],
                addr->ip[14], addr->ip[15]);
    } else {
        snprintf(address_to_string_buf, sizeof(address_to_string_buf),
                 "%d.%d.%d.%d",
                 addr->ip[0], addr->ip[1], addr->ip[2], addr->ip[3]);
    }

    return address_to_string_buf;
}

void web_send_headers(struct mg_connection *nc, int status_code, size_t length, char *extra_headers);

void web_serve_asset_index_html(struct mg_connection *nc);
void web_serve_asset_index_js(struct mg_connection *nc);
void web_serve_asset_chunk_vendors_js(struct mg_connection *nc);
void web_serve_asset_favicon_ico(struct mg_connection *nc);
void web_serve_asset_style_css(struct mg_connection *nc);

cJSON *web_get_json_body(struct mg_http_message *hm);
char *web_get_string_body(struct mg_http_message *hm);
void mg_send_json(struct mg_connection *nc, const cJSON *json);

#endif //SIST2_WEB_UTIL_H

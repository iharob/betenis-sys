#ifndef __BT_HTTP_HEADERS_H__
#define __BT_HTTP_HEADERS_H__

#include <stdbool.h>

struct httpio;
typedef struct bt_http_headers bt_http_headers;

bt_http_headers *bt_http_headers_new();
int bt_http_headers_append(bt_http_headers *headers, const char *const name, const char *const value, bool replace);
int bt_http_headers_merge(bt_http_headers *dst, const bt_http_headers *const src, bool replace);
int bt_http_headers_write(bt_http_headers *headers, struct httpio *link);
void bt_http_headers_free(bt_http_headers *headers);

#endif /* __BT_HTTP_HEADERS_H__ */

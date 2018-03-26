#include <string.h>

#include <http-connection.h>

#include <bt-http-headers.h>
#include <bt-util.h>
#include <bt-memory.h>

#define BASE_HEADER_LIST_SIZE 0x20

typedef struct bt_http_header {
    char *name;
    char *value;
} bt_http_header;

typedef struct bt_http_headers {
    bool sorted;
    bt_http_header **list;
    size_t size;
    size_t count;
} bt_http_headers;

bt_http_headers *
bt_http_headers_new()
{
    bt_http_headers *headers;
    headers = bt_malloc(sizeof(*headers));
    if (headers == NULL)
        return NULL;
    headers->count = 0;
    headers->list = malloc(BASE_HEADER_LIST_SIZE * sizeof(*headers->list));
    if (headers->list != NULL)
        headers->size = BASE_HEADER_LIST_SIZE;
    headers->sorted = false;
    return headers;
}

static bt_http_header *
bt_http_header_new(const char *const name, const char *const value)
{
    bt_http_header *header;
    header = bt_malloc(sizeof(*header));
    if (header == NULL)
        return NULL;
    header->name = bt_strdup(name);
    header->value = bt_strdup(value);
    return header;
}

int
bt_http_headers_cmp(const void *const lhs, const void *const rhs)
{
    bt_http_header *lhh;
    bt_http_header *rhh;

    lhh = *(bt_http_header **) lhs;
    rhh = *(bt_http_header **) rhs;

    return strcmp(lhh->name, rhh->name);
}

void
bt_http_headers_sort(bt_http_headers *headers)
{
    qsort(headers->list, headers->count,
                                   sizeof(*headers->list), bt_http_headers_cmp);
}

bt_http_header *
bt_http_header_find(bt_http_headers *headers, const char *const name)
{
    bt_http_header *pointer;
    bt_http_header key = {(char *) name, NULL};
    bt_http_header **found;
    if (headers->count == 0)
        return NULL;
    pointer = &key;
    if (headers->sorted == false)
        bt_http_headers_sort(headers);
    found = bsearch(&pointer, headers->list, headers->count,
                                  sizeof(*headers->list), bt_http_headers_cmp);
    if (found == NULL)
        return NULL;
    return *found;
}

int
bt_http_headers_append(bt_http_headers *headers,
                  const char *const name, const char *const value, bool replace)
{
    bt_http_header *header;
    if ((header = bt_http_header_find(headers, name)) != NULL) {
        if (replace == true) {
            bt_free(header->value);
            bt_free(header->name);

            headers->sorted = false;
            header->name = bt_strdup(name);
            header->value = bt_strdup(value);

            return 0;
        } else {
            return 0;
        }
    } else {
        if (headers->count + 1 >= headers->size) {
            void *pointer;
            size_t count;
            count = 2 * headers->size;
            pointer = bt_realloc(headers->list, count * sizeof(*headers->list));
            if (pointer == NULL)
                return -1;
            headers->list = pointer;
        }
        headers->sorted = false;
        headers->list[headers->count] = bt_http_header_new(name, value);
        headers->count += 1;
    }
    return 0;
}

int
bt_http_headers_merge(bt_http_headers *headers,
                       const bt_http_headers *const source, bool replace)
{
    if (source == NULL)
        return 0;
    for (size_t idx = 0; idx < source->count; ++idx) {
        bt_http_header *item;
        const char *name;
        const char *value;
        item = source->list[idx];
        if (item == NULL)
            continue;
        name = item->name;
        if (name == NULL)
            continue;
        value = item->value;
        if (value == NULL)
            continue;
        if (bt_http_headers_append(headers, name, value, replace) != -1)
            continue;
        return -1;
    }
    return 0;
}

int
bt_http_headers_write(bt_http_headers *headers, struct httpio *link)
{
    for (size_t idx = 0; idx < headers->count; ++idx) {
        bt_http_header *item;
        const char *name;
        const char *value;
        item = headers->list[idx];
        if (item == NULL)
            continue;
        name = item->name;
        if (name == NULL)
            continue;
        value = item->value;
        if (value == NULL)
            continue;
        if (httpio_write_line(link, "%s: %s", name, value) != -1)
            continue;
        return -1;
    }
    return 0;
}

void
bt_http_headers_free(bt_http_headers *headers)
{
    if (headers == NULL)
        return;
    for (size_t idx = 0; idx < headers->count; ++idx) {
        bt_http_header *item;
        item = headers->list[idx];
        if (item == NULL)
            continue;
        bt_free(item->name);
        bt_free(item->value);
        bt_free(item);
    }
    bt_free(headers->list);
    bt_free(headers);
}

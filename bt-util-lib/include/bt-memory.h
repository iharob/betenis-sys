#ifndef __BT_MEMORY_H__
#define __BT_MEMORY_H__

#include <stdlib.h>

void *bt_malloc(size_t size);
void *bt_calloc(size_t nmemb, size_t size);
void *bt_realloc(void *ptr, size_t size);
void bt_free(void *ptr);

#endif // __BT_MEMORY_H__

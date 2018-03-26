#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stddef.h>

#include <bt-memory.h>

void *
bt_malloc(size_t size)
{
    return malloc(size);
}

void *
bt_calloc(size_t nmemb, size_t size)
{
    return calloc(nmemb, size);
}

void
bt_free(void *ptr)
{
    free(ptr);
}

void *
bt_realloc(void *ptr, size_t size)
{
    return realloc(ptr, size);
}

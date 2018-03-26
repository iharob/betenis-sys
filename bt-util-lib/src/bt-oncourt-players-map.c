#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include <bt-memory.h>
#include <bt-util.h>

#ifndef SYSCONFDIR
#define SYSCONFDIR "/etc"
#endif

#define MAP_FILES_DIRECTORY SYSCONFDIR "/bt/players.data/"

typedef struct bt_oc_map_item_ru {
	char *name;
	int id;
} bt_oc_map_item_ru;

typedef struct bt_oc_map_ru {
	bt_oc_map_item_ru *items;
	size_t count;
} bt_oc_map_ru;

void
bt_oc_map_ru_free(bt_oc_map_ru *map)
{
    if (map == NULL)
        return;
    for (size_t idx = 0; idx < map->count; ++idx) {
        bt_oc_map_item_ru *item;
        item = &map->items[idx];

        bt_free(item->name);
    }
    bt_free(map->items);
    bt_free(map);
}

static bt_oc_map_ru *
bt_oc_map_ru_new(size_t count)
{
    bt_oc_map_ru *map;

    map = bt_malloc(sizeof(*map));
    if (map == NULL)
        return NULL;
    map->items = bt_malloc(count * sizeof(*map->items));
    if (map->items == NULL)
        map->items = NULL;
    map->count = count;
    return map;
}

static int
bt_oc_map_ru_compare(const void *const lhs, const void *const rhs)
{
    return strcmp(((bt_oc_map_item_ru *) lhs)->name, ((bt_oc_map_item_ru *) rhs)->name);
}

static void
bt_oc_map_ru_sort(bt_oc_map_ru *map)
{
    if (map->items == NULL)
        return;
    qsort(map->items, map->count, sizeof(*map->items), bt_oc_map_ru_compare);
}

bt_oc_map_item_ru *
bt_oc_map_ru_find(bt_oc_map_ru *map, const char *const name)
{
    bt_oc_map_item_ru item;
    if (map == NULL)
        return NULL;
    item.name = (char *) name;
    return bsearch(&item, map->items, map->count,
                                    sizeof(*map->items), bt_oc_map_ru_compare);
}

static bt_oc_map_ru *
bt_oncourt_map_load(const char *const filepath)
{
    size_t count;
    char name[100];
    bt_oc_map_ru *atp;
    int id;
    FILE *file;

    file = fopen(filepath, "r");
    if (file == NULL)
        return NULL;
    count = 0;
    while (fscanf(file, "%99[^|]|%d", name, &id) == 2) {
        if (*name == '#')
            continue;
        count += 1;
    }

    atp = bt_oc_map_ru_new(count);
    if (atp == NULL)
        goto error;

    rewind(file);

    count = 0;
    while (fscanf(file, "%99[^|]|%d", name, &id) == 2) {
        bt_oc_map_item_ru *item;
        size_t length;
        if (*name == '#')
            continue;
        item = &atp->items[count];
        length = strlen(name);
        item->name = bt_stripdup(name, &length);
        item->id = id;

        count += 1;
    }
    bt_oc_map_ru_sort(atp);
error:
    fclose(file);
    return atp;
}

bt_oc_map_ru *
bt_oc_map_load_atp(void)
{
    return bt_oncourt_map_load(MAP_FILES_DIRECTORY "atp.txt");
}

bt_oc_map_ru *
bt_oc_map_load_wta(void)
{
    return bt_oncourt_map_load(MAP_FILES_DIRECTORY "wta.txt");
}

int
bt_oc_map_item_ru_get_id(bt_oc_map_item_ru *item)
{
    return item->id;
}

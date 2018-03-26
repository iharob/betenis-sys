#ifndef __BT_ONCOURT_PLAYERS_MAP_H__
#define __BT_ONCOURT_PLAYERS_MAP_H__

typedef struct bt_oc_map_item_ru bt_oc_map_item_ru;
typedef struct bt_oc_map_ru bt_oc_map_ru;

bt_oc_map_item_ru *bt_oc_map_ru_find(bt_oc_map_ru *map, const char *const name);

void bt_oc_map_ru_free(bt_oc_map_ru *map);

bt_oc_map_ru *bt_oc_map_load_atp(void);
bt_oc_map_ru *bt_oc_map_load_wta(void);

int bt_oc_map_item_ru_get_id(bt_oc_map_item_ru *item);

#endif // __BT_ONCOURT_PLAYERS_MAP_H__

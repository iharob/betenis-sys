#ifndef __BT_CHANNEL_SETTINGS_H__
#define __BT_CHANNEL_SETTINGS_H__

/** @file
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <bt-util.h>

typedef struct bt_channel_settings bt_channel_settings;
typedef bool (*bt_channel_settings_check_fn)(bt_tennis_category,size_t);

void bt_channel_settings_finalize(void);
size_t bt_channel_settings_count();
const char *bt_channel_settings_get_name(int idx);
ssize_t bt_channel_settings_get_id(char *target, size_t length, int idx);

bool bt_channel_settings_mto_enabled(bt_tennis_category category, size_t idx);
bool bt_channel_settings_mto_score_enabled(bt_tennis_category category, size_t idx);
bool bt_channel_settings_mto_summary_enabled(bt_tennis_category category, size_t idx);
bool bt_channel_settings_mto_show_player(bt_tennis_category category, size_t idx);
bool bt_channel_settings_get_dogs(bt_tennis_category category, size_t idx);
bool bt_channel_settings_get_super_dogs(bt_tennis_category category, size_t idx);
bool bt_channel_settings_get_retired(bt_tennis_category category, size_t idx);
bool bt_channel_settings_get_drops(bt_tennis_category category, size_t idx);
bool bt_channel_settings_get_super_drops(bt_tennis_category category, size_t idx);
bool bt_channel_settings_get_ex_drops(bt_tennis_category category, size_t idx);
bool bt_channel_settings_get_retired(bt_tennis_category category, size_t idx);
bool bt_channel_settings_new_odds(bt_tennis_category category, size_t idx);
#endif // __BT_CHANNEL_SETTINGS_H__

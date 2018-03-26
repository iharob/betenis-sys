#include <bt-channel-settings.h>
#include <bt-memory.h>
#include <bt-context.h>

#include <limits.h>

#include <sys/stat.h>
#include <stdio.h>
#include <string.h>

#ifndef SYSCONFDIR
#define SYSCONFDIR "/etc"
#endif

typedef struct bt_channel_settings_mto_object {
    uint8_t enabled;
    uint8_t show_player_name;
    uint8_t show_score_enabled;
    uint8_t summary_enabled;
} __attribute__((packed)) bt_channel_settings_mto_object;

typedef struct common_data {
    uint8_t singles_wta;
    uint8_t singles_atp;
    uint8_t singles_itf;
    uint8_t doubles_wta;
    uint8_t doubles_atp;
    uint8_t doubles_itf;
} __attribute__((packed)) common_data;

typedef struct bt_channel_settings {
    int64_t id;
    char name[64];

    bt_channel_settings_mto_object singles_wta_mto;
    bt_channel_settings_mto_object singles_atp_mto;
    bt_channel_settings_mto_object doubles_wta_mto;
    bt_channel_settings_mto_object doubles_atp_mto;

    common_data normal_dogs;
    common_data super_dogs;
    common_data normal_drops;
    common_data super_drops;
    common_data ex_drops;

    common_data retired;
    common_data new_odds;
} __attribute__((packed)) bt_channel_settings;

typedef struct bt_channel_settings_list {
    bt_channel_settings *items;
    size_t count;
} bt_channel_settings_list;

__thread bt_channel_settings_list global_settings;

static bt_channel_settings *
bt_channel_settings_load(size_t *count)
{
    struct stat st;
    bt_channel_settings *settings;
    const char *filename;
    char path[PATH_MAX];
    const char *home;
    ssize_t length;
    FILE *file;

    settings = NULL;
    home = getenv("HOME");
    filename = ".channel-settings.bt";
    length = snprintf(path, sizeof(path), "%s/%s", home, filename);
    if ((length < 0) || (length >= sizeof(path)))
        return NULL;
    // Stat the file
    if (stat(path, &st) == -1) {
        fprintf(stdout, "%s\n", path);
        return NULL;
    }
    // If no channel settings are loaded ensure
    // this makes sense
    *count = 0;
    // Open for reading
    file = fopen(path, "r");
    if (file == NULL)
        return NULL;
    *count = st.st_size / sizeof(*settings);
    if (*count == 0)
        goto error;
    settings = bt_malloc(*count * sizeof(*settings));
    if (settings == NULL)
        goto error;
    if (fread(settings, sizeof(*settings), *count, file) != *count) {
        *count = 0;

        bt_free(settings);
        fclose(file);

        return NULL;
    }
error:
    fclose(file);

    fprintf(stdout, "channel: %s\n", settings->name);
    return settings;
}

static bool
bt_channel_settings_initialize(void)
{
    if (global_settings.count > 0)
        return true;
    global_settings.items = bt_channel_settings_load(&global_settings.count);
    return (global_settings.items != NULL);
}

static const bt_channel_settings *
bt_channel_settings_get(int idx)
{
    if (bt_channel_settings_initialize() == false)
        return NULL;
    if ((global_settings.count <= idx) || (idx < 0))
        return NULL;
    return &global_settings.items[idx];
}

const char *
bt_channel_settings_get_name(int idx)
{
    const bt_channel_settings *settings;
    if ((settings = bt_channel_settings_get(idx)) == NULL)
        return NULL;
    return settings->name;
}

size_t
bt_channel_settings_count()
{
    if (bt_channel_settings_initialize() == false)
        return 0;
    return global_settings.count;
}

void
bt_channel_settings_finalize(void)
{
    if (global_settings.count > 0) {
        bt_free(global_settings.items);

        global_settings.items = NULL;
        global_settings.count = 0;
    }
}

const static bt_channel_settings_mto_object *
bt_channel_settings_mto(bt_tennis_category category,
                               const bt_channel_settings *const settings)
{
    if ((category & CategoryATP) == CategoryATP) {
        if ((category & DoublesMask) == DoublesMask)
            return &settings->doubles_atp_mto;
        return &settings->singles_atp_mto;
    } else if ((category & CategoryWTA) == CategoryWTA) {
        if ((category & DoublesMask) == DoublesMask)
            return &settings->doubles_wta_mto;
        return &settings->singles_wta_mto;
    } else {
        return NULL;
    }
    return NULL;
}

bool
bt_channel_settings_mto_enabled(bt_tennis_category category, size_t idx)
{
    const bt_channel_settings *settings;
    const bt_channel_settings_mto_object *object;
    settings = bt_channel_settings_get(idx);
    if (settings == NULL)
        return false;
    object = bt_channel_settings_mto(category, settings);
    if (object == NULL)
        return false;
    return object->enabled;
}

bool
bt_channel_settings_mto_score_enabled(bt_tennis_category category, size_t idx)
{
    const bt_channel_settings *settings;
    const bt_channel_settings_mto_object *object;
    settings = bt_channel_settings_get(idx);
    if (settings == NULL)
        return false;
    object = bt_channel_settings_mto(category, settings);
    if (object == NULL)
        return false;
    return object->show_score_enabled;
}

bool
bt_channel_settings_mto_summary_enabled(bt_tennis_category category, size_t idx)
{
    const bt_channel_settings *settings;
    const bt_channel_settings_mto_object *object;
    settings = bt_channel_settings_get(idx);
    if (settings == NULL)
        return false;
    object = bt_channel_settings_mto(category, settings);
    if (object == NULL)
        return false;
    return object->summary_enabled;
}

ssize_t
bt_channel_settings_get_id(char *target, size_t length, int idx)
{
    const bt_channel_settings *settings;
    settings = bt_channel_settings_get(idx);
    if (settings == NULL)
        return 0;
    return snprintf(target, length, "%ld", settings->id);
}

static bool
bt_channel_settings_get_common(bt_tennis_category category, const common_data *const object)
{
    if ((category & CategoryATP) == CategoryATP) {
        if ((category & DoublesMask) == DoublesMask)
            return object->doubles_atp;
        return object->singles_atp;
    } else if ((category & CategoryWTA) == CategoryWTA) {
        if ((category & DoublesMask) == DoublesMask)
            return object->doubles_wta;
        return object->singles_wta;
    }
    return false;
}

bool
bt_channel_settings_get_dogs(bt_tennis_category category, size_t idx)
{
    const bt_channel_settings *settings;
    settings = bt_channel_settings_get(idx);
    if (settings == NULL)
        return false;
    return bt_channel_settings_get_common(category, &settings->normal_dogs);
}

bool
bt_channel_settings_get_super_dogs(bt_tennis_category category, size_t idx)
{
    const bt_channel_settings *settings;
    settings = bt_channel_settings_get(idx);
    if (settings == NULL)
        return false;
    return bt_channel_settings_get_common(category, &settings->super_dogs);
}

bool
bt_channel_settings_get_drops(bt_tennis_category category, size_t idx)
{
    const bt_channel_settings *settings;
    settings = bt_channel_settings_get(idx);
    if (settings == NULL)
        return false;
    return bt_channel_settings_get_common(category, &settings->normal_drops);
}

bool
bt_channel_settings_get_super_drops(bt_tennis_category category, size_t idx)
{
    const bt_channel_settings *settings;
    settings = bt_channel_settings_get(idx);
    if (settings == NULL)
        return false;
    return bt_channel_settings_get_common(category, &settings->super_drops);
}

bool
bt_channel_settings_get_ex_drops(bt_tennis_category category, size_t idx)
{
    const bt_channel_settings *settings;
    settings = bt_channel_settings_get(idx);
    if (settings == NULL)
        return false;
    return bt_channel_settings_get_common(category, &settings->ex_drops);
}

bool
bt_channel_settings_get_retired(bt_tennis_category category, size_t idx)
{
    const bt_channel_settings *settings;
    settings = bt_channel_settings_get(idx);
    if (settings == NULL)
        return false;
    return bt_channel_settings_get_common(category, &settings->retired);
}

bool
bt_channel_settings_new_odds(bt_tennis_category category, size_t idx)
{
    return false;
}

bool
bt_channel_settings_mto_show_player(bt_tennis_category category, size_t idx)
{
    const bt_channel_settings *settings;
    const bt_channel_settings_mto_object *mto;
    settings = bt_channel_settings_get(idx);
    if (settings == NULL)
        return false;
    mto = bt_channel_settings_mto(category, settings);
    if (mto == NULL)
        return false;
    return mto->show_player_name;
}

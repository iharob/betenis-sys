#include <bt-telegram-channel.h>
#include <bt-channel-settings.h>
#include <bt-drops.h>
#include <bt-util.h>
#include <bt-memory.h>

#include <stdio.h>
#include <string.h>

typedef struct bt_drop {
    char name[256];
    double previous;
    double current;
} bt_drop;

#define ARROW_UP "\xE2\x86\x97"
#define ARROW_DO "\xE2\x86\x99"

#define ND_MSG                                       \
    "Drop de %.2f%% <b>%s</b>\n"                     \
    "\n"                                             \
    ARROW_DO "  %s <b>%.4f</b> &#8594 <b>%.4f</b>\n" \
    ARROW_UP "  %s %.4f &#8594; %.4f\n"              \

#define SD_MSG                                       \
    "Super drop de %.2f%% <b>%s</b>\n"               \
    "\n"                                             \
    ARROW_DO "  %s <b>%.4f</b> &#8594 <b>%.4f</b>\n" \
    ARROW_UP "  %s %.4f &#8594; %.4f\n"              \


static int
bt_new_drop_message_link(const char *const iid)
{
    int link;
    const char *query;
    MYSQL_STMT *stmt;
    query = "SELECT telegram FROM mercado_ganador_partido WHERE iid = ?";
    stmt = bt_mysql_easy_query(query, "%s|%d", iid, &link);
    if (stmt == NULL)
        return -1;
    mysql_stmt_fetch(stmt);
    mysql_stmt_free_result(stmt);
    mysql_stmt_close(stmt);
    if (link != -1)
        return link;
    query = "SELECT MAX(telegram) + 1 FROM mercado_ganador_partido";
    stmt = bt_mysql_easy_query(query, "|%d", &link);
    if (stmt == NULL)
        return -1;
    mysql_stmt_fetch(stmt);
    mysql_stmt_free_result(stmt);
    mysql_stmt_close(stmt);

    query = "UPDATE mercado_ganador_partido SET telegram = ? WHERE iid = ?";
    stmt = bt_mysql_easy_query(query, "%d%s", &link, iid);
    if (stmt == NULL)
        return -1;
    mysql_stmt_close(stmt);
    return link;
}

static int
bt_drops_get_message_id(const char *const channel, int link)
{
    int id;
    const char *query;
    MYSQL_STMT *stmt;
    query = "SELECT id FROM mercado_ganador_partido_telegram_ids "
            "WHERE link = ? AND channel = ?";
    id = -1;
    stmt = bt_mysql_easy_query(query, "%d%s|%d", &link, channel, &id);
    if (stmt == NULL)
        return -1;
    mysql_stmt_fetch(stmt);
    mysql_stmt_free_result(stmt);
    mysql_stmt_close(stmt);

    return id;
}

static void
bt_drops_set_message_id(const char *const channel, int link, int id)
{
    const char *query;
    MYSQL_STMT *stmt;
    query = "INSERT INTO mercado_ganador_partido_telegram_ids "
            "(link, channel, id) VALUES (?, ?, ?)";
    stmt = bt_mysql_easy_query(query, "%d%s%d", &link, channel, &id);
    if (stmt == NULL)
        return;
    mysql_stmt_close(stmt);
    return;
}

static void
bt_drops_send_any(const char *const format, bt_drop *up, bt_drop *down, int link,
         const char *const tour, bt_tennis_category category, double value)
{
    size_t chcount;
    chcount = bt_channel_settings_count();
    for (size_t idx = 0; idx < chcount; ++idx) {
        int telegram;
        char channel[100];
        int result;
        if (bt_channel_settings_get_super_drops(category, idx) == false)
            continue;
        result = bt_channel_settings_get_id(channel, sizeof(channel), idx);
        if ((result < 0) || (result >= sizeof(channel)))
            continue;
        telegram = bt_drops_get_message_id(channel, link);
        result = bt_telegram_edit_message(telegram, channel, format, value, tour,
                    up->name, up->previous, up->current, down->name, down->previous, down->current);
        if (result == telegram)
            continue;
        bt_drops_set_message_id(channel, link, result);
    }
}

static void
bt_drops_send_super_drop(bt_drop *up, bt_drop *down, int link,
         const char *const tour, bt_tennis_category category, double value)
{
    bt_drops_send_any(SD_MSG, up, down, link, tour, category, value);
}

static void
bt_drops_send_drop(bt_drop *up, bt_drop *down, int link,
         const char *const tour, bt_tennis_category category, double value)
{
    bt_drops_send_any(ND_MSG, up, down, link, tour, category, value);
}

void
bt_check_drops()
{
    MYSQL_STMT *stmt;
    bt_tennis_category category;
    char tour[256];
    char iid[256];
    bt_drop D1;
    bt_drop D2;
    char *query;
    double value;
    int link;

    query = bt_load_query("drops", "", NULL);
    if (query == NULL)
        return;
    value = 0.0;
    memset(tour, 0, sizeof(tour));
    memset(&D1, 0, sizeof(D1));
    memset(&D2, 0, sizeof(D2));
    stmt = bt_mysql_easy_query(query, "|%256a%256a%256a%256a%lf%lf%lf%lf%lf%d%d",
                iid, tour, D1.name, D2.name, &D1.current, &D1.previous, &D2.current, &D2.previous, &value, &category, &link);
    bt_free(query);
    if (stmt == NULL)
        return;
    while (mysql_stmt_fetch(stmt) == 0) {
        if (link == -1)
            link = bt_new_drop_message_link(iid);
        if (value > 9.0) { // Super drop (home)
            bt_drops_send_super_drop(&D2, &D1, link, tour, category, value);
        } else if (value > 4.0) { // Drop (home)
            bt_drops_send_drop(&D2, &D1, link, tour, category, value);
        } else if (value < -9.0) { // Super drop (away)
            bt_drops_send_super_drop(&D1, &D2, link, tour, category, -value);
        } else if (value < -4.0) { // Drop (away)
            bt_drops_send_drop(&D1, &D2, link, tour, category, -value);
        }
    }
    mysql_stmt_close(stmt);
}

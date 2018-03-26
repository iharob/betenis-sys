#include <stdio.h>
#include <string.h>

#include <bt-mbet-fresh-odds-notifier.h>
#include <bt-database.h>
#include <bt-memory.h>
#include <bt-util.h>
#include <bt-telegram-channel.h>
#include <bt-string-builder.h>
#include <bt-channel-settings.h>

#include <bt-private.h>
typedef struct bt_mbet_event bt_mbet_event;

static void
bt_mbet_fresh_odds_save_markets(const bt_mbet_event *const event,
                                           bt_mysql_operation *operation)
{
    bt_mbet_member *home;
    bt_mbet_member *away;
    bt_mbet_list *list;
    if (event->octour == -1)
        return;
    home = event->home;
    away = event->away;

    list = event->markets;
    for (size_t idx = 0; idx < list->count; ++idx) {
        bt_mbet_selection *s_home;
        bt_mbet_selection *s_away;
        const bt_mbet_list_item *item;
        bt_mbet_list *selections;
        bt_mbet_market *market;
        item = list->items[idx];
        market = item->data;
        if (strcmp(market->model, "MTCH_R") != 0)
            continue;
        selections = market->selections;
        if (list->count != 2)
            continue;
        item = selections->items[0];
        s_home = item->data;
        if (s_home->selkey[0] == 'H') {
            item = selections->items[1];
            s_away = item->data;
        } else if (s_home->selkey[0] == 'A') {
            item = selections->items[1];
            s_home = item->data;
            item = selections->items[0];
            s_away = item->data;
        } else {
            continue;
        }

        bt_mysql_operation_put(operation, "%d%d%ld%d%f%d%f%s",
             event->octour,
             event->ocround,
             event->tree_id,
             home->ocid,
             s_home->coeff,
             away->ocid,
             s_away->coeff,
             event->url
        );
    }
}

static int
bt_mbet_fresh_odds_save_odds(const bt_mbet_sport *const sport)
{
    bt_mbet_group *group;
    bt_mysql_transaction *transaction;
    bt_mbet_list *list;
    const char *queries[2] = {
        "INSERT IGNORE INTO `bt_mbet_fresh_odds_atp` (`tour`, `round`, `event`, `T1`, `O1`, `T2`, `O2`, `url`) VALUES %values%",
        "INSERT IGNORE INTO `bt_mbet_fresh_odds_wta` (`tour`, `round`, `event`, `T1`, `O1`, `T2`, `O2`, `url`) VALUES %values%"
    };
    transaction = bt_mysql_transaction_new(2, 8, queries[0], 8, queries[1]);
    if (transaction == NULL)
        return -1;
    list = sport->groups;
    for (size_t idx = 0; idx < list->count; ++idx) {
        bt_mbet_list *events;
        bt_mysql_operation *operation;
        bt_mbet_list_item *item;
        item = list->items[idx];
        if (item == NULL)
            continue;
        group = item->data;
        if (group->ocid == -1)
            continue;
        if ((group->category & CategoryATP) == CategoryATP) {
            operation = bt_transaction_get_operation(transaction, 0);
        } else if ((group->category & CategoryWTA) == CategoryWTA) {
            operation = bt_transaction_get_operation(transaction, 1);
        }
        if (operation == NULL)
            continue;
        events = group->events;
        for (size_t jdx = 0; jdx < events->count; ++jdx) {
            item = events->items[jdx];
            if (item == NULL)
                continue;
            bt_mbet_fresh_odds_save_markets(item->data, operation);
        }
    }
    bt_mysql_transaction_execute(transaction);
    bt_mysql_transaction_free(transaction);

    return 0;
}

static int
bt_mbet_fresh_odds_send_tour(const bt_mbet_group *const group)
{
    MYSQL_STMT *stmt;
    char *query;
    char tour[128];
    char round[32];
    char T1[64];
    double O1;
    char T2[64];
    double O2;
    char url[256];
    bt_string_builder *sb;
    const char *message;
    const char *category;
    int result;

    category = bt_get_category_name(group->category);
    query = bt_load_query("new odds", "%category%", category, NULL);
    if (query == NULL)
        return -1;
    stmt = bt_mysql_easy_query(query, "%d%d|%128a%32a%64a%lf%64a%lf%256a",
             &group->ocid, &group->ocround, tour, round, T1, &O1, T2, &O2, url);
    bt_free(query);
    result = -1;
    if (stmt == NULL)
        return -1;
    if (mysql_stmt_store_result(stmt) != 0)
        goto error;
    if (mysql_stmt_num_rows(stmt) == 0)
        goto error;
    sb = bt_string_builder_new();
    if (sb == NULL)
        goto error;
    if (mysql_stmt_fetch(stmt) != 0)
        goto error;
    bt_string_builder_printf(sb,
        "<a href=\"http://" MBET_URL "\">Nuevas cuotas publicadas</a>\n"
        "<b>%s</b>\n<i>%s</i>\n\n", tour, round);
    do {
        bt_string_builder_printf(sb,
            "%s <b>%.2f</b>\n"
            "%s <b>%.2f</b>\n"
            "<a href=\"%s\">ver en www.mbet.com</a>\n\n",
            T1, O1, T2, O2, url);
    } while (mysql_stmt_fetch(stmt) == 0);

    message = bt_string_builder_string(sb);
    for (size_t idx = 0; idx < bt_channel_settings_count(); ++idx) {
        char channel[128];
        if (bt_channel_settings_new_odds(group->category, idx) == false)
            continue;
        result = bt_channel_settings_get_id(channel, sizeof(channel), idx);
        if ((result < 0) || (result >= sizeof(channel)))
            continue;
        if (message != NULL) {
            result = bt_telegram_send_message(channel, "%s", message);
        }
    }
    bt_string_builder_free(sb);

    mysql_stmt_free_result(stmt);
error:
    mysql_stmt_close(stmt);
    return result;
}

void
bt_mbet_fresh_odds_check(bt_mbet_sport *sport)
{
    bt_mbet_list *list;
    if (bt_mbet_fresh_odds_save_odds(sport) == -1)
        return;
    list = sport->groups;
    for (size_t idx = 0; idx < list->count; ++idx) {
        bt_mbet_list_item *item;
        item = list->items[idx];
        if (item == NULL)
            continue;
        bt_mbet_fresh_odds_send_tour(item->data);
    }
    bt_mysql_execute_query("UPDATE `bt_mbet_fresh_odds_itf` "
                                    "SET `fresh` = FALSE WHERE `fresh` = TRUE");
}

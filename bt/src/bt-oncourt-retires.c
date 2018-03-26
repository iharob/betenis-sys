#include <bt-util.h>
#include <bt-database.h>
#include <bt-telegram-channel.h>
#include <bt-string-builder.h>
#include <bt-memory.h>
#include <bt-channel-settings.h>

#include <stdio.h>

static bt_tennis_category categories[] = {
    CategoryATP, CategoryDoublesATP,
    CategoryWTA, CategoryDoublesWTA
};

static const char *queries[] = {"list retires", "list retires doubles"};

typedef struct described_object {
    int id;
    char name[128];
} described_object;

void
oncourt_send_new_retires()
{
    MYSQL_STMT *stmt;
    bt_string_builder *sb;
    described_object tour;
    described_object p1;
    described_object p2;
    int count;
    const char *msg;
    char *query;
    // Initialize the database for this thread
    // Create a new string builder
    bt_database_initialize();
    sb = bt_string_builder_new();
    // Do this for each category
    for (size_t idx = 0; idx < sizeof(categories) / sizeof(*categories); ++idx) {
        const char *category;
        int status;
        category = bt_get_category_name(categories[idx]);
        // Get current category's name
        // Reset the string builder
        bt_string_builder_reset(sb);
        // Get the correct query for this category
        query = bt_load_query(queries[(idx + 1) % 2], "%category%", category, NULL);
        if (query == NULL)
            continue;
        fprintf(stderr, "%s\n", query);
        // Create and execute the SQL statement
        stmt = bt_mysql_easy_query(query, "|%64a%64a%128a%d%d%d%d",
                 p1.name, p2.name, tour.name, &p1.id, &p2.id, &tour.id, &count);
        // Free the temporary query string
        bt_free(query);
        if (stmt == NULL) {
            bt_database_finalize();
            return;
        }
        // Make a message from each row
        while ((count = mysql_stmt_fetch(stmt)) == 0) {
            bt_string_builder_printf(sb, RETIRED_MESSAGE,
                 p1.name, p2.name, tour.name, (count > 0) ? PLAYS_DOUBLES : "");
        }
        // Free the result set
        mysql_stmt_free_result(stmt);
        // Close the statement, so the next one can execute
        mysql_stmt_close(stmt);
        // Get the generated message
        msg = bt_string_builder_string(sb);
        if ((msg == NULL) || (msg[0] == '\0'))
            continue;
        for (size_t jdx = 0; jdx < bt_channel_settings_count(); ++jdx) {
            char channel[32];
            ssize_t result;
            result = bt_channel_settings_get_id(channel, sizeof(channel), jdx);
            if ((result < 0) || (result >= sizeof(channel)))
                continue;
            if (bt_channel_settings_get_retired(categories[idx], jdx) == false)
                continue;
            // Send the message it and check for success
            status = bt_telegram_send_message(channel, RETIRED_LIST, msg);
            if (status != -1)
                continue;
            goto error;
        }
    }

error:
    query = bt_load_query("update retires", "%category%", "atp", NULL);
    if (query != NULL) {
        // Execute it
        bt_mysql_execute_query(query);
        // Free the query string now
        bt_free(query);
    }

    query = bt_load_query("update retires", "%category%", "wta", NULL);
    if (query != NULL) {
        // Execute it
        bt_mysql_execute_query(query);
        // Free the query string now
        bt_free(query);
    }

    bt_channel_settings_finalize();
    bt_database_finalize();
    // Release temporary resources
    bt_string_builder_free(sb);
    return;
}

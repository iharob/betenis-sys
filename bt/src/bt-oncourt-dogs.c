#include <bt-util.h>
#include <bt-telegram-channel.h>
#include <bt-string-builder.h>
#include <bt-memory.h>
#include <bt-channel-settings.h>
#include <bt-database.h>

#include <limits.h>
#include <string.h>
#include <stdio.h>

#define DOGS_PARM_FMT "%lf%lf|%d%d%d%d%128a%lf%128a%lf%128a%128a%256a"
typedef struct bt_dogs_data
{
    char result[128];
    char P1[128];
    char P2[128];
    char tour_name[128];
    char url[256];
    int tour;
    int ID1;
    double K1;
    int ID2;
    double K2;
    int round;
} bt_dogs_data;

static MYSQL_STMT *
oncourt_get_dogs(const char *const category, enum bt_tennis_subcategory sc,
                              double min, double max, bt_dogs_data *data)
{
    MYSQL_STMT *stmt;
    char *query;
    // Get the appropriate query
    switch (sc) {
    case Singles:
        query = bt_load_query("list dogs", "%category%", category, NULL);
        break;
    case Doubles:
        query = bt_load_query("list dogs doubles", "%category%", category, NULL);
        break;
    }
    // Check if there is a query
    if (query == NULL)
        return NULL;
    // Execute it
    stmt = bt_mysql_easy_query(query, DOGS_PARM_FMT, &min, &max,
        &data->ID1, // First player ID
        &data->ID2, // Second player ID
        &data->tour, // Tournament ID
        &data->round, // Round ID
         data->P1, // First player name
        &data->K1, // First player odds
         data->P2, // Second player name
        &data->K2, // Second player odds
         data->result, // Match result (TODO: Prettify)
         data->tour_name, // Tournament name
         data->url // tournament URL
    );
    // Free the query
    bt_free(query);
    // Return the statement object to fetch the results
    return stmt;
}

static bt_mysql_transaction *
bt_oncourt_get_update_transaction_object(const char *const category)
{
    bt_mysql_transaction *transaction;
    char *query;
    query = bt_load_query("update dogs", "%category%", category, NULL);
    if (query == NULL)
        return NULL;
    transaction = bt_mysql_transaction_new(1, 4, query);
    // Free the query string
    bt_free(query);
    return transaction;
}


static void
oncourt_send_new_dogs(const char *const category,
    double min, double max, const char *const id, enum bt_tennis_subcategory sc)
{
    bt_string_builder *sb;
    bt_mysql_transaction *transaction;
    bt_mysql_operation *operation;
    bt_dogs_data data;
    const char *message;
    MYSQL_STMT *stmt;
    // Get a executed MySQL statment with the dogs in it's rows
    stmt = oncourt_get_dogs(category, sc, min, max, &data);
    if (stmt == NULL)
        return;
    // Get the updater object
    transaction = bt_oncourt_get_update_transaction_object(category);
    if (transaction == NULL)
        goto error;
    operation = bt_transaction_get_operation(transaction, 0);
    if (operation == NULL)
        goto error;
    // Store the results to get the number of rows
    if (mysql_stmt_store_result(stmt) != 0)
        goto error;
    // Check the number of rows
    if (mysql_stmt_num_rows(stmt) == 0)
        goto error;
    // Build the message string
    sb = bt_string_builder_new();
    if (sb == NULL)
        goto error;
    while (mysql_stmt_fetch(stmt) == 0) {
        // Add an item to the operation to update this object
        bt_mysql_operation_put(operation, "%d%d%d%d",
            data.ID1, // First player ID
            data.ID2, // Second player ID
            data.tour, // Tournament ID
            data.round // Tournament round ID
        );
        // Insert the message into the string builder
        bt_string_builder_printf(sb, DOG_MESSAGE,
            data.P1, // First player name
            data.K1, // First player odds
            data.P2, // Second player name
            data.K2, // Second player odds
            data.result, // Match result
            data.url, // tournament url
            data.tour_name // Tournament name
        );
    }
    // Get a reference to the string with the message
    message = bt_string_builder_string(sb);
    // Check that it's not `NULL` or empty
    if ((message != NULL) && (message[0] != '\0')) {
        // Send the message using the appropriate `send' function
        // as determined by the caller
        bt_telegram_edit_message(-1, id, DOGS_TITLE, message);
    }
    // Release string builder resources
    bt_string_builder_free(sb);
    // Execute the update transaction
    bt_mysql_transaction_execute(transaction);
error:
    // Release all other resources
    bt_mysql_transaction_free(transaction);

    mysql_stmt_free_result(stmt);
    mysql_stmt_close(stmt);
}

void
oncourt_send_new_normal_dogs()
{
    // Normal dogs have odds between 0 and 3
    bt_database_initialize();
    for (size_t idx = 0; idx < bt_channel_settings_count(); ++idx) {
        char channel[32];
        ssize_t result;
        result = bt_channel_settings_get_id(channel, sizeof(channel), idx);
        if ((result < 0) || (result >= sizeof(channel)))
            continue;
        if (bt_channel_settings_get_dogs(CategoryATP, idx) == true)
            oncourt_send_new_dogs("atp", 0.0, 3.0, channel, Singles);
        if (bt_channel_settings_get_dogs(CategoryDoublesATP, idx) == true)
            oncourt_send_new_dogs("atp", 0.0, 3.0, channel, Doubles);
        if (bt_channel_settings_get_dogs(CategoryWTA, idx) == true)
            oncourt_send_new_dogs("wta", 0.0, 3.0, channel, Singles);
        if (bt_channel_settings_get_dogs(CategoryDoublesWTA, idx) == true)
            oncourt_send_new_dogs("wta", 0.0, 3.0, channel, Doubles);
    }
    bt_channel_settings_finalize();
    bt_database_finalize();
}

void
oncourt_send_new_super_dogs()
{
    // Super dogs have odds between 3.5 and infinity
    for (size_t idx = 0; idx < bt_channel_settings_count(); ++idx) {
        char channel[32];
        ssize_t result;
        result = bt_channel_settings_get_id(channel, sizeof(channel), idx);
        if ((result < 0) || (result >= sizeof(channel)))
            continue;
        if (bt_channel_settings_get_super_dogs(CategoryATP, idx) == true)
            oncourt_send_new_dogs("atp", 3.0, LONG_MAX, channel, Singles);
        if (bt_channel_settings_get_super_dogs(CategoryDoublesATP, idx) == true)
            oncourt_send_new_dogs("atp", 3.0, LONG_MAX, channel, Doubles);
        if (bt_channel_settings_get_super_dogs(CategoryWTA, idx) == true)
            oncourt_send_new_dogs("wta", 3.0, LONG_MAX, channel, Singles);
        if (bt_channel_settings_get_super_dogs(CategoryDoublesWTA, idx) == true)
            oncourt_send_new_dogs("wta", 3.0, LONG_MAX, channel, Doubles);
    }
}

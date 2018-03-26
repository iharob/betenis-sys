#include <string.h>

#include <bt-database.h>
#include <bt-util.h>
#include <bt-telegram-channel.h>
#include <bt-mbet-feed.h>
#include <bt-mbet.h>
#include <bt-memory.h>
#include <bt-channel-settings.h>
#include <bt-drops.h>
#include <bt-debug.h>

#include <bt-context.h>

#include <sys/time.h>

#include <bt-private.h>

#define bt_mbet_drop_value(previous, current) ((previous) - (current)) / (previous)
#define UPDATE_QUERY "UPDATE mercado_ganador_partido SET isnew = 0 WHERE LEFT(iid, 1) = 'M'"

struct bt_mbet_event_ids {
    long int *matches;
    size_t count;
};

static const char *const bt_mbet_live_queries[] = {
    "INSERT INTO partidos (id, fecha, J1, J2, torneo, ronda, estado, mbet) VALUES %values% ON DUPLICATE KEY UPDATE estado = VALUES(estado), mbet = VALUES(mbet), ronda = VALUES(ronda)",
    "INSERT INTO _sets (_set, event_id, GA, GB) VALUES %values% ON DUPLICATE KEY UPDATE GA = VALUES(GA), GB = VALUES(GB)",
    "INSERT INTO game (_set, juego, event_id, PA) VALUES %values% ON DUPLICATE KEY UPDATE PA = VALUES(PA)",
    "INSERT INTO game (_set, juego, event_id, PB) VALUES %values% ON DUPLICATE KEY UPDATE PB = VALUES(PB)",
    "INSERT INTO servicio (event_id, jugador, _set, juego) VALUES %values% ON DUPLICATE KEY UPDATE jugador = VALUES(jugador)",
    "UPDATE partidos SET estado = 2 WHERE id IN (%values%)"
};
static const char *const bt_mbet_check_markets_query = "INSERT INTO mercado_ganador_partido (iid, tournament, home, away, category, home_price, away_price, created) VALUES %values%";

// Comaprison functions
static int
bt_mbet_intcmp(const void *const lhs, const void *const rhs)
{
    return *(int *) lhs - *(int *) rhs;
}

static void
bt_mbet_check_market_changes(const bt_mbet_event *const event, void *data)
{
    bt_mbet_list *markets;
    bt_mysql_transaction *transaction;
    bt_mysql_operation *operation;
    time_t timestamp;
    char *tournament;

    tournament = data;
    if (tournament == NULL)
        return;
    transaction = bt_mysql_transaction_new(1, 8, bt_mbet_check_markets_query);
    if (transaction == NULL)
        return;
    timestamp = time(NULL);
    operation = bt_transaction_get_operation(transaction, 0);
    markets = event->markets;
    for (size_t idx = 0; idx < markets->count; ++idx) {
        char iid[11];
        int result;
        bt_mbet_market *market;
        bt_mbet_selection *P1;
        bt_mbet_selection *P2;
        bt_mbet_list_item *item;
        bt_mbet_list *selections;

        item = markets->items[idx];
        market = item->data;
        selections = market->selections;
        item = selections->items[0];

        P1 = item->data;
        if (*P1->selkey == 'A') {
            item = selections->items[1];
            P2 = item->data;
        } else {
            item = selections->items[1];
            P1 = item->data;
            item = selections->items[0];
            P2 = item->data;
        }
        result = snprintf(iid, sizeof(iid), "M%08ld", event->tree_id);
        if ((result < 0) || (result >= sizeof(iid))) {
            log("imposible almacenar mercado mbet `%ld'\n", event->tree_id);
            continue;
        }
        bt_mysql_operation_put(operation, "%s%s%s%s%d%f%f%ld", iid, tournament,
          P1->name, P2->name, event->category, P1->coeff, P2->coeff, timestamp);
    }
    bt_mysql_transaction_execute(transaction);
    bt_mysql_transaction_free(transaction);

    bt_check_drops();
    bt_mysql_execute_query(UPDATE_QUERY);
}

static void
bt_mbet_save_score(const bt_mbet_event *const event, const bt_mbet_score *const score, bt_mysql_transaction *transaction)
{
    bt_mysql_operation *operation;
    const bt_mbet_score_item *game;
    const bt_mbet_score_item *set;
    int gameno;
    // Sanity check
    if ((score->nsets == 0) || (score->sets == NULL))
        return;
    // Get the second operation (the sets score) from the `bt_mysql_transaction`
    operation = bt_transaction_get_operation(transaction, 1);
    if (operation == NULL)
        return;
    // Make a pointer to the game score (the points)
    game = &score->game;
    // Make a pointer to the last set score (the games)
    set = &score->sets[score->nsets - 1];
    // Infere the game number
    gameno = set->home + set->away + 1;
    // Iterate through all the sets and insert the score into the
    // database
    for (int i = 0; i < score->nsets; ++i) {
        bt_mbet_score_item *set;
        // Make a pointer to the i-th  set
        set = &score->sets[i];
        // Insert this score into the operation object
        bt_mysql_operation_put(operation,
           "%d%ld%d%d", i + 1, event->tree_id, set->home, set->away);
    }
    // Get the third operation (the game score) from the `bt_mysql_transaction`
    operation = bt_transaction_get_operation(transaction, 2);
    if (operation == NULL)
        return;
    // Insert current game for the first player
    bt_mysql_operation_put(operation, "%d%d%ld%d",
                          score->nsets, gameno, event->tree_id, game->home);
    operation = bt_transaction_get_operation(transaction, 3);
    if (operation == NULL)
        return;
    // Insert current game for the first second
    bt_mysql_operation_put(operation, "%d%d%ld%d",
                          score->nsets, gameno, event->tree_id, game->away);
    // Get the fifth operation (the service) from the `bt_mysql_transaction`
    operation = bt_transaction_get_operation(transaction, 4);
    if (operation == NULL)
        return;
    // Update who is serving now
    bt_mysql_operation_put(operation, "%ld%d%d%d",
                      event->tree_id, score->service, score->nsets, gameno);
}

static void
bt_mbet_handle_sport(const bt_mbet_sport *const sport,
                                   bt_mbet_sport_handler_fn handler, void *data)
{
    handler(sport, data);
}

static void
bt_mbet_save_match(const bt_mbet_event *const event, void *data)
{
    int status;
    bt_mysql_operation *operation;
    bt_mysql_transaction *transaction;
    bt_mbet_member *home;
    bt_mbet_member *awya;
    char date[11];
    transaction = data;
    // Make a pointer to the appropriate operation object
    operation = bt_transaction_get_operation(transaction, 0);
    if (operation == NULL)
        return;
    home = event->home;
    awya = event->away;
    // Store the match time
    strftime(date, sizeof(date), "%Y-%m-%d", &event->date);
    if ((home->ocid == -1) || (awya->ocid == -1) || (event->octour == -1))
        return;
    // Check if the match is running
    status = 1;
    // Store the result into the database
    bt_mysql_operation_put(operation, "%ld%s%d%d%d%d%d%s",
        event->tree_id,   // The event id
        date,             // Event date as per the XML
        home->ocid,       // The first player id
        awya->ocid,       // The second player id
        event->octour,    // The tournament id (oncourt database)
        event->ocround,   // The match round
        status,           // Match status (inferred from the XML)
        event->url        // The www.mbet.org URL
    );
    // Save the score now
    bt_mbet_save_score(event, event->score, transaction);
}

static void
bt_mbet_pre_sport_handler(const bt_mbet_sport *const sport, void *data)
{
    bt_mbet_generic_sport_handler(sport, bt_mbet_check_market_changes, data);
}

static void
bt_mbet_live_sport_handler(const bt_mbet_sport *const sport, void *data)
{
    bt_mbet_generic_sport_handler(sport, bt_mbet_save_match, data);
}

static void
bt_mbet_live_result_check_handicap(int match)
{
    // TODO: implement this function
    (void) match;
}

static void
bt_mbet_append_event(const bt_mbet_event *const event, void *data)
{
    struct bt_mbet_event_ids *events;
    events = data;

    events->matches[events->count++] = event->tree_id;
}

static long *
bt_mbet_get_matches_ids(const bt_mbet_feed *const live, size_t *count)
{
    size_t total;
    struct bt_mbet_event_ids events;
    bt_mbet_list *sports;
    events.count = 0;
    // Make a poitner to the sports object
    sports = live->sports;
    // Set this to 0 so the caller can check
    *count = 0;
    // Get the total event count
    total = bt_mbet_count_events(live);
    if (total == 0)
        return NULL;
    // Allocate the necessary space for the integer list
    events.matches = bt_malloc(total * sizeof(*events.matches));
    if (events.matches == NULL)
        return NULL;
    // Iterate again, but this time saving match ids
    for (size_t sdx = 0; sdx < sports->count; ++sdx) {
        bt_mbet_list_item *item;
        item = sports->items[sdx];
        if (item == NULL)
            continue;
        bt_mbet_generic_sport_handler(item->data, bt_mbet_append_event, &events);
    }
    // Sort the array
    qsort(events.matches, events.count, sizeof(*events.matches), bt_mbet_intcmp);
    // Assign the total count
    *count = events.count;
    // Return the array
    return events.matches;
}

static void
bt_mbet_update_finished_matches(const bt_mbet_feed *const live, bt_mysql_operation *operation)
{
    size_t count;
    const char *query;
    long *list;
    long int match;
    MYSQL_STMT *stmt;
    // Get all the ids of the 'live' matches
    list = bt_mbet_get_matches_ids(live, &count);
    if (list == NULL)
        return;
    query = "SELECT id FROM partidos WHERE estado = 1";
    stmt = bt_mysql_easy_query(query, "|%ld", &match);
    if (stmt == NULL)
        goto error;
    // Iterate for each id in this result set
    while (mysql_stmt_fetch(stmt) == 0) {
        long int *value;
        // Fidn this id in the list
        value = bsearch(&match, list, count, sizeof(*list), bt_mbet_intcmp);
        // Skip this item, because it's still in the live
        // feed xml, thus it's "LIVE"
        if (value != NULL)
            continue;
        // This was not in the list, update it's status
        bt_mysql_operation_put(operation, "%ld", match);
        // Notify if the handicap for the match wasn't met
        bt_mbet_live_result_check_handicap(match);
    }
    // Release resources
    mysql_stmt_free_result(stmt);
error:
    if (stmt != NULL)
        mysql_stmt_close(stmt);
    // Release resources
    bt_free(list);
}

static void
bt_mbet_live_handler(const bt_mbet_feed *const live)
{
    bt_mbet_list *sports;
    bt_mysql_transaction *transaction;
    bt_mysql_operation *operation;
    sports = live->sports;
    if (sports == NULL)
        return;
    // Make a transaction object to execute all these
    // queries and set parameters easily and efficiently.
    transaction = bt_mysql_transaction_new(
        6,
        8, bt_mbet_live_queries[0],
        4, bt_mbet_live_queries[1],
        4, bt_mbet_live_queries[2],
        4, bt_mbet_live_queries[3],
        4, bt_mbet_live_queries[4],
        1, bt_mbet_live_queries[5]
    );
    if (transaction == NULL)
        return;
    // Handle each sport in the lsit
    for (size_t idx = 0; idx < sports->count; ++idx) {
        bt_mbet_sport *sport;
        // Make a pointer
        sport = bt_mbet_list_get_item_data(live->sports, idx);
        // Note, there is no need to execute any query
        // for the PreMatchFeed, so the transaction can
        // be anything since it will not be accessed.
        //
        // But just because I am extremely paranoid, id
        // set it to `NULL` anyway to account for future
        // changes or all the things that may be missed
        bt_mbet_handle_sport(sport, bt_mbet_live_sport_handler, transaction);
    }
    // This operation is just for the finished matches
    operation = bt_transaction_get_operation(transaction, 5);
    if (operation != NULL) {
        // Insert all finished matches id's into the operation obejct
        bt_mbet_update_finished_matches(live, operation);
    }
    // Execute the transation
    bt_mysql_transaction_execute(transaction);
    // Release resources
    bt_mysql_transaction_free(transaction);
}

static void
bt_mbet_pre_handler(const bt_mbet_feed *const live)
{
    bt_mbet_list *sports;
    sports = live->sports;
    if (sports == NULL)
        return;
    // Handle each sport in the lsit
    for (size_t idx = 0; idx < sports->count; ++idx) {
        bt_mbet_sport *sport;
        // Make a pointer
        sport = bt_mbet_list_get_item_data(live->sports, idx);
        // Note, there is no need to execute any query
        // for the PreMatchFeed, so the transaction can
        // be anything since it will not be accessed.
        //
        // But just because I am extremely paranoid, id
        // set it to `NULL` anyway to account for future
        // changes or all the things that may be missed
        bt_mbet_handle_sport(sport, bt_mbet_pre_sport_handler, NULL);
    }
}

void *
bt_mbet_feed_live(void *context)
{
    // Initialize MySQL database resources
    bt_database_initialize();
    // Start the main loop
    while (bt_isrunning(context) == true) {
        bt_mbet_feed *feed;
        // Make an update from the feed data
        feed = bt_mbet_feed_get(LiveFeed, bt_mbet_live_handler);
        if (feed != NULL) {
            bt_mbet_feed_free(feed);
        }
        // Wait 5 seconds for the next request
        bt_sleep(5);
    }
    // Release MySQL connection resources
    bt_database_finalize();
    bt_notify_thread_end();
    return NULL;
}

void *
bt_mbet_feed_prematch(void *context)
{
    // Initialize MySQL database resources
    bt_database_initialize();
    // Start the main loop
    while (bt_isrunning(context) == true) {
        bt_mbet_feed *feed;
        // Make an update from the feed data
        feed = bt_mbet_feed_get(PreMatchFeed, bt_mbet_pre_handler);
        if (feed != NULL)
            bt_mbet_feed_free(feed);
        // Wait five minutes for the next request
        bt_sleep(300);
    }
    // Release MySQL connection resources
    bt_database_finalize();
    bt_notify_thread_end();
    return NULL;
}

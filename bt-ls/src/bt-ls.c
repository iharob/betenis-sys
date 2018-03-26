#include <string.h>
#include <stdio.h>

#include <unistd.h>
#include <bt-memory.h>

#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include <time.h>

#include <pthread.h>
#include <poll.h>
#include <stdint.h>

#include <fcntl.h>

#include <json.h>

#include <bt-private.h>
#include <bt-mbet.h>
#include <bt-mbet-feed.h>
#include <bt-database.h>
#include <bt-mbet-score.h>

#include <mongoc.h>
#include <bson.h>

#include <execinfo.h>

#include <signal.h>
#include <errno.h>

#define CLIENT_POOL_SIZE 0x400
#define bt_ls_display_error() do { if ((errno != 0) && (errno != EPIPE)) { fprintf(stderr, "%5d:%s/%d: %s\n", errno, __FUNCTION__, __LINE__, strerror(errno)); errno = 0; } } while (0)
#define MATCHES_QUERY_FORMAT "|%66a%d%4a%127a%d%4a%d%61a%lf%d%4a%d%61a%lf%d%d%32a"
#define NO_PREFIX '\0'
#define TS_TO_SECONDS(x) ((x).tv_sec + (double) (x).tv_nsec / 1.0E9)

static const char *ETX = (char []) {0x03};

typedef enum bt_command {
    InvalidCommand = -1,
    StartCommand = 1,
    PointsHistorical,
    Head2Head
} bt_command;

typedef struct bt_command_descriptor {
    const char *name;
    bt_command id;
} bt_command_descriptor;

static bt_command_descriptor bt_commands[] = {
    {"start", StartCommand},
};

#define CMDCOUNT sizeof(bt_commands) / CMDSIZE
#define CMDSIZE sizeof(bt_command_descriptor)

typedef enum bt_ls_player_idx {
    Home, Away, PlayerCount
} bt_ls_player_idx;

typedef struct bt_ls_hash_table {
    const struct bt_mbet_event **events;
    size_t count;
} bt_ls_hash_table;

typedef struct bt_ls_handler_ctx {
    size_t count;
    const bt_ls_hash_table *htbl;
    json_object *object;
} bt_ls_handler_ctx;

typedef struct bt_ls_client {
    int fd;
    bool listening;
    // FIXME: we should place here what the client
    //        is listening to.
} bt_ls_client;

typedef struct bt_ls_context {
    bt_ls_client *clients;
    bt_mbet_feed *live;
    bt_mbet_feed *next;
    bt_ls_hash_table *htbl;
    size_t client_pool_size;
    size_t clients_count;
} bt_ls_context;

static pthread_mutex_t bt_ls_mutex;
typedef json_object *(*bt_ls_feed_handler)(const bt_ls_context *const ctx, const bt_ls_context *const cached);

typedef struct bt_ls_player {
    int rank;
    double odds;
    char name[61];
    char flag[4];
    int id;
} bt_ls_player;

typedef struct bt_ls_tour {
    int id;
    char name[66];
    char flag[4];
    char court[128];
    int rank;
    int round;
} bt_ls_tour;

typedef struct bt_ls_event {
    char result[32];
    bt_ls_player home;
    bt_ls_player away;
    bt_ls_tour tour;
    bt_tennis_category category;
} bt_ls_event;

typedef union bt_ls_json_value {
    const char *string;
    int integer;
    double real;
    bool boolean;
} bt_ls_json_value;

typedef enum bt_ls_json_type {
    JsonString, JsonInteger, JsonReal, JsonBoolean
} bt_ls_json_type;

static json_object *bt_ls_update_handler(const bt_ls_context *const ctx, const bt_ls_context *const cached);
static json_object *bt_ls_delete_handler(const bt_ls_context *const ctx, const bt_ls_context *const cached);
static json_object *bt_ls_append_handler(const bt_ls_context *const ctx, const bt_ls_context *const cached);
typedef json_object *(bt_ls_initfn)(const bt_ls_context *const ls);

static const bt_ls_feed_handler handlers[] = {
    bt_ls_update_handler,
    bt_ls_delete_handler,
    bt_ls_append_handler,
    NULL
};

static int
bt_ls_make_id(char *target, size_t size, long long int number, char prefix)
{
    int length;
    if (prefix != '\0') {
        length = snprintf(target, size, "%c%lld", prefix, number);
    } else {
        length = snprintf(target, size, "%lld", number);
    }
    if ((length < 0) || (length >= size))
        return -1;
    return length;
}

static int
bt_ls_make_real(char *buffer, size_t size, double value)
{
    int length;
    length = snprintf(buffer, size, "%.2f", value);
    if ((length == -1) || (length >= size))
        return -1;
    return 0;
}

static int
bt_ls_json_add_value(json_object *object, const char *const key, bt_ls_json_type type, bt_ls_json_value *value)
{
    json_object *jval;
    char buffer[256];
    switch (type) {
    case JsonString:
        jval = json_object_new_string(value->string);
        break;
    case JsonInteger:
        jval = json_object_new_int(value->integer);
        break;
    case JsonReal:
        if (bt_ls_make_real(buffer, sizeof(buffer), value->real) == -1)
            return -1;
        jval = json_object_new_double(value->real);
        break;
    case JsonBoolean:
        jval = json_object_new_boolean(value->boolean);
        break;
    default:
        jval = NULL;
        break;
    }

    if (jval == NULL)
        return -1;
    json_object_object_add(object, key, jval);
    return 0;
}

static int
bt_ls_json_add_string(json_object *object, const char *const key, const char *const string)
{
    bt_ls_json_value value;
    value.string = string;
    return bt_ls_json_add_value(object, key, JsonString, &value);
}

static int
bt_ls_json_add_integer(json_object *object, const char *const key, int integer)
{
    bt_ls_json_value value;
    value.integer = integer;
    return bt_ls_json_add_value(object, key, JsonInteger, &value);
}

static int
bt_ls_json_add_real(json_object *object, const char *const key, double real)
{
    bt_ls_json_value value;
    value.real = real;
    return bt_ls_json_add_value(object, key, JsonReal, &value);
}

static int
bt_ls_json_add_boolean(json_object *object, const char *const key, bool boolean)
{
    bt_ls_json_value value;
    value.boolean = boolean;
    return bt_ls_json_add_value(object, key, JsonBoolean, &value);
}

static void
bt_ls_reset_socket(const char *const path)
{
    struct stat st;
    // TODO: check whether it belongs to an active
    //       process or it's a stale socket
    if (stat(path, &st) == -1)
        return;
    unlink(path);
}

static int
bt_ls_append_client(bt_ls_context *ls, int fd)
{
    bt_ls_client client;
    size_t count;
    count = ls->clients_count;
    if (count >= ls->client_pool_size) {
        size_t new_count;
        void *pointer;
        new_count = ls->client_pool_size + CLIENT_POOL_SIZE;
        pointer = bt_realloc(ls->clients, new_count * sizeof(*ls->clients));
        if (pointer == NULL)
            return -1;
        ls->clients = pointer;
        ls->client_pool_size = new_count;
    }
    client.fd = fd;
    client.listening = false;

    ls->clients[count] = client;
    ls->clients_count = count + 1;
    return 0;
}

static void
bt_ls_remove_client(bt_ls_context *ls, bt_ls_client *client)
{
    for (size_t idx = 0; idx < ls->clients_count; ++idx) {
        bt_ls_client *next;
        bt_ls_client *current;
        size_t right;
        // If it's just the last element, there is no
        // need to move memory
        if (idx < ls->clients_count - 1) {
            // How many elements are there to the right?
            //
            // NOTE: this can be 0 and it's perfectly legal
            right = (ls->clients_count - idx - 1) * sizeof(*current);
            // Which on is the next element?
            next = &ls->clients[idx + 1];
            // Make a pointer to current element
            current = &ls->clients[idx];
            if (current->fd != client->fd)
                continue;
            // Close the file descriptor before overwriting it
            close(client->fd);
            // Update the array and "remove" the requested item
            memmove(current, next, right);
            // Update the array count
        }
        ls->clients_count -= 1;
        break;
    }
}

static int
bt_ls_make_socket(const char *const path)
{
    int flags;
    socklen_t socklen;
    struct sockaddr_un sa;
    int sock;
    // Ensure that bind will succeed
    bt_ls_reset_socket(path);
    // Create the socket
    sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock == -1)
        return -1;
    // Family is Unix Domain Socket
    sa.sun_family = AF_UNIX;
    // The length pf the socket file path
    socklen = strlen(path);
    // Copy the path string
    memcpy(sa.sun_path, path, socklen + 1);
    // Update the length
    socklen += sizeof(sa.sun_family);
    // Attempt to bind
    if (bind(sock, (struct sockaddr *) &sa, socklen) == -1)
        goto error;
    // Start listening for connections
    if (listen(sock, 0x4000) == -1)
        goto error;
    // Make it non-blocing
    flags = fcntl(sock, F_GETFL, 0);
    if (fcntl(sock, F_SETFL, flags | O_NONBLOCK) == -1)
        goto error;
    // We're ready, return the new socket
    return sock;
error:
    bt_ls_display_error();
    close(sock);
    return -1;
}

static bool
bt_ls_handle_client(const bt_ls_client *const client, int server)
{
    char buffer[0x4000];
    ssize_t size;
    if (client == NULL)
        return false;
    size = recv(server, buffer, sizeof(buffer), 0);
    if ((size <= 0) || (send(client->fd, buffer, size, MSG_NOSIGNAL) == -1)) {
        bt_ls_display_error();
        return false;
    }
    return true;
}

ssize_t
bt_ls_send_message(int fd, const char *const string, size_t length)
{
    ssize_t result;
    while (length > 0) {
        result = send(fd, string, length, MSG_NOSIGNAL | MSG_DONTWAIT);
        if (result == -1)
            goto error;
        length -= result;
    }
    result = send(fd, ETX, 1, MSG_NOSIGNAL | MSG_DONTWAIT);
error:
    if (result == -1)
        bt_ls_display_error();
    return result;
}

json_object *
bt_ls_json_object_merge(json_object *object, const char *const key, bool *existed)
{
    json_object *element;
    if (json_object_object_get_ex(object, key, &element) == false) {
        element = json_object_new_object();
        if (element == NULL)
            return NULL;
        json_object_object_add(object, key, element);

        *existed = false;
    } else {
        *existed = true;
    }
    return element;
}

json_object *
bt_ls_make_json_from_score(const bt_mbet_score *const score, int pdx)
{
    const bt_mbet_score_item *item;
    int8_t value;
    json_object *result;
    result = json_object_new_object();
    if (result == NULL)
        return NULL;
    for (int idx = 0; idx < score->nsets; ++idx) {
        char number[32];
        if (bt_ls_make_id(number, sizeof(number), idx + 1, NO_PREFIX) == -1)
            continue;
        item = &score->sets[idx];
        value = (pdx == 0) ? item->home : item->away;
        if (bt_ls_json_add_integer(result, number, value) == 0)
            continue;
        goto error;
    }
    item = &score->game;
    value = (pdx == 0) ? item->home : item->away;
    if (bt_ls_json_add_integer(result, "g", value) == -1)
        goto error;
    if (bt_ls_json_add_boolean(result, "s", score->service == pdx + 1) == -1)
        goto error;
    item = &score->score;
    value = (pdx == 0) ? item->home : item->away;
    if (bt_ls_json_add_boolean(result, "r", value) == -1)
        goto error;
    return result;
error:
    json_object_put(result);
    return NULL;
}

json_object *
bt_ls_get_score(const bt_mbet_score *const score)
{
    json_object *home;
    json_object *object;
    json_object *away;

    object = json_object_new_object();
    if (score == NULL)
        return object;

    home = bt_ls_make_json_from_score(score, Home);
    away = bt_ls_make_json_from_score(score, Away);

    json_object_object_add(object, "h", home);
    json_object_object_add(object, "a", away);

    return object;
}

static const char *
bt_ls_get_category(bt_tennis_category category)
{
    if ((category & CategoryATP) == CategoryATP) {
        return "atp";
    } else if ((category & CategoryWTA) == CategoryWTA) {
        return "wta";
    }
    return NULL;
}

static json_object *
bt_ls_make_player(bt_mbet_member *member)
{
    json_object *object;
    const char *category;
    object = json_object_new_object();
    if (object == NULL)
        return NULL;
    if (bt_ls_json_add_string(object, "n", member->name) == -1)
        goto error;
    if (bt_ls_json_add_string(object, "f", member->flag) == -1)
        goto error;
    if (bt_ls_json_add_integer(object, "i", member->ocid) == -1)
        goto error;
    category = bt_ls_get_category(member->category);
    if (category == NULL)
        goto error;
    if (bt_ls_json_add_string(object, "c", category) == -1)
        goto error;
    if (bt_ls_json_add_integer(object, "r", member->ranking) == -1)
        goto error;
    if (bt_ls_json_add_real(object, "o", member->odds) == -1)
        goto error;
    return object;
error:
    json_object_put(object);
    return NULL;
}

static int
bt_ls_extract_odss(bt_mbet_market *market, float *home, float *away)
{
    bt_mbet_list *list;
    bt_mbet_selection *selection[2];
    bt_mbet_list_item *item;
    list = market->selections;
    if ((list == NULL) || (list->count != 2))
        return -1;
    item = list->items[0];
    selection[0] = item->data;
    item = list->items[1];
    selection[1] = item->data;

    if (selection[0]->selkey[0] == 'H') {
        *home = selection[0]->coeff;
        *away = selection[1]->coeff;
    } else {
        *home = selection[1]->coeff;
        *away = selection[0]->coeff;
    }
    return 0;
}

static int
bt_ls_get_odds(const bt_mbet_event *const event, float *home, float *away)
{
    bt_mbet_list *list;

    *home = 0.0;
    *away = 0.0;

    list = event->markets;
    if (list == NULL)
        return -1;
    for (size_t idx = 0; idx < list->count; ++idx) {
        bt_mbet_list_item *item;
        bt_mbet_market *market;
        item = list->items[idx];
        if (item == NULL)
            return -1;
        market = item->data;
        if (market == NULL)
            return -1;
        if (strcmp(market->model, "MTCH_R") != 0)
            continue;
        return bt_ls_extract_odss(market, home, away);
    }
    return 0;
}

static int
bt_ls_get_set_count(int rank, bt_tennis_category category)
{
    if ((category & CategoryWTA) != 0)
        return 3;
    switch (rank) {
    case 4:
    case 5:
        return 5;
    }
    return 3;
}

static const char *
bt_ls_get_round_name(int id)
{
    const char *round;
    round = "";
    switch (id) {
    case 0:
        round = "Pre";
        break;
    case 1:
        round = "Q-1";
        break;
    case 2:
        round = "Q-2";
        break;
    case 3:
        round = "Qualy";
        break;
    case 4:
        round = "R32";
        break;
    case 5:
        round = "Octavos";
        break;
    case 6:
        round = "R32";
        break;
    case 7:
        round = "Octavos";
        break;
    case 8:
        round = "Round Robin";
        break;
    case 9:
        round = "Cuartos";
        break;
    case 10:
        round = "Semi Final";
        break;
    case 12:
        round = "Final";
        break;
    }
    return round;
}

float
bt_ls_get_tournament_altitude(int id_t)
{
    bson_t item;
    bson_t query;
    bson_t project;
    bson_t match;
    float value;
    mongoc_client_t *mongo;
    mongoc_collection_t *collection;
    mongoc_cursor_t *cursor;
    const bson_t *result;

    mongo = mongoc_client_new("mongodb://127.0.0.1");
    collection = NULL;
    cursor = NULL;
    if (mongo == NULL)
        return 0.0;
    bson_init(&query);
    bson_init(&project);
    bson_init(&match);
    bson_init(&item);
    if (bson_append_document_begin(&query, "0", 1, &match) == false)
        goto error;
    if (bson_append_document_begin(&match, "$match", 6, &item) == false)
        goto error;
    if (bson_append_int32(&item, "id_t", 4, id_t) == false)
        goto error;
    if (bson_append_document_end(&match, &item) == false)
        goto error;
    if (bson_append_document_end(&query, &match) == false)
        goto error;
    if (bson_append_document_begin(&query, "1", 1, &project) == false)
        goto error;
    if (bson_append_document_begin(&project, "$project", 8, &item) == false)
        goto error;
    if (bson_append_utf8(&item, "elevation", 9, "$results.elevation", 18) == false)
        goto error;
    if (bson_append_document_end(&project, &item) == false)
        goto error;
    if (bson_append_document_end(&query, &project) == false)
        goto error;

    collection = mongoc_client_get_collection(mongo, "elevation", "tournaments");
    if (collection == NULL)
        goto error;
    cursor = mongoc_collection_aggregate(collection,
                                         MONGOC_QUERY_NONE, &query, NULL, NULL);
    if (mongoc_cursor_next(cursor, &result) != 0) {
        bson_iter_t elevation;
        bson_iter_t array;
        if (bson_iter_init_find(&array, result, "elevation") == false)
            goto error;
        if (bson_iter_recurse(&array, &elevation) == false)
            goto error;
        if (bson_iter_next(&elevation) == false)
            goto error;
        value = bson_iter_double(&elevation);
    } else {
        value = -1.0E5F;
    }
    mongoc_cursor_destroy(cursor);
    mongoc_collection_destroy(collection);
    mongoc_client_destroy(mongo);

    bson_destroy(&match);
    bson_destroy(&project);
    bson_destroy(&query);
    bson_destroy(&item);

    return value;
error:
    if (collection != NULL)
        mongoc_collection_destroy(collection);
    if (cursor != NULL)
        mongoc_cursor_destroy(cursor);
    mongoc_client_destroy(mongo);

    bson_destroy(&match);
    bson_destroy(&project);
    bson_destroy(&query);
    bson_destroy(&item);
    return -1.0E5F;
}

static json_object *
bt_ls_add_event_get_group(const bt_mbet_event *const event, json_object *container)
{
    bool existed;
    json_object *tour;
    json_object *category;
    char id[32];
    const bt_mbet_group *group;
    int set_count;
    float altitude;

    group = event->group;
    if ((group == NULL) || (group->name == NULL))
        return NULL;
    if ((event->category & CategoryChallenger) == CategoryChallenger) {
        category = bt_ls_json_object_merge(container, "Challenger", &existed);
    } else if ((event->category & CategoryITF) == CategoryITF) {
        category = bt_ls_json_object_merge(container, "ITF", &existed);
    } else if ((event->category & CategoryATP) == CategoryATP) {
        category = bt_ls_json_object_merge(container, "ATP", &existed);
    } else if ((event->category & CategoryWTA) == CategoryWTA) {
        category = bt_ls_json_object_merge(container, "WTA", &existed);
    } else {
        return NULL;
    }

    if (category == NULL)
        return NULL;
    if (bt_ls_make_id(id, sizeof(id), group->tree_id, NO_PREFIX) == -1)
        return NULL;
    tour = bt_ls_json_object_merge(category, id, &existed);
    if (tour == NULL)
        goto error;
    if (existed == false) {
        const char *category;
        const char *round;
        round = bt_ls_get_round_name(group->ocround);
        if (bt_ls_json_add_string(tour, "r", round) == -1)
            goto error;
        if (bt_ls_json_add_string(tour, "n", group->name) == -1)
            goto error;
        if (bt_ls_json_add_string(tour, "c", group->court) == -1)
            goto error;
        altitude = bt_ls_get_tournament_altitude(group->ocid);
        if (altitude > -1.0E5) {
            if (bt_ls_json_add_real(tour, "a", altitude) == -1)
                goto error;
        } else {
            if (bt_ls_json_add_string(tour, "a", "") == -1)
                goto error;
        }
        if (bt_ls_json_add_integer(tour, "id", group->ocid) == -1)
            goto error;
        if (bt_ls_json_add_string(tour, "f", group->flag) == -1)
            goto error;
        set_count = bt_ls_get_set_count(group->ocrank, group->category);
        if (bt_ls_json_add_integer(tour, "sc", set_count) == -1)
            goto error;
        category = bt_ls_get_category(group->category);
        if (category == NULL)
            goto error;
        if (bt_ls_json_add_string(tour, "q", category) == -1)
            goto error;
    }
    return bt_ls_json_object_merge(tour, "e", &existed);
error:
    json_object_put(tour);
    return NULL;
}

static int
bt_ls_update_odds(const bt_mbet_event *const event, float home, float away)
{
    char *query;
    bt_mbet_member *members[PlayerCount];
    const bt_mbet_group *group;
    const char *category;
    MYSQL_STMT *stmt;
    members[Home] = event->home;
    members[Away] = event->away;
    if ((members[Home]->odds >= 1.0) && (members[Away]->odds >= 1.0))
        return 0;
    category = bt_ls_get_category(event->category);
    if (category == NULL)
        return 0;
    group = event->group;
    query = bt_load_query("insert odds", "%category%", category, NULL);
    if (query == NULL)
        return -1;
    stmt = bt_mysql_easy_query(query, "%d%d%d%d%f%f",
        //     Player 1           Player 2
        &members[Home]->ocid, &members[Away]->ocid,
        // TourId        RoundId
        &group->ocid, &group->ocround,
        // Odds
        &home, &away
    );
    bt_free(query);
    if (stmt == NULL)
        return -1;
    members[Home]->odds = home;
    members[Away]->odds = away;
    mysql_stmt_close(stmt);
    return 0;
}

static int
bt_ls_create_event_id(json_object *element, const bt_mbet_event *event)
{
    const bt_mbet_member *home;
    const char *category;
    const bt_mbet_member *away;
    char id[32];
    int length;
    home = event->home;
    away = event->away;
    category = bt_ls_get_category(event->category);
    if (category == NULL)
        return -1;
    length = snprintf(id, sizeof(id), "%s.%d.%d.%d.%d", category,
                         event->octour, event->ocround, home->ocid, away->ocid);
    if ((length < 0) || (length >= sizeof(id)))
        return -1;
    return bt_ls_json_add_string(element, "id", id);
}

static int
bt_ls_create_ls_event_id(json_object *element, const bt_ls_event *event)
{
    const bt_ls_tour *tour;
    const bt_ls_player *home;
    const bt_ls_player *away;
    const char *category;
    char id[32];
    int length;
    home = &event->home;
    away = &event->away;
    tour = &event->tour;
    category = bt_ls_get_category(event->category);
    if (category == NULL)
        return -1;
    length = snprintf(id, sizeof(id), "%s.%d.%d.%d.%d", category,
                                                         tour->id, tour->round, home->id, away->id);
    if ((length < 0) || (length >= sizeof(id)))
        return -1;
    return bt_ls_json_add_string(element, "id", id);
}

static void
bt_ls_add_event(const bt_mbet_event *const event, void *data)
{
    char time[256];
    char id[32];
    json_object *events;
    json_object *element;
    json_object *object;
    json_object *players;
    float odds[PlayerCount];

    object = data;
    // Get events for this group
    events = bt_ls_add_event_get_group(event, object);
    if (events == NULL)
        return;
    // The root element
    element = json_object_new_object();
    if (element == NULL)
        return;
    // Make the ID
    if (bt_ls_make_id(id, sizeof(id), event->tree_id, NO_PREFIX) == -1)
        goto error;
    json_object_object_add(events, id, element);
    // Add Marathon bet odds from XML
    object = json_object_new_object();
    if (object == NULL)
        goto error;
    json_object_object_add(element, "o", object);
    if (bt_ls_get_odds(event, &odds[Home], &odds[Away]) == 0) {
        if (bt_ls_json_add_real(object, "h", odds[Home]) == -1)
            goto error;
        if (bt_ls_json_add_real(object, "a", odds[Away]) == -1)
            goto error;
    }
    // Create the players object
    players = json_object_new_object();
    if (players == NULL)
        return;
    // Update odds in case they are not set
    if (bt_ls_update_odds(event, odds[Home], odds[Away]) == -1)
        goto error;
    // Home member
    object = bt_ls_make_player(event->home);
    if (object == NULL)
        goto error;
    if (bt_ls_create_event_id(element, event) == -1)
        goto error;
    json_object_object_add(players, "h", object);
    // Away member
    object = bt_ls_make_player(event->away);
    if (object == NULL)
        goto error;
    json_object_object_add(players, "a", object);
    // Add players
    json_object_object_add(element, "p", players);
    // Build ISO date/time string
    if (strftime(time, sizeof(time), "%Y-%m-%d %H:%M:%S UTC", &event->date) == 0)
        goto error;
    // Add it to the element
    if (bt_ls_json_add_string(element, "t", time) == -1)
        goto error;
    // Get the score object
    object = bt_ls_get_score(event->score);
    if (object == NULL)
        goto error;
    json_object_object_add(element, "s", object);
    return;
error:
    json_object_put(element);
}

const bt_mbet_event *
bt_ls_find_event(const bt_mbet_event *const event, const bt_ls_hash_table *const ht)
{
    const bt_mbet_event **found;
    if (ht == NULL)
        return NULL;
    found = bsearch(&event, ht->events, ht->count, sizeof(event), bt_mbet_eventcmp);
    if (found == NULL)
        return NULL;
    return *found;
}

static void
bt_ls_append_event(const bt_mbet_event *const event, void *data)
{
    bt_ls_handler_ctx *ctx;
    // Make a pointer to the context
    ctx = data;
    // Find this event in the old table
    if (bt_ls_find_event(event, ctx->htbl) != NULL)
        return;
    // Add the event to the object
    bt_ls_add_event(event, ctx->object);
    // Update this so we know that there is something to return
    ctx->count += 1;
}

static void
bt_ls_remove_event(const bt_mbet_event *const event, void *data)
{
    json_object *array;
    bt_ls_handler_ctx *ctx;
    json_object *value;
    // Make a pointer to the context
    ctx = data;
    // Find this event in the new table
    if (bt_ls_find_event(event, ctx->htbl) != NULL)
        return;
    // If it was not found, add it to the "delete" array
    // Check that the array already exists
    if (json_object_object_get_ex(ctx->object, "l", &array) == false) {
        // It did not, so create it
        array = json_object_new_array();
        if (array == NULL)
            return;
        // Add the array to the object
        json_object_object_add(ctx->object, "l", array);
    }
    // Make the json value with the event id
    value = json_object_new_int64(event->tree_id);
    if (value == NULL)
        return;
    // Insert the value in the array
    json_object_array_add(array, value);
    // Update this so we know that there is something to return
    ctx->count += 1;
}

static json_object *
bt_ls_new(bt_mbet_feed *feed, const char *const method)
{
    bt_mbet_list *sports;
    json_object *object;
    if (feed == NULL)
        return NULL;
    // Make a new json object to store the result
    object = json_object_new_object();
    if (object == NULL)
        return NULL;
    // Set the object method to let the client choose an action
    if (bt_ls_json_add_string(object, "mt", method) == -1)
        goto error;
    // Make a pointer to the sports in the feed
    sports = feed->sports;
    if (sports == NULL)
        return object;
    // Start iterating for each sport
    for (size_t sdx = 0; sdx < sports->count; ++sdx) {
        bt_mbet_list_item *item;
        // Make a pointer to the list item
        item = sports->items[sdx];
        if (item == NULL)
            continue;
        // Apply the handler to every event
        bt_mbet_generic_sport_handler(item->data, bt_ls_add_event, object);
    }
    // Return the resulting object
    return object;
error:
    // Addnig the method string failed
    json_object_put(object);
    return NULL;
}

static int
bt_ls_score_valuecmp(const char *const group, const char *const key,
                                  int8_t old, int8_t n3w, json_object *result)
{
    json_object *target;
    if (json_object_object_get_ex(result, group, &target) == false)
        return -1;
    if (old == n3w)
        return 0;
    if (bt_ls_json_add_integer(target, key, n3w) == -1)
        return -1;
    return 0;
}

static int
bt_ls_score_itemcmp(const char *const key, const bt_mbet_score_item *const old,
                       const bt_mbet_score_item *const n3w, json_object *result)
{
    if (bt_ls_score_valuecmp("h", key, old->home, n3w->home, result) == -1)
        return -1;
    if (bt_ls_score_valuecmp("a", key, old->away, n3w->away, result) == -1)
        return -1;
    return 0;
}

static json_object *
bt_ls_compare_scores(const bt_mbet_score *const old, const bt_mbet_score *const n3w)
{
    json_object *home;
    json_object *score;
    json_object *away;
    bt_mbet_score_item *oldset;
    bt_mbet_score_item *n3wset;
    bool existed;
    char set[32];
    // Wierd situation

    if ((old->sets == NULL) || (n3w->sets == NULL))
        return NULL;
    // The root score object
    score = json_object_new_object();
    if (score == NULL)
        return NULL;
    // Create and add the `home' score object
    home = bt_ls_json_object_merge(score, "h", &existed);
    if (home == NULL)
        goto error;
    // Create and add the `away' score object
    away = bt_ls_json_object_merge(score, "a", &existed);
    if (away == NULL)
        goto error;
    // Compare current game
    if (bt_ls_score_itemcmp("g", &old->game, &n3w->game, score) == -1)
        goto error;
    // Make the last set identifier
    if (bt_ls_make_id(set, sizeof(set), old->nsets, NO_PREFIX) == -1)
        goto error;
    // Make poitners to current set
    oldset = &old->sets[old->nsets - 1];
    n3wset = &n3w->sets[old->nsets - 1];
    // If number of sets has changed we MUST add the new one
    if (old->nsets < n3w->nsets) {
        // Update the previous set value (to update the final result)
        if (bt_ls_score_itemcmp(set, oldset, n3wset, score) == -1)
            goto error;
        // Make the new set identifier
        if (bt_ls_make_id(set, sizeof(set), n3w->nsets, NO_PREFIX) == -1)
            goto error;
        // Insert the new value into the home object
        if (bt_ls_json_add_integer(home, set, n3w->sets[old->nsets].home) == -1)
            goto error;
        // Insert the new value into the away object
        if (bt_ls_json_add_integer(away, set, n3w->sets[old->nsets].away) == -1)
            goto error;
    } else if (bt_ls_score_itemcmp(set, oldset, n3wset, score) == -1) {
        goto error;
    }
    return score;
error:
    json_object_put(score);
    return NULL;
}

static int
bt_ls_check_odds(json_object *score,
                 const bt_mbet_event *const old, const bt_mbet_event *const n3w)
{
    float oldodds[2];
    json_object *target;
    float n3wodds[2];
    if (bt_ls_get_odds(old, &oldodds[Home], &oldodds[Away]) == -1)
        return -1;
    if (bt_ls_get_odds(n3w, &n3wodds[Home], &n3wodds[Away]) == -1)
        return -1;
    if ((n3wodds[Home] == oldodds[Home]) && (n3wodds[Away] == oldodds[Away]))
        return 0;
    if (json_object_object_get_ex(score, "h", &target) == false)
        return -1;
    if (bt_ls_json_add_real(target, "o", n3wodds[Home]) == -1)
        return -1;
    if (json_object_object_get_ex(score, "a", &target) == false)
        return -1;
    if (bt_ls_json_add_real(target, "o", n3wodds[Away]) == -1)
        return -1;
    return 0;
}

static void
bt_ls_json_object_delete_if_empty(json_object *object, const char *const key)
{
    json_object *removable;
    if (json_object_object_get_ex(object, key, &removable) == false)
        return;
    if (json_object_object_length(removable) > 0)
        return;
    json_object_object_del(object, key);
}

static int
bt_ls_make_mongo_query(bson_t *query, const bt_mbet_member *const home,
             const bt_mbet_member *const away, const bt_mbet_event *const event)
{
    const char *category;
    // FIXME: why is this here anyway?
    if (event->octour == -1)
        return -1;
    if (bson_append_int32(query, "h", 1, home->ocid) == false)
        goto error;
    if (bson_append_int32(query, "a", 1, away->ocid) == false)
        goto error;
    if (bson_append_int32(query, "t", 1, event->octour) == false)
        goto error;
    if (bson_append_int32(query, "r", 1, event->ocround) == false)
        goto error;
    category = bt_ls_get_category(event->category);
    if (category == NULL)
        goto error;
    if (bson_append_utf8(query, "c", 1, category, -1) == false)
        goto error;
    return 0;
error:
    return -1;
}

static int
bt_ls_insert_points_array(bson_t *object,
                   const bt_mbet_event *event, const bt_mbet_score *const score)
{
    bson_t data;
    const bt_mbet_score_item *item;
    const bt_mbet_score_item *set;
    char id[32];
    float odds[PlayerCount];
    int game;
    int length;

    item = &score->game;
    set = &score->sets[score->nsets - 1];
    game = set->home + set->away + 1;
    // Make the game id
    if (bt_ls_get_odds(event, &odds[Home], &odds[Away]) == -1)
        return -1;
    if (item->home == -2) {
        length = snprintf(id, sizeof(id), "%d.%d.AD%02d",
                                                score->nsets, game, item->away);
    } else if (item->away == -2) {
        length = snprintf(id, sizeof(id), "%d.%d.%02dAD",
                                                score->nsets, game, item->home);
    } else {
        length = snprintf(id, sizeof(id), "%d.%d.%02d%02d",
                                    score->nsets, game, item->home, item->away);
    }
    if ((length == -1) || (length >= sizeof(id)))
        return -1;
    if (bson_append_document_begin(object, id, length, &data) == false)
        return -1;
    if (bson_append_double(&data, "h", 1, odds[Home]) == false)
        return -1;
    if (bson_append_double(&data, "a", 1, odds[Away]) == false)
        return -1;
    if (bson_append_double(&data, "s", 1, score->service) == false)
        return -1;
    if (bson_append_double(&data, "H", 1, set->home) == false)
        return -1;
    if (bson_append_double(&data, "A", 1, set->away) == false)
        return -1;
    if (bson_append_document_end(object, &data) == false)
        return -1;
    return 0;
}

static void
bt_ls_update_mongo_entry_points(mongoc_collection_t *collection,
                                const bson_t *const result, const bt_mbet_event *const event)
{
    bson_error_t error;
    bson_t update;
    bson_t command;
    bson_init(&update);
    if (bson_append_document_begin(&update, "$set", 4, &command) == false)
        goto error;
    if (bt_ls_insert_points_array(&command, event, event->score) == -1)
        goto error;
    if (bson_append_document_end(&update, &command) == false)
        goto error;
    if (mongoc_collection_update(collection,
                MONGOC_UPDATE_UPSERT, result, &update, NULL, &error) == false) {
        fprintf(stderr, "%s\n", error.message);
    }
error:
    bson_destroy(&update);
}

static void
bt_ls_update_mongo_entry(mongoc_collection_t *collection,
                   const bson_t *const result, const bt_mbet_event *const event)
{
    bt_ls_update_mongo_entry_points(collection, result, event);
}

static void
bt_ls_insert_mongo_entry(mongoc_collection_t *collection,
                                               const bt_mbet_event *const event)
{
    const char *category;
    const bt_mbet_member *home;
    const bt_mbet_member *away;    
    bson_error_t error;
    bson_t *object;

    object = bson_new();
    if (object == NULL)
        return;

    home = event->home;
    away = event->away;

    if (bson_append_int32(object, "h", 1, home->ocid) == false)
        goto error;
    if (bson_append_int32(object, "a", 1, away->ocid) == false)
        goto error;
    if (bson_append_int32(object, "t", 1, event->octour) == false)
        goto error;
    if (bson_append_int32(object, "r", 1, event->ocround) == false)
        goto error;
    category = bt_ls_get_category(event->category);
    if (category == NULL)
        goto error;
    if (bson_append_utf8(object, "c", 1, category, -1) == false)
        goto error;
    if (mongoc_collection_insert(collection,
                           MONGOC_INSERT_NONE, object, NULL, &error) == false) {
        fprintf(stderr, "%s\n", error.message);
    }
    bt_ls_update_mongo_entry(collection, object, event);
error:
    bson_destroy(object);
}

static void
bt_ls_save_point2point(const bt_mbet_event *const event)
{
    mongoc_client_t *mongo;
    mongoc_collection_t *collection;
    mongoc_cursor_t *cursor;
    const bson_t *result;
    bson_t query;

    mongo = mongoc_client_new("mongodb://127.0.0.1");
    collection = NULL;
    cursor = NULL;
    if (mongo == NULL)
        return;
    bson_init(&query);

    collection = mongoc_client_get_collection(mongo, "bt", "point2point");
    if (collection == NULL)
        goto error;
    if (bt_ls_make_mongo_query(&query, event->home, event->away, event) == -1)
        goto error;
    cursor = mongoc_collection_find_with_opts(collection, &query, NULL, NULL);
    if (mongoc_cursor_next(cursor, &result) != 0) {
        bt_ls_update_mongo_entry(collection, result, event);
    } else {
        bt_ls_insert_mongo_entry(collection, event);
    }
error:
    if (collection != NULL)
        mongoc_collection_destroy(collection);
    if (cursor != NULL)
        mongoc_cursor_destroy(cursor);
    bson_destroy(&query);

    mongoc_client_destroy(mongo);
    errno = 0;
}

static void
bt_ls_get_updates(const bt_mbet_event *const event, void *userdata)
{
    bt_ls_handler_ctx *ctx;
    const bt_mbet_event *n3w;
    json_object *result;
    json_object *score;
    char id[32];

    ctx = userdata;
    n3w = bt_ls_find_event(event, ctx->htbl);
    if (n3w == NULL)
        return;
    if (bt_ls_make_id(id, sizeof(id), event->tree_id, NO_PREFIX) == -1)
        return;
    result = bt_ls_compare_scores(event->score, n3w->score);
    if (result == NULL)
        return;
    score = json_object_new_object();
    if (score != NULL) {
        json_object_object_add(score, "s", result);
        if (bt_ls_check_odds(result, event, n3w) == -1) {
            json_object_put(result);
        } else {
            // Remove empty obejcts
            bt_ls_json_object_delete_if_empty(result, "a");
            bt_ls_json_object_delete_if_empty(result, "h");
            if (json_object_object_length(result) > 0) {
                // Send to database
                bt_ls_save_point2point(event);
                // It's good, add it to the root object
                json_object_object_add(ctx->object, id, score);
                // Update count
                ctx->count += 1;
            } else {
                json_object_put(score);
            }
        }
    }
}

json_object *
bt_ls_handle_feed(const bt_mbet_feed *const feed,
                  const bt_ls_hash_table *const ht,
                  const char *const method,
                  bt_mbet_event_handler_fn handler
    )
{
    bt_ls_handler_ctx ctx;
    bt_mbet_list *list;
    // Fill the context values
    ctx.count = 0;
    if (feed == NULL)
        return NULL;
    // This will be where we store the result
    ctx.object = json_object_new_object();
    if (ctx.object == NULL)
        return NULL;
    // This will be used to search for events (Quickly!)
    ctx.htbl = ht;
    // Try to set the method
    if (bt_ls_json_add_string(ctx.object, "mt", method) == -1)
        goto error;
    list = feed->sports;
    if (list == NULL)
        goto error;
    for (size_t idx = 0; idx < list->count; ++idx) {
        bt_mbet_list_item *item;
        // Make a pointer to the item
        item = list->items[idx];
        if (item == NULL)
            continue;
        // Apply the handler to every event
        bt_mbet_generic_sport_handler(item->data, handler, &ctx);
    }
    // We did not handle anything
    if (ctx.count == 0)
        goto error;
    // We have somehting, return it now
    return ctx.object;
error:
    json_object_put(ctx.object);
    return NULL;
}

static json_object *
bt_ls_update_handler(const bt_ls_context *const ctx, const bt_ls_context *const cached)
{
    return bt_ls_handle_feed(cached->live, ctx->htbl, "u", bt_ls_get_updates);
}

static json_object *
bt_ls_append_handler(const bt_ls_context *const ctx, const bt_ls_context *const cached)
{
    if (cached->htbl == NULL)
        return NULL;
    return bt_ls_handle_feed(ctx->live, cached->htbl, "a", bt_ls_append_event);
}

static json_object *
bt_ls_delete_handler(const bt_ls_context *const ctx, const bt_ls_context *const cached)
{
    return bt_ls_handle_feed(cached->live, ctx->htbl, "d", bt_ls_remove_event);
}

static void
bt_ls_hash_table_add_event(const bt_mbet_event *const event, void *data)
{
    bt_ls_hash_table *cache;
    size_t count;
    cache = data;
    if (cache == NULL)
        return;
    count = cache->count;
    cache->events[count] = event;
    cache->count = count + 1;
}

static bt_ls_hash_table *
bt_ls_generate_hash_table(bt_mbet_feed *feed)
{
    size_t count;
    bt_ls_hash_table *htbl;
    bt_mbet_list *list;
    if (feed == NULL)
        return NULL;
    list = feed->sports;
    if (list == NULL)
        return NULL;
    count = bt_mbet_count_events(feed);
    if (count == 0)
        return NULL;
    htbl = bt_malloc(sizeof(*htbl));
    if (htbl == NULL)
        return NULL;
    htbl->events = bt_malloc(count * sizeof(*htbl->events));
    htbl->count = 0;
    for (size_t idx = 0; idx < list->count; ++idx) {
        bt_mbet_list_item *item;
        bt_mbet_sport *sport;
        item = list->items[idx];
        if (item == NULL)
            continue;
        sport = item->data;

        bt_mbet_generic_sport_handler(sport, bt_ls_hash_table_add_event, htbl);
    }
    qsort(htbl->events, htbl->count, sizeof(*htbl->events), bt_mbet_eventcmp);
    return htbl;
}

static ssize_t
bt_ls_broadcast_message(bt_ls_context *ctx, json_object *object)
{
    ssize_t size;
    size_t count;
    const char *string;
    size_t length;
    pthread_mutex_lock(&bt_ls_mutex);
    count = ctx->clients_count;
    pthread_mutex_unlock(&bt_ls_mutex);
    if (count == 0)
        return 0;
    size = 0;
    string = json_object_to_json_string_ext(object, JSON_C_TO_STRING_PLAIN);
    if (string == NULL)
        return 0;
    length = strlen(string);
    if (length == 0)
        return 0;
    for (size_t idx = 0; idx < count; ++idx) {
        bool listening;
        bt_ls_client *client;
        ssize_t result;

        pthread_mutex_lock(&bt_ls_mutex);
        client = &ctx->clients[idx];        
        listening = client->listening;
        pthread_mutex_unlock(&bt_ls_mutex);

        if (listening == false)
            continue;

        pthread_mutex_lock(&bt_ls_mutex);
        result = bt_ls_send_message(client->fd, string, length);
        pthread_mutex_unlock(&bt_ls_mutex);

        if (result <= -1) {
            bt_ls_display_error();

            pthread_mutex_lock(&bt_ls_mutex);
            bt_ls_remove_client(ctx, client);
            pthread_mutex_unlock(&bt_ls_mutex);
        } else {
            size += result;
        }
    }
    return size;
}

static void
bt_ls_update_context(bt_ls_context *const cached, bt_ls_context *const ctx)
{
    bt_mbet_feed *feed;
    bt_ls_hash_table *htbl;

    feed = cached->live;
    cached->live = ctx->live;

    bt_mbet_feed_free(feed);

    feed = cached->next;
    cached->next = ctx->next;

    bt_mbet_feed_free(feed);

    htbl = cached->htbl;
    cached->htbl = ctx->htbl;

    if (htbl != NULL) {
        bt_free(htbl->events);
        bt_free(htbl);
    }
}

static ssize_t
bt_ls_iteration(bt_ls_context *cached)
{
    bt_mbet_feed *next;
    bt_mbet_feed *live;
    int result;
    bt_ls_context ctx;
    json_object *json;
    // Initialize them, to avoid UB
    result = 0;

    next = bt_mbet_feed_get(PreMatchFeed, NULL);
    live = bt_mbet_feed_get(LiveFeed, NULL);

    pthread_mutex_lock(&bt_ls_mutex);
    ctx.next = next;
    ctx.live = live;
    ctx.htbl = NULL;
    pthread_mutex_unlock(&bt_ls_mutex);

    pthread_mutex_lock(&bt_ls_mutex);
    ctx.htbl = bt_ls_generate_hash_table(ctx.live);
    pthread_mutex_unlock(&bt_ls_mutex);
    for (size_t idx = 0; handlers[idx] != NULL; ++idx) {
        bt_ls_feed_handler handler;
        // Make a pointer to call the function
        handler = handlers[idx];

        // Get the update
        pthread_mutex_lock(&bt_ls_mutex);
        json = handler(&ctx, cached);
        pthread_mutex_unlock(&bt_ls_mutex);
        if (json == NULL)
            continue;
        // Send the live score update if any
        result = bt_ls_broadcast_message(cached, json);
        // Release temporary memory
        json_object_put(json);
    }
    bt_ls_update_context(cached, &ctx);
    return result;
}

static void *
bt_ls_start(void *data)
{
    mongoc_init();
    bt_database_initialize();
    for (;;) {
        struct timespec end_time;
        struct timespec start_time;
        ssize_t result;
        double elapsed;

        clock_gettime(CLOCK_MONOTONIC, &start_time);

        result = bt_ls_iteration(data);
        if (result == -1)
            bt_ls_display_error();
        clock_gettime(CLOCK_MONOTONIC, &end_time);

        elapsed = TS_TO_SECONDS(end_time) - TS_TO_SECONDS(start_time);
        if (elapsed < 5.0) {
            fprintf(stderr, "sleeping %f s\n", 5.0 - elapsed);
            usleep((unsigned int) ((5.0 - elapsed) * 1.0E6));
        }
    }
    bt_ls_display_error();
    bt_database_finalize();

    mongoc_cleanup();
    return NULL;
}

static int
bt_ls_initialize(bt_ls_context *ctx)
{
    bt_database_initialize();
    ctx->next = bt_mbet_feed_get(LiveFeed, NULL);
    ctx->live = bt_mbet_feed_get(LiveFeed, NULL);
    bt_database_finalize();

    ctx->htbl = NULL;
    ctx->clients_count = 0;
    ctx->clients = bt_malloc(CLIENT_POOL_SIZE * sizeof(*ctx->clients));
    if (ctx->clients == NULL)
        goto error;
    ctx->client_pool_size = CLIENT_POOL_SIZE;
    return 0;
error:
    return -1;
}

static int
bt_ls_reset_rfds(bt_ls_context *ctx, fd_set *rfds, int server)
{
    int fdmax;

    FD_ZERO(rfds);
    FD_SET(server, rfds);

    fdmax = server;
    for (size_t idx = 0; idx < ctx->clients_count; ++idx) {
        bt_ls_client *client;
        client = &ctx->clients[idx];
        if (fdmax < client->fd)
            fdmax = client->fd;
        FD_SET(client->fd, rfds);
    }
    return fdmax;
}

static int
bt_ls_unknown_uds_location()
{
    fprintf(stderr, "dónde va el Unix Domain Socket?\n");
    return -1;
}

static json_object *
bt_ls_init_current_feed(const bt_ls_context *const ctx)
{
    return bt_ls_new(ctx->live, "i");
}

static const char *
bt_ls_get_class(int rank, bt_tennis_category category)
{
    switch (rank) {
    case 0:
        return "ITF";
    case 1:
        return "Challenger";
    }
    return ((category & CategoryATP) != 0) ? "ATP" : "WTA";
}

static int
bt_ls_db_event_set_player(json_object *players, bt_tennis_category cid,
                       const bt_ls_player *const player, const char *const team)
{
    json_object *object;
    const char *category;
    object = json_object_new_object();
    if (object == NULL)
        return -1;
    json_object_object_add(players, team, object);

    if (bt_ls_json_add_string(object, "n", player->name) == -1)
        return -1;
    if (bt_ls_json_add_string(object, "f", player->flag) == -1)
        return -1;
    if (bt_ls_json_add_integer(object, "i", player->id) == -1)
        return -1;
    category = bt_ls_get_category(cid);
    if (category == NULL)
        return -1;
    if (bt_ls_json_add_string(object, "c", category) == -1)
        return -1;
    if (bt_ls_json_add_integer(object, "r", player->rank) == -1)
        return -1;
    if (bt_ls_json_add_real(object, "o", player->odds) == -1)
        return -1;
    return 0;
}

static int
bt_ls_append_event_from_db_add_score(json_object *const event, const char *const result)
{
    bt_mbet_score *score;
    json_object *object;

    score = bt_score_parse_oncourt(result);
    if (score == NULL)
        return -1;
    object = bt_ls_get_score(score);
    if (object == NULL)
        goto error;
    json_object_object_add(event, "s", object);
    bt_mbet_score_free(score);

    return 0;
error:
    bt_mbet_score_free(score);
    return -1;
}

static int
bt_ls_append_event_from_db_add_event(int eid,
                         json_object *const events, const bt_ls_event *const ev)
{
    json_object *event;
    json_object *players;
    char id[32];
    event = json_object_new_object();
    if (bt_ls_make_id(id, sizeof(id), eid, 'E') == -1)
        return -1;
    json_object_object_add(events, id, event);

    if (bt_ls_create_ls_event_id(event, ev) == -1)
        return -1;
    players = json_object_new_object();
    if (events == NULL)
        return -1;
    json_object_object_add(event, "p", players);

    if (bt_ls_db_event_set_player(players, ev->category, &ev->home, "h") == -1)
        return -1;
    if (bt_ls_db_event_set_player(players, ev->category, &ev->away, "a") == -1)
        return -1;
    if (bt_ls_append_event_from_db_add_score(event, ev->result) == -1)
        return -1;
    return 0;
}

static int
bt_ls_append_event_from_db_add_event_list(int eid,
                         json_object *const parent, const bt_ls_event *const ev)
{
    json_object *events;
    bool existed;
    events = bt_ls_json_object_merge(parent, "e", &existed);
    if (events == NULL)
        return -1;
    return bt_ls_append_event_from_db_add_event(eid, events, ev);
}

static int
bt_ls_append_event_from_db_add_tournament(int eid,
                         json_object *const parent, const bt_ls_event *const ev)
{
    bool existed;
    const bt_ls_tour *tour;
    json_object *tourobj;
    char id[32];
    float altitude;

    tour = &ev->tour;
    if (bt_ls_make_id(id, sizeof(id), tour->id, 'S') == -1)
        return -1;
    // The tournament
    tourobj = bt_ls_json_object_merge(parent, id, &existed);
    if (tourobj == NULL)
        return -1;
    if (existed == false) {
        const char *round;
        int nsets;
        const char *category;
        round = bt_ls_get_round_name(tour->round);
        if (bt_ls_json_add_string(tourobj, "n", tour->name) == -1)
            return -1;
        if (bt_ls_json_add_string(tourobj, "r", round) == -1)
            return -1;
        if (bt_ls_json_add_string(tourobj, "f", tour->flag) == -1)
            return -1;
        if (bt_ls_json_add_integer(tourobj, "id", tour->id) == -1)
            return -1;
        category = bt_ls_get_category(ev->category);
        if (category == NULL)
            return -1;
        if (bt_ls_json_add_string(tourobj, "q", category) == -1)
            return -1;
        altitude = bt_ls_get_tournament_altitude(tour->id);
        if (bt_ls_json_add_real(tourobj, "a", altitude) == -1)
            return -1;
        if (bt_ls_json_add_string(tourobj, "c", tour->court) == -1)
            return -1;
        nsets = bt_ls_get_set_count(tour->rank, ev->category);
        if (bt_ls_json_add_integer(tourobj, "sc", nsets) == -1)
            return -1;
    }
    return bt_ls_append_event_from_db_add_event_list(eid, tourobj, ev);
}

static int
bt_ls_append_event_from_db(int eid,
                               json_object *object, const bt_ls_event *const ev)
{
    const char *name;
    json_object *group;
    const bt_ls_tour *tour;
    bool existed;
    tour = &ev->tour;
    name = bt_ls_get_class(tour->rank, ev->category);
    // The global group (ATP, WTA, Challenger or ITF)
    group = bt_ls_json_object_merge(object, name, &existed);
    if (group == NULL)
        return -1;
    bt_ls_append_event_from_db_add_tournament(eid, group, ev);
    return 0;
}

static int
bt_ls_append_event_from_db_fetch(json_object *object,
        bt_ls_event *const event, bt_tennis_category category, MYSQL_STMT *stmt)
{
    int result;
    int count;
    if (stmt == NULL)
        return -1;
    memset(event, 0, sizeof(*event));
    event->category = category;

    mysql_stmt_store_result(stmt);
    for (count = mysql_stmt_num_rows(stmt); count >= 0; --count) {
        if (mysql_stmt_fetch(stmt) != 0)
            goto error;
        result = bt_ls_append_event_from_db(1 + count, object, event);
        if (result == -1)
            goto error;
        memset(event, 0, sizeof(*event));
        event->category = category;
    }
    mysql_stmt_free_result(stmt);
    mysql_stmt_close(stmt);
    return 0;

error:
    mysql_stmt_free_result(stmt);
    mysql_stmt_close(stmt);
    return -1;
}

static int
bt_ls_init_matches_from_dbfc(json_object *object,
                      const char *const query_name, bt_tennis_category category)
{
    MYSQL_STMT *stmt;
    char *query;
    /* Players data */
    const char *name;
    bt_ls_player *home;
    bt_ls_tour *tour;
    bt_ls_player *away;
    bt_ls_event event;
    // Choose the name of the category
    name = ((category & CategoryATP) != 0) ? "atp" : "wta";
    // Make a few pointers to simplify syntax
    home = &event.home;
    tour = &event.tour;
    away = &event.away;
    // Load the query from the table
    query = bt_load_query(query_name, "%category%", name, NULL);
    if (query == NULL)
        return -1;
    // Create the statement and, execute it
    stmt = bt_mysql_easy_query(query, MATCHES_QUERY_FORMAT,
        // Tournament data
        tour->name, &tour->id, tour->flag, tour->court,
        // Player 1 data
        &home->id, home->flag, &home->rank, home->name, &home->odds,
        // Player 2 data
        &away->id, away->flag, &away->rank, away->name, &away->odds,
        // Tournament rank and round
        &tour->rank, &tour->round,
        // Event result
        event.result
    );
    // Free the query string
    bt_free(query);
    // Use the helper function to return the result and fill
    // the json object
    return bt_ls_append_event_from_db_fetch(object, &event, category, stmt);
}

static json_object *
bt_ls_init_matches_from_db(const bt_ls_context *const ls,
                              const char *const method, const char *const query)
{
    json_object *object;
    object = json_object_new_object();
    if (object == NULL)
        return NULL;
    (void) ls;
    if (bt_ls_json_add_string(object, "mt", method) == -1)
        goto error;
    bt_ls_init_matches_from_dbfc(object, query, CategoryATP);
    bt_ls_init_matches_from_dbfc(object, query, CategoryWTA);
    return object;
error:
    json_object_put(object);
    return NULL;
}

static json_object *
bt_ls_init_next_matches(const bt_ls_context *const ls)
{
    json_object *result;
    result = bt_ls_handle_feed(ls->next, NULL, "s", bt_ls_append_event);
    return result;
}

static json_object *
bt_ls_init_finished_matches(const bt_ls_context *const ls)
{
    return bt_ls_init_matches_from_db(ls, "f", "finished matches");
}

static json_object *
bt_ls_init_yesterday_matches(const bt_ls_context *const ls)
{
    return bt_ls_init_matches_from_db(ls, "y", "yesterday matches");
}

static void
bt_ls_execute_initfn(bt_ls_initfn function,
                const bt_ls_context *const ls, const bt_ls_client *const client)
{
    json_object *object;
    size_t length;
    const char *string;
    // Create the initial data container
    object = function(ls);
    if (object == NULL)
        return;
    string = json_object_to_json_string_ext(object, JSON_C_TO_STRING_PLAIN);
    if (string == NULL)
        goto invalid;
    length = strlen(string);
    if (length == 0)
        goto invalid;
    // Send the initial structure to the server
    bt_ls_send_message(client->fd, string, length);
invalid:
    // Release memory
    json_object_put(object);
}

static void
bt_ls_execute_start(bt_ls_context *ls, bt_ls_client *client)
{
    bt_ls_initfn *functions[] = {
        bt_ls_init_next_matches,
        bt_ls_init_current_feed,
        bt_ls_init_finished_matches,
        bt_ls_init_yesterday_matches,
        NULL
    };

    client->listening = true;
    for (size_t idx = 0; functions[idx] != NULL; ++idx) {
        bt_ls_execute_initfn(functions[idx], ls, client);
    }
}

static int
bt_ls_commandcmp(const void *const lhs, const void *const rhs)
{
    return strcmp(
        ((const bt_command_descriptor *) lhs)->name,
        ((const bt_command_descriptor *) rhs)->name
    );
}

static bt_command
bt_ls_get_command(const char *const buffer)
{
    bt_command_descriptor *result;
    bt_command_descriptor key;
    key.name = buffer;
    result = bsearch(&key, bt_commands, CMDCOUNT, CMDSIZE, bt_ls_commandcmp);
    if (result == NULL)
        return InvalidCommand;
    return result->id;
}

static void
bt_ls_client_ready_function(bt_ls_context *ls, fd_set *rfds)
{
    for (size_t idx = 0; idx < ls->clients_count; ++idx) {
        char buffer[0x4000];
        bt_ls_client *client;
        ssize_t length;
        // Safely get the client from the list
        client = &ls->clients[idx];
        // Check if this client has messages
        if (FD_ISSET(client->fd, rfds) == 0)
            continue;
        length = recv(client->fd, buffer, sizeof(buffer) - 1, MSG_NOSIGNAL);
        if (length <= 0) {
            bt_ls_remove_client(ls, client);
        } else {
            buffer[length] = '\0';
            switch (bt_ls_get_command(buffer)) {
            case StartCommand:
                bt_ls_execute_start(ls, client);
                break;
            case PointsHistorical:
                break;
            case Head2Head:
                break;
            case InvalidCommand:
                bt_ls_remove_client(ls, client);
                break;
            }
        }
    }
}

static void
bt_server_ready_function(bt_ls_context *ctx, int server)
{
    int client;
    if ((client = accept(server, NULL, NULL)) != -1) {
        // Append this client to the list
        bt_ls_append_client(ctx, client);
    } else if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
        for (size_t idx = 0; idx < ctx->clients_count; ++idx) {
            bt_ls_handle_client(&ctx->clients[idx], server);
        }
    }

}

static void
bt_ls_main_loop(int server, bt_ls_context *ctx)
{
    int count;
    fd_set rfds;
    int maxfd;

    pthread_mutex_lock(&bt_ls_mutex);
    maxfd = bt_ls_reset_rfds(ctx, &rfds, server);
    pthread_mutex_unlock(&bt_ls_mutex);

    // Events loop
    fprintf(stderr, "Event loop ready\n");
    while ((count = select(maxfd + 1, &rfds, NULL, NULL, NULL)) != -1) {
        if (FD_ISSET(server, &rfds) != 0) {
            pthread_mutex_lock(&bt_ls_mutex);
            bt_server_ready_function(ctx, server);
            pthread_mutex_unlock(&bt_ls_mutex);
        }
        pthread_mutex_lock(&bt_ls_mutex);
        bt_ls_client_ready_function(ctx, &rfds);
        pthread_mutex_unlock(&bt_ls_mutex);
        // Update file descriptor set
        pthread_mutex_lock(&bt_ls_mutex);
        maxfd = bt_ls_reset_rfds(ctx, &rfds, server);
        // Unlock the mutex
        pthread_mutex_unlock(&bt_ls_mutex);
    }
}

// This program will handle connections from the apache web server
// that are on turn connected to a websocket client.
//
// This program will, send the results of the tennis matches in the
// live xml feed comming from the currently used feed by the
// www.betenis.com website and the BeTenis project.
//
int
main(int argc, char **argv)
{
    bt_ls_context ctx;
    int server;
    pthread_t thread;

    if (argc < 2)
        return bt_ls_unknown_uds_location();
    // Initialize the main structure
    if (bt_ls_initialize(&ctx) == -1)
        return -1;
    qsort(bt_commands, CMDCOUNT, CMDSIZE, bt_ls_commandcmp);
    // Create a Unix Domain Socket to allow the apache
    // web server to connect with us
    server = bt_ls_make_socket(argv[1]);
    if (server == -1)
        goto error;
    // Start a new thread to read the results from the
    // XML and send them to the cleints
    if (pthread_create(&thread, NULL, bt_ls_start, &ctx) == -1)
        goto error;
    // Start the main loop
    bt_ls_main_loop(server, &ctx);
    // Both threads must be always alive
    pthread_join(thread, NULL);
    // If we reach this, it's over
    close(server);
    return 0;
error:
    bt_ls_display_error();
    // Release temporary memory
    bt_mbet_feed_free(ctx.live);
    if (ctx.htbl != NULL) {
        bt_ls_hash_table *htbl;

        htbl = ctx.htbl;

        bt_free(htbl->events);
        bt_free(htbl);
    }
    if (server == -1)
        return -1;
    close(server);
    return -1;
}

#ifdef _DEBUG
void
bt_print_backtrace(void)
{
    void *buffer[0x4000];
    int result;
    char **names;
    result = backtrace(buffer, sizeof(buffer) / sizeof(*buffer));
    names = backtrace_symbols(buffer, result);
    if (names != NULL) {
        for (int idx = 0; idx < result; ++idx) {
            fprintf(stderr, "> %s\n", names[idx]);
        }
    }
    free(names);
}
#endif

#include <string.h>
#include <unistd.h>

#include <bt-context.h>
#include <bt-pinnacle.h>
#include <bt-memory.h>

#include <bt-debug.h>
#include <bt-drops.h>
#include <bt-util.h>
#include <bt-http-headers.h>
#include <bt-database.h>

#include <bt-daemon.h>

#include <http-connection.h>
#include <http-protocol.h>

#include <pcre.h>
#include <json.h>

#include <limits.h>

#define LEAGUES "https://api.pinnaclesports.com/v2/leagues?sportid=33"
#define FIXTURES "https://api.pinnaclesports.com/v1/fixtures?sportid=33&oddsformat=1&islive=0&since=%ld"
#define ODDS "https://api.pinnaclesports.com/v1/odds?sportid=33&oddsformat=1&islive=0&since=%ld"
#define GETITEM(x) ((bt_pinnacle_get_item_fn *) &bt_pinnacle_get_ ## x)
#define bt_pinnacle_get_array(field, object, item) \
        (bt_pinnacle_ ## field **) bt_pinnacle_get_array_imp(&object->field ## _count, item, GETITEM(field))
#define UPDATE_QUERY "UPDATE mercado_ganador_partido SET isnew = 0 WHERE LEFT(iid, 1) = 'P'"

typedef void *(bt_pinnacle_get_item_fn)(json_object *);

enum bt_pinnacle_event_status {
    PESInvalid = 0,
    PESLimited = 'I',
    PESUnavailable = 'H',
    PESOpen = 'O'
};

enum bt_pinnacle_tennis_period_number {
    PNMatch,
    PNFirstSet,
    PNSecondSet,
    PNThirdSet,
    PNFourthSet,
    PNFifthSet
};

typedef struct bt_pinnacle_spread {
    int altLineId;
    double hdp;
    double home;
    double away;
} bt_pinnacle_spread;

typedef struct bt_pinnacle_money_line {
    double home;
    double away;
    double draw;
} bt_pinnacle_money_line;

typedef struct bt_pinnacle_total {
    int altLineId;
    double points;
    double over;
    double under;
} bt_pinnacle_total;

typedef struct bt_pinnacle_team_total {
    double home;
    double away;
} bt_pinnacle_team_total;

typedef struct bt_pinnacle_period {
    int lineId;
    enum bt_pinnacle_tennis_period_number number;
    char *cutoff;
    bt_pinnacle_spread **spread;
    double maxSpread;
    size_t spread_count;
    bt_pinnacle_total **totals;
    double max_total;
    size_t total_count;
    bt_pinnacle_money_line *money_line;
    double max_money_line;
    bt_pinnacle_team_total *team_total;
    double max_team_total;
} bt_pinnacle_period;

typedef struct bt_pinnacle_event {
    int id;
    char *starts;
    char *home;
    char *away;
    enum bt_pinnacle_event_status status;
    bt_pinnacle_period **periods;
    int live_status;
    int parlay_restriction;
    size_t period_count;
} bt_pinnacle_event;

typedef struct bt_pinnacle_league {
    int id;
    char *name;
    bt_pinnacle_event **events;
    bt_tennis_category category;
    bool has_offerings;
    size_t event_count;
} bt_pinnacle_league;

typedef struct bt_pinnacle_league_list {
    bt_pinnacle_league **leagues;
    size_t league_count;
} bt_pinnacle_league_list;

typedef struct bt_pinnacle_object {
    int sportId;
    long int last;
    bt_pinnacle_league **leagues;
    size_t league_count;
} bt_pinnacle_object;


typedef struct bt_pinnacle_ctx {
    bt_http *http;
    bt_http_headers *headers;
    long int fixtures_last;
    long int odds_last;
} bt_pinnacle_ctx;


static void
bt_pinnacle_set_fixtures_last(long int last)
{
    const char *query;
    MYSQL_STMT *stmt;
    query = "UPDATE pinnacle_last SET fixtures = ?";
    stmt = bt_mysql_easy_query(query, "%ld", &last);
    if (stmt == NULL)
        return;
    mysql_stmt_close(stmt);
}

static void
bt_pinnacle_set_odds_last(long int last)
{
    const char *query;
    MYSQL_STMT *stmt;
    query = "UPDATE pinnacle_last SET odds = ?";
    stmt = bt_mysql_easy_query(query, "%ld", &last);
    if (stmt == NULL)
        return;
    mysql_stmt_close(stmt);
}

static long int
bt_pinnacle_get_fixtures_last(void)
{
    long int value;
    const char *query;
    MYSQL_STMT *stmt;
    query = "SELECT fixtures FROM pinnacle_last";
    stmt = bt_mysql_easy_query(query, "|%ld", &value);
    if (stmt == NULL)
        return 0;
    mysql_stmt_fetch(stmt);
    mysql_stmt_free_result(stmt);
    mysql_stmt_close(stmt);
    return value;
}

static long int
bt_pinnacle_get_odds_last(void)
{
    long int value;
    const char *query;
    MYSQL_STMT *stmt;
    query = "SELECT odds FROM pinnacle_last";
    stmt = bt_mysql_easy_query(query, "|%ld", &value);
    if (stmt == NULL)
        return 0;
    mysql_stmt_fetch(stmt);
    mysql_stmt_free_result(stmt);
    mysql_stmt_close(stmt);
    return value;
}

int
bt_pinnacle_event_cmp(const void *const lhs, const void *const rhs)
{
    bt_pinnacle_event *lev;
    bt_pinnacle_event *rev;

    lev = *(bt_pinnacle_event **) lhs;
    rev = *(bt_pinnacle_event **) rhs;

    return lev->id - rev->id;
}

int
bt_pinnacle_league_cmp(const void *const lhs, const void *const rhs)
{
    bt_pinnacle_league *lleague;
    bt_pinnacle_league *rleague;

    lleague = *(bt_pinnacle_league **) lhs;
    rleague = *(bt_pinnacle_league **) rhs;

    return lleague->id - rleague->id;
}

static void
bt_pinnacle_period_free(bt_pinnacle_period *period)
{
    if (period == NULL)
        return;
    if (period->spread != NULL) {
        for (size_t i = 0; i < period->spread_count; ++i)
            bt_free(period->spread[i]);
        bt_free(period->spread);
    }
    if (period->totals != NULL) {
        for (size_t i = 0; i < period->total_count; ++i)
            bt_free(period->totals[i]);
        bt_free(period->totals);
    }
    bt_free(period->money_line);
    bt_free(period->team_total);
    bt_free(period->cutoff);
    bt_free(period);
}

static void
bt_pinnacle_event_free(bt_pinnacle_event *event)
{
    if (event == NULL)
        return;
    if (event->periods != NULL) {
        for (size_t idx = 0; idx < event->period_count; ++idx) {
            bt_pinnacle_period_free(event->periods[idx]);
        }
        bt_free(event->periods);
    }
    bt_free(event->home);
    bt_free(event->starts);
    bt_free(event->away);
    bt_free(event);
}

static void
bt_pinnacle_league_free(bt_pinnacle_league *league)
{
    if (league == NULL)
        return;
    if (league->events != NULL) {
        for (size_t idx = 0; idx < league->event_count; ++idx)
            bt_pinnacle_event_free(league->events[idx]);
        bt_free(league->events);
    }
    bt_free(league->name);
    bt_free(league);
}

static void
bt_pinnacle_leagues_free(bt_pinnacle_league_list *list)
{
    if (list == NULL)
        return;
    if (list->leagues != NULL) {
        for (size_t idx = 0; idx < list->league_count; ++idx)
            bt_pinnacle_league_free(list->leagues[idx]);
        bt_free(list->leagues);
    }
    bt_free(list);
}

static void
bt_pinnacle_root_free(bt_pinnacle_object *root)
{
    if (root == NULL)
        return;
    if (root->leagues != NULL) {
        for (size_t idx = 0; idx < root->league_count; ++idx)
            bt_pinnacle_league_free(root->leagues[idx]);
        bt_free(root->leagues);
    }
    bt_free(root);
}

static void **
bt_pinnacle_get_array_imp(size_t *count, json_object *object,
                                              bt_pinnacle_get_item_fn *get_item)
{
    void **data;
    *count = json_object_array_length(object);
    if (*count == 0)
        return NULL;
    data = bt_malloc(*count * sizeof(void *));
    if (data == NULL) {
        *count = 0;
        return NULL;
    }

    for (size_t idx = 0; idx < *count; ++idx) {
        json_object *item;
        item = json_object_array_get_idx(object, idx);
        if (item == NULL)
            continue;
        data[idx] = get_item(item);
    }
    return data;
}

bt_pinnacle_money_line *
bt_pinnacle_get_money_line(json_object *object)
{
    bt_pinnacle_money_line *money_line;
    json_object *home;
    json_object *draw;
    json_object *away;
    if (json_object_object_get_ex(object, "home", &home) == false)
        return NULL;
    if (json_object_object_get_ex(object, "away", &away) == false)
        return NULL;
    money_line = bt_malloc(sizeof(*money_line));
    if (money_line == NULL)
        return NULL;
    money_line->home = json_object_get_double(home);
    money_line->away = json_object_get_double(away);
    // Draw is optional, only for sports where it can happen
    // tennis is not such a sport. But for correctness this
    // will be left as is
    if (json_object_object_get_ex(object, "draw", &draw) == true) {
        money_line->draw = json_object_get_double(draw);
    } else {
        money_line->draw = 0;
    }
    return money_line;
}

bt_pinnacle_spread *
bt_pinnacle_get_spread(json_object *object)
{
    bt_pinnacle_spread *spread;
    json_object *altLineId;
    json_object *home;
    json_object *hdp;
    json_object *away;
    if (json_object_object_get_ex(object, "home", &home) == false)
        return NULL;
    if (json_object_object_get_ex(object, "hdp", &hdp) == false)
        return NULL;
    if (json_object_object_get_ex(object, "away", &away) == false)
        return NULL;
    spread = bt_malloc(sizeof(*spread));
    if (spread == NULL)
        return NULL;
    // This field is optional
    if (json_object_object_get_ex(object, "altLineId", &altLineId) == true) {
        spread->altLineId = json_object_get_double(altLineId);
    } else {
        spread->altLineId = 0;
    }
    spread->home = json_object_get_double(home);
    spread->hdp = json_object_get_double(hdp);
    spread->away = json_object_get_double(away);
    return spread;
}

bt_pinnacle_total *
bt_pinnacle_get_total(json_object *object)
{
    bt_pinnacle_total *total;
    json_object *altLineId;
    json_object *over;
    json_object *points;
    json_object *under;
    if (json_object_object_get_ex(object, "over", &over) == false)
        return NULL;
    if (json_object_object_get_ex(object, "points", &points) == false)
        return NULL;
    if (json_object_object_get_ex(object, "under", &under) == false)
        return NULL;
    total = bt_malloc(sizeof(*total));
    if (total == NULL)
        return NULL;
    // This field is optional
    if (json_object_object_get_ex(object, "aldLineId", &altLineId) == true) {
        total->altLineId = json_object_get_double(altLineId);
    } else {
        total->altLineId = 0;
    }
    total->over = json_object_get_double(over);
    total->points = json_object_get_double(points);
    total->under = json_object_get_double(under);
    return total;
}

bt_pinnacle_team_total *
bt_pinnacle_get_team_total(json_object *object)
{
    bt_pinnacle_team_total *team_total;
    json_object *home;
    json_object *away;
    // None of these is a mandatory value, it might be present or not
    // but it doesn't make sense if they are not present, so in case
    // they're not, we will ignore them
    if (json_object_object_get_ex(object, "home", &home) == false)
        return NULL;
    if (json_object_object_get_ex(object, "away", &away) == false)
        return NULL;
    team_total = bt_malloc(sizeof(*team_total));
    if (team_total == NULL)
        return NULL;
    team_total->home = json_object_get_double(home);
    team_total->away = json_object_get_double(away);
    return team_total;
}

bt_pinnacle_period *
bt_pinnacle_get_period(json_object *object)
{
    bt_pinnacle_period *period;
    json_object *item;
    period = bt_malloc(sizeof(*period));
    if (period == NULL)
        return NULL;
    // Initialize everything to 0
    memset(period, 0, sizeof(*period));
    if (json_object_object_get_ex(object, "lineId", &item) == false)
        goto error;
    // Get the line id
    period->lineId = json_object_get_int(item);
    if (json_object_object_get_ex(object, "cutoff", &item) == false)
        goto error;
    // The cutoff
    period->cutoff = bt_strdup(json_object_get_string(item));
    if (json_object_object_get_ex(object, "number", &item) == false)
        goto error;
    // Period number
    period->number = json_object_get_int(item);
    // The spread
    if (json_object_object_get_ex(object, "spread", &item) == true)
        period->spread = bt_pinnacle_get_array(spread, period, item);
    if (json_object_object_get_ex(object, "maxSpread", &item) == true)
        period->maxSpread = json_object_get_double(item);
    // The money line
    if (json_object_object_get_ex(object, "moneyline", &item) == true)
        period->money_line = bt_pinnacle_get_money_line(item);
    if (json_object_object_get_ex(object, "maxMoneyLine", &item) == true)
        period->max_money_line = json_object_get_double(item);
    // The total points
    if (json_object_object_get_ex(object, "totals", &item) == true)
        period->totals = bt_pinnacle_get_array(total, period, item);
    if (json_object_object_get_ex(object, "maxTotal", &item) == true)
        period->max_total = json_object_get_double(item);
    // The team total points
    if (json_object_object_get_ex(object, "teamTotal", &item) == true)
        period->team_total = bt_pinnacle_get_team_total(item);
    if (json_object_object_get_ex(object, "maxTeamTotal", &item) == true)
        period->max_team_total = json_object_get_double(item);
    return period;
error:
    bt_pinnacle_period_free(period);
    return NULL;
}

static bt_pinnacle_event *
bt_pinnacle_get_event(json_object *object)
{
    bt_pinnacle_event *event;
    json_object *item;
    event = bt_malloc(sizeof(*event));
    if (event == NULL)
        return NULL;
    // Set every thing to 0
    memset(event, 0, sizeof(*event));
    if (json_object_object_get_ex(object, "id", &item) == false)
        goto error;
    // The id is always available
    event->id = json_object_get_int(item);
    // The `status' member is present only in GetFxitures operation
    if (json_object_object_get_ex(object, "status", &item) == true) {
        const char *string;
        string = json_object_get_string(item);
        event->status = (string != NULL) ? *string : PESInvalid;
    }
    // The `home' member is present only in GetFxitures operation
    if (json_object_object_get_ex(object, "home", &item) == true)
        event->home = bt_strdup(json_object_get_string(item));
    // The `away' member is present only in GetFxitures operation
    if (json_object_object_get_ex(object, "away", &item) == true)
        event->away = bt_strdup(json_object_get_string(item));
    // The `starts' member is present only in GetFxitures operation
    if (json_object_object_get_ex(object, "starts", &item) == true)
        event->starts = bt_strdup(json_object_get_string(item));
    if (json_object_object_get_ex(object, "liveStatus", &item) == true)
        event->live_status = json_object_get_int(item);
    if (json_object_object_get_ex(object, "parlayRestriction", &item) == true)
        event->parlay_restriction = json_object_get_int(item);
    if (json_object_object_get_ex(object, "periods", &item) == true)
        event->periods = bt_pinnacle_get_array(period, event, item);
    return event;
error:
    bt_pinnacle_event_free(event);
    return NULL;
}

static bt_pinnacle_league *
bt_pinnacle_get_league(json_object *object)
{
    bt_pinnacle_league *league;
    json_object *offerings;
    json_object *id;
    json_object *events;
    json_object *name;
    if (json_object_object_get_ex(object, "id", &id) == false)
        return NULL;
    league = bt_malloc(sizeof(*league));
    if (league == NULL)
        return NULL;
    league->id = json_object_get_int(id);
    if (json_object_object_get_ex(object, "name", &name) == true) {
        league->name = bt_strdup(json_object_get_string(name));
    } else {
        league->name = NULL;
    }
    // Check if this field is set (useful for GetLeagues)
    if (json_object_object_get_ex(object, "hasOfferings", &offerings) == true) {
        league->has_offerings = json_object_get_boolean(offerings);
    } else {
        // If this field is not present, it's GetFixtures, and in
        // principle all of them are "valid"
        league->has_offerings = true;
    }
    if (json_object_object_get_ex(object, "events", &events) == true) {
        // Events array
        league->events = bt_pinnacle_get_array(event, league, events);
        if (league->events == NULL)
            return league;
        // Sort the potential array so we can find events on demand
        // with binary search
        qsort(league->events, league->event_count,
                               sizeof(*league->events), bt_pinnacle_event_cmp);
    } else {
        league->events = NULL;
        league->event_count = 0;
    }
    return league;
}

static bt_pinnacle_object *
bt_pinnacle_parse_main(const char *const json)
{
    bt_pinnacle_object *root;
    json_object *item;
    json_object *object;
    bool result;
    // Parse the Json string
    object = json_tokener_parse(json);
    if (object == NULL)
        return NULL;
    // Allocate space
    root = bt_malloc(sizeof(*root));
    if (root == NULL)
        goto error;
    root->leagues = NULL;
    root->league_count = 0;
    // Extract the sportId (it must be 33 since we requested that)
    if (json_object_object_get_ex(object, "sportId", &item) == false)
        goto error;
    root->sportId = json_object_get_int(item);
    if (json_object_object_get_ex(object, "last", &item) == false)
        goto error;
    root->last = json_object_get_int64(item);
    // Get the league/leagues (wonder why they wer named like this?)
    result = json_object_object_get_ex(object, "league", &item);
    if (result == false)
        result = json_object_object_get_ex(object, "leagues", &item);
    if (result == true)
        root->leagues = bt_pinnacle_get_array(league, root, item);
    json_object_put(object);
    return root;
error:
    bt_pinnacle_root_free(root);
    json_object_put(object);
    return NULL;

}

bt_pinnacle_event *
bt_pinnacle_events_find_one(bt_pinnacle_event **list, size_t count, int id)
{
    bt_pinnacle_event *pointer;
    bt_pinnacle_event event;
    bt_pinnacle_event **found;
    pointer = &event;
    pointer->id = id;
    found = bsearch(&pointer, list, count, sizeof(*list), bt_pinnacle_event_cmp);
    if (found == NULL)
        return NULL;
    return *found;
}

bt_pinnacle_event *
bt_pinnacle_league_find_event(bt_pinnacle_league *league, int eid)
{
    return bt_pinnacle_events_find_one(league->events, league->event_count, eid);
}

bt_pinnacle_event *
bt_pinnacle_root_find_event(bt_pinnacle_object *fixtures, int eid)
{
    for (size_t idx = 0; idx < fixtures->league_count; ++idx) {
        bt_pinnacle_event *event;
        event = bt_pinnacle_league_find_event(fixtures->leagues[idx], eid);
        if (event == NULL)
            continue;
        return event;
    }
    return NULL;
}

static long int
bt_pinnacle_parse_odds(bt_pinnacle_object *target, const char *const json)
{
    long int last;
    // In the second phase, we make a call to the GetOdds operation
    // of the pinnacle API. We then extract the information that is
    // missing in the GetFixtures feed.
    bt_pinnacle_object *root;
    // Get the events tree from this input
    root = bt_pinnacle_parse_main(json);
    if (root == NULL)
        return -1;
    // For each `league' parse every event
    for (size_t idx = 0; idx < root->league_count; ++idx) {
        bt_pinnacle_league *league;
        // Make a pointer to current league
        league = root->leagues[idx];
        // For each event, find the corresponding event in
        // target, extract the `periods' from the original
        // event.
        for (size_t jdx = 0; jdx < league->event_count; ++jdx) {
            bt_pinnacle_event *event;
            bt_pinnacle_event *tneve;

            event = league->events[jdx];
            tneve = bt_pinnacle_root_find_event(target, event->id);
            if (tneve == NULL) {
                // Evento sin odds establecidas
            } else {
                // Exract the only interesting information
                // from `event'
                tneve->periods = event->periods;
                event->periods = NULL;
                // Avoid dereferencing the `periods' pointer that was
                // made `NULL' above and copy the count too
                tneve->period_count = event->period_count;
                event->period_count = 0;
            }
        }
    }
    last = root->last;
    // Releas temporary resources
    bt_pinnacle_root_free(root);
    return last;
}

static int
bt_pinnacle_get_odds(bt_pinnacle_object *object, bt_pinnacle_ctx *const api)
{
    char *json;
    char *url;
    // Build the URL
    url = bt_strdup_printf(ODDS, api->odds_last);
    if (url == NULL)
        return -1;
    // Make the HTTP request
    json = bt_http_get(url, false, api->http, api->headers);
    bt_free(url);
    if (json == NULL)
        return -1;
    // Extract the interesting dta
    api->odds_last = bt_pinnacle_parse_odds(object, json);
    bt_pinnacle_set_odds_last(api->odds_last);
    bt_free(json);
    return 0;
}

static bt_pinnacle_league_list *
bt_pinnacle_get_leagues(const bt_pinnacle_ctx *const api)
{
    json_object *object;
    bt_pinnacle_league_list *list;
    json_object *leagues;
    char *json;
    // Build the URL
    list = NULL;
    object = NULL;
    json = bt_http_get(LEAGUES, false, api->http, api->headers);
    if (json == NULL)
        return NULL;
    object = json_tokener_parse(json);
    if (object == NULL)
        goto error;
    if (json_object_object_get_ex(object, "leagues", &leagues) == false)
        goto error;
    list = bt_malloc(sizeof(*list));
    if (list == NULL)
        goto error;
    list->leagues = bt_pinnacle_get_array(league, list, leagues);
    qsort(list->leagues, list->league_count,
                                sizeof(*list->leagues), bt_pinnacle_league_cmp);
error:
    if (object != NULL)
        json_object_put(object);
    bt_free(json);
    return list;
}

bt_pinnacle_league *
bt_pinnacle_leagues_find(const bt_pinnacle_league_list *const list, int id)
{
    bt_pinnacle_league *pointer;
    bt_pinnacle_league storage;
    bt_pinnacle_league **found;

    pointer = &storage;
    pointer->id = id;
    found = bsearch(&pointer, list->leagues, list->league_count,
                                sizeof(*list->leagues), bt_pinnacle_league_cmp);
    if (found == NULL)
        return NULL;
    return *found;
}

static bt_pinnacle_object *
bt_pinnacle_get_fixtures(bt_pinnacle_ctx *const api)
{
    bt_pinnacle_object *root;
    bt_pinnacle_league_list *leagues;
    char *url;
    char *json;

    leagues = bt_pinnacle_get_leagues(api);
    if (leagues == NULL)
        return NULL;
    // Build the URL
    url = bt_strdup_printf(FIXTURES, api->fixtures_last);
    if (url == NULL)
        goto error;
    json = bt_http_get(url, false, api->http, api->headers);
    bt_free(url);
    if (json == NULL)
        goto error;
    root = bt_pinnacle_parse_main(json);
    if (root == NULL)
        goto error;
    api->fixtures_last = root->last;
    for (size_t idx = 0; idx < root->league_count; ++idx) {
        bt_pinnacle_league *league;
        bt_pinnacle_league *eugael;
        league = root->leagues[idx];
        if (league == NULL) // Very unlikely, but just in case
            continue;
        eugael = bt_pinnacle_leagues_find(leagues, league->id);
        if (eugael != NULL) {
            league->name = eugael->name;
            eugael->name = NULL;
            if (strstr(league->name, "WTA") != NULL) {
                league->category = CategoryWTA;
            } else if (strstr(league->name, "ATP") != NULL) {
                league->category = CategoryATP;
            } else {
                league->category = NoCategory;
            }
        }
    }
    bt_pinnacle_set_fixtures_last(api->fixtures_last);
    bt_pinnacle_leagues_free(leagues);
    bt_free(json);
    return root;
error:
    bt_pinnacle_leagues_free(leagues);
    bt_free(json);
    return NULL;
}

static void
bt_pinnacle_handle_event(bt_mysql_operation *operation, long int last,
                      bt_pinnacle_league *lg, const bt_pinnacle_event *const ev)
{
    for (size_t idx = 0; idx < ev->period_count; ++idx) {
        ssize_t result;
        char iid[11];
        bt_pinnacle_money_line *market;
        bt_pinnacle_period *p;
        p = ev->periods[idx];
        if (p == NULL) // Unlikely, but not impossible
            continue;
        if (p->number != PNMatch)
            continue;
        market = p->money_line;
        if (market == NULL)
            continue;
        result = snprintf(iid, sizeof(iid), "P%08d", ev->id);
        if ((result < 0) || (result >= sizeof(iid)))
            continue;
        bt_mysql_operation_put(operation, "%s%s%s%s%d%f%f%ld", iid, lg->name,
            ev->home, ev->away, lg->category, market->home, market->away, last);
    }
}

static void
bt_pinnacle_handler(const bt_pinnacle_object *const root)
{
    bt_mysql_transaction *transaction;
    bt_mysql_operation *operation;
    char *queries[] = {
        "INSERT INTO mercado_ganador_partido "
        "(iid, tournament, home, away, category, home_price, away_price, created)"
        " VALUES %values%"
    };
    transaction = bt_mysql_transaction_new(1, 8, queries[0]);
    if (transaction == NULL)
        return;
    operation = bt_transaction_get_operation(transaction, 0);
    for (size_t idx = 0; idx < root->league_count; ++idx) {
        bt_pinnacle_league *league;
        league = root->leagues[idx];
        if (league == NULL) // Unlikely, but not impossible
            continue;
        for (size_t jdx = 0; jdx < league->event_count; ++jdx) {
            bt_pinnacle_event *event;
            event = league->events[jdx];
            if (event == NULL) // Unlikely, but not impossible
                continue;
            // If we cannot bet into this event
            // then it's not interesting
            if ((event->status == PESUnavailable) || (event->live_status == 1))
                continue;
            bt_pinnacle_handle_event(operation, time(NULL), league, event);
        }
    }
    bt_mysql_transaction_execute(transaction);
    bt_mysql_transaction_free(transaction);
}

static void
bt_pinnacle_update(bt_pinnacle_ctx *ctx)
{
    bt_pinnacle_object *root;
    // Make a single connection for both requests
    ctx->http = bt_http_connect("https://api.pinnaclesports.com", false);
    if (ctx->http == NULL)
        goto error;
    root = bt_pinnacle_get_fixtures(ctx);
    if (root == NULL)
        goto error;
    if (bt_pinnacle_get_odds(root, ctx) == 0)
        bt_pinnacle_handler(root);
    bt_pinnacle_root_free(root);
error:
    bt_http_disconnect(ctx->http);
}

void *
bt_pinnacle_main(void *context)
{
    const char *auth;
    bt_pinnacle_ctx ctx;

    ctx.headers = bt_http_headers_new();
    if (ctx.headers == NULL)
        return NULL;
    ctx.http = NULL;
    ctx.fixtures_last = bt_pinnacle_get_fixtures_last();
    ctx.odds_last = bt_pinnacle_get_odds_last();
    // base64(AFF4280:pinn@cle87);
    // auth = "Basic QUZGNDI4MDpwaW5uQGNsZTg3";
    auth = "Basic Ukc5MzY0MTI6VHJAc3QxdDE=";
    // Set the authorization header, and accept json
    bt_http_headers_append(ctx.headers, "Accept", "application/json", true);
    bt_http_headers_append(ctx.headers, "Authorization", auth, true);

    while (bt_isrunning(context) == true) {
        bt_pinnacle_update(&ctx);
        bt_check_drops();

        bt_mysql_execute_query(UPDATE_QUERY);
        bt_sleep(30);
    }
    bt_http_headers_free(ctx.headers);
    bt_notify_thread_end();
    return NULL;
}

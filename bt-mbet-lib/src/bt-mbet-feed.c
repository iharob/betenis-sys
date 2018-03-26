#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <stdarg.h>
#include <math.h>

#include <http-connection.h>
#include <http-protocol.h>

#include <libxml/parser.h>
#include <libxml/xpath.h>

#include <bt-daemon.h>
#include <bt-oncourt-players-map.h>

#include <bt-mbet-feed.h>
#include <bt-private.h>
#include <bt-mbet.h>
#include <bt-mbet-xml.h>
#include <bt-mbet-score.h>
#include <bt-memory.h>
#include <bt-database.h>
#include <bt-util.h>
#include <bt-telegram-channel.h>
#include <bt-debug.h>

static __thread bt_oc_map_ru *atp_ru_map;
static __thread bt_oc_map_ru *wta_ru_map;

#ifdef _DEBUG
#define FEED_URL "http://www.betenis.com/feed.php?type=%s&lang=ru"
#else
#define FEED_URL "http://livefeeds.marathonbet.com/feed/betennis_%s_ru"
#endif

bt_mbet_market_descriptor s_bt_mbet_markets[] = {
    {"Ganador partido con hándicap por sets", "MTCH_HB", MATCH_HANDICAP_PER_SET}
  , {"Ganador partido con hándicap por juego", "MTCH_HBP", MATCH_HANDICAP_PER_GAME}
  , {"Resultado del partido", "MTCH_R", MATCH_RESULT}
  , {"Total de sets", "MTCH_TTLG", MATCH_TOTAL_SETS}
  , {"Total de juegos del primer set", "MTCH_TTLG1", MATCH_TOTAL_GAMES_FIRST_SET}
  , {"Total de juegos del segundo set", "MTCH_TTLG2", MATCH_TOTAL_GAMES_SECOND_SET}
  , {"Total de juegos del tercer set", "MTCH_TTLG3", MATCH_TOTAL_GAMES_THIRD_SET}
  , {"Total de juegos", "MTCH_TTLP", MATCH_TOTAL_GAMES}
};
typedef int (*bt_mbet_setter)(bt_mbet_list_item *, xmlNode *);
typedef int (*bt_mbet_apply_function)(size_t count, size_t idx, xmlNode *node, bt_mbet_list *list, bt_mbet_setter setter);

static int bt_mbet_init_group(bt_mbet_list_item *object, xmlNode *node);
static int bt_mbet_init_sport(bt_mbet_list_item *object, xmlNode *node);
static bt_mbet_feed *bt_mbet_parse_root(xmlNode *root);

static int bt_mbet_init_event(bt_mbet_list_item *object, xmlNode *node);

static int bt_mbet_init_market(bt_mbet_list_item *object, xmlNode *node);
static int bt_mbet_init_selection(bt_mbet_list_item *object, xmlNode *node);

#define HOME_XPATH ((const xmlChar *) "./members/member[@selkey=\"HOME\"]")
#define AWAY_XPATH ((const xmlChar *) "./members/member[@selkey=\"AWAY\"]")

static void
bt_mbet_free_generic_list(bt_mbet_list *list)
{
    if (list == NULL)
        return;
    for (size_t idx = 0; idx < list->count; ++idx) {
        bt_mbet_list_item *item;
        void (*free_item)(void *);
        // Make a pointer
        item = list->items[idx];
        // Get the free function
        free_item = item->freefn;
        // Free the item data
        free_item(item->data);
        // Free the item
        bt_free(item);
    }
    // Free the items container
    bt_free(list->items);
    // Free the container
    bt_free(list);
}

void *
bt_mbet_list_get_item_data(const bt_mbet_list *list, size_t idx)
{
    bt_mbet_list_item *item;
    if (idx >= list->count)
        return NULL;
    item = list->items[idx];
    if (item == NULL)
        return NULL;
    return item->data;
}

static void
bt_mbet_member_free(void *object)
{
    bt_mbet_member *member;
    if (object == NULL)
        return;
    member = object;

    bt_free(member->name);
    bt_free(member->flag);

    xmlFree(member->role);
    xmlFree(member->selkey);

    bt_free(member);
}

static void
bt_mbet_selection_free(void *object)
{
    bt_mbet_selection *selection;
    if (object == NULL)
        return;
    selection = object;
    xmlFree(selection->name);
    xmlFree(selection->uid);
    xmlFree(selection->selkey);
    bt_free(selection);
}

static void
bt_mbet_market_free(void *object)
{
    bt_mbet_market *market;
    if (object == NULL)
        return;
    market = object;

    xmlFree(market->model);
    xmlFree(market->type);
    xmlFree(market->name);
    bt_mbet_free_generic_list(market->selections);
    bt_free(market);
}

static void
bt_mbet_live_result_free(void *object)
{
    bt_mbet_score *result;
    if (object == NULL)
        return;
    result = object;
    bt_free(result->sets);
    bt_free(result);
}

static void
bt_mbet_event_free(void *object)
{
    bt_mbet_event *event;
    if (object == NULL)
        return;
    event = object;

    xmlFree(event->url);
    xmlFree(event->name);

    bt_mbet_member_free(event->home);
    bt_mbet_member_free(event->away);
    bt_mbet_free_generic_list(event->markets);
    bt_mbet_live_result_free(event->score);

    bt_free(event);
}

static void
bt_mbet_group_free(void *object)
{
    bt_mbet_group *group;
    if (object == NULL)
        return;
    group = object;

    bt_free(group->name);
    bt_free(group->court);
    bt_free(group->flag);

    bt_mbet_free_generic_list(group->events);
    bt_free(group);
}

static void
bt_mbet_sport_free(void *object)
{
    bt_mbet_sport *sport;
    if (object == NULL)
        return;
    sport = object;
    xmlFree(sport->code);
    xmlFree(sport->name);
    bt_mbet_free_generic_list(sport->groups);
    bt_free(sport);
}

void
bt_mbet_feed_free(bt_mbet_feed *feed)
{
    if (feed == NULL)
        return;
    bt_mbet_free_generic_list(feed->sports);
    bt_free(feed);
}

static int
bt_mbet_compare_groups(const void *const lhs, const void *const rhs)
{
    const bt_mbet_group *mlhs;
    const bt_mbet_group *mrhs;
    long int result;

    mlhs = (*(const bt_mbet_list_item **) lhs)->data;
    mrhs = (*(const bt_mbet_list_item **) rhs)->data;

    result = mlhs->tree_id - mrhs->tree_id;
    if (result > 0)
        return 1;
    else if (result < 0)
        return -1;
    return 0;
}

int
bt_mbet_eventcmp(const void *const lhs, const void *const rhs)
{
    const bt_mbet_event *mlhs;
    const bt_mbet_event *mrhs;
    long int result;

    mlhs = (*(const bt_mbet_list_item **) lhs)->data;
    mrhs = (*(const bt_mbet_list_item **) rhs)->data;

    result = mlhs->tree_id - mrhs->tree_id;
    if (result > 0L) {
        return 1;
    } else if (result < 0L) {
        return -1;
    }
    return 0;
}

static int
bt_mbet_compare_selections(const void *const lhs, const void *const rhs)
{
    const bt_mbet_selection *mlhs;
    const bt_mbet_selection *mrhs;
    long int result;

    mlhs = (*(const bt_mbet_list_item **) lhs)->data;
    mrhs = (*(const bt_mbet_list_item **) rhs)->data;

    result = mlhs->coeff_id - mrhs->coeff_id;
    if (result > 0)
        return 1;
    else if (result < 0)
        return -1;
    return 0;
}

static bt_mbet_list *
bt_mbet_list_new(size_t count)
{
    bt_mbet_list *list;
    if (count == 0)
        return NULL;
    list = bt_malloc(sizeof(bt_mbet_list));
    if (list == NULL)
        return NULL;
    list->count = count;
    list->items = bt_malloc(count * sizeof(*list->items));
    if (list->items == NULL) {
        // This means we can try later, now it wouldn't make sense
        // to use `list'
        bt_free(list);
        return NULL;
    }
    return list;
}

static bt_mbet_list_item *
bt_mbet_list_item_new(void *parent)
{
    bt_mbet_list_item *item;
    item = bt_malloc(sizeof(bt_mbet_list_item));
    if (item == NULL)
        return NULL;
    item->freefn = NULL;
    item->data = NULL;
    item->parent = parent;
    return item;
}

int
bt_mbet_init_list(size_t idx, xmlNode *node,
                        bt_mbet_list *list, void *parent, bt_mbet_setter setter)
{
    if (list == NULL)
        return -1;
    // If this node is `NULL' we need an empty list
    if (node != NULL) {
        // Create a new list item
        list->items[idx] = bt_mbet_list_item_new(parent);
        if (list->items[idx] == NULL)
            return -1;
        // Set item fields, if this item is rejected
        // this function will return -1
        if (setter(list->items[idx], node) != 0) {
            bt_mbet_list_item *item;
            void (*free_item)(void *);
            // Make a pointer to the item
            item = list->items[idx];
            // Get the free function
            free_item = item->freefn;
            // Free the item
            free_item(item->data);
            // Free the item
            bt_free(item);
            // Avoid problems
            list->items[idx] = NULL;
            // Return this value to allow the caller
            // to know that this failed.
            return -1;
        }
    } else {
        list->items[idx] = NULL;
    }
    return 0;
}

static xmlNode *
bt_mbet_get_node_xpath(const xmlChar *const expression, xmlNode *node)
{
    xmlNodeSet *nodes;
    xmlXPathContext *context;
    xmlXPathObject *xpath;
    context = xmlXPathNewContext(node->doc);
    if (context == NULL)
        return NULL;
    // Set the context node (the root node)
    xmlXPathSetContextNode(node, context);
    // Evaluate the xpath expression
    xpath = xmlXPathEvalExpression((const xmlChar *) expression, context);
    if (xpath == NULL)
        goto error;
    // Get the set of matching nodes
    nodes = xpath->nodesetval;
    if ((nodes == NULL) || (nodes->nodeNr != 1))
        goto error;
    node = xmlCopyNode(nodes->nodeTab[0], 1);

    xmlXPathFreeObject(xpath);
    xmlXPathFreeContext(context);

    return node;
error:
    if (xpath != NULL)
        xmlXPathFreeObject(xpath);
    xmlXPathFreeContext(context);
    return NULL;
}

static bt_mbet_list *
bt_mbet_nodes_foreach(xmlNode *node, const xmlChar *const expression,
                                            void *parent, bt_mbet_setter setter)
{
    bt_mbet_list *list;
    xmlNodeSet *nodes;
    xmlXPathContext *context;
    xmlXPathObject *xpath;
    xmlNode **table;
    size_t count;
    // Set this to 0
    count = 0;
    // Ensure this is `NULL'
    list = NULL;
    // If there is no xpath expression and no `apply` function then
    // this is a single ??? I have no fucking idea
    if (expression == NULL)
        return NULL;
    // Safety first
    if (node == NULL)
        return NULL;
    // Make a new context for the xpath query
    context = xmlXPathNewContext(node->doc);
    if (context == NULL)
        return NULL;
    // Set the context node (the root node)
    xmlXPathSetContextNode(node, context);
    // Evaluate the xpath expression
    xpath = xmlXPathEvalExpression((const xmlChar *) expression, context);
    if (xpath == NULL)
        goto error;
    // Get the set of matching nodes
    nodes = xpath->nodesetval;
    if (nodes == NULL)
        goto error;
    list = bt_mbet_list_new(nodes->nodeNr);
    if (list == NULL)
        goto done;
    table = nodes->nodeTab;
    // Apply the init function to every node
    for (size_t i = 0; i < nodes->nodeNr; ++i) {
        int result;
        // If this function returns a non-zero value it means that
        // this item was not inserted into the list
        result = bt_mbet_init_list(count, table[i], list, parent, setter);
        if (result != 0)
            continue;
        count += 1;
    }
done:
    if (list != NULL)
        list->count = count;
error:
    if (xpath != NULL)
        xmlXPathFreeObject(xpath);
    xmlXPathFreeContext(context);
    return list;
}

static void
bt_mbet_sort_list(bt_mbet_list *list,
                               int (*cmp)(const void *const, const void *const))
{
    if (list == NULL)
        return;
    qsort(list->items, list->count, sizeof(*list->items), cmp);
}

static int
bt_mbet_init_selection(bt_mbet_list_item *item, xmlNode *node)
{
    bt_mbet_selection *selection;
    // Allocate space
    item->freefn = bt_mbet_selection_free;
    item->data = bt_malloc(sizeof(*selection));
    if (item->data == NULL)
        return -1;
    // Make a poitner with the appropriate type
    selection = item->data;
    // Fill the structure
    selection->name = bt_mbet_get_string_property(node, "name");
    selection->value = bt_mbet_get_float_property(node, "value");
    selection->coeff_id = bt_mbet_get_long_property(node, "coeffId");
    selection->coeff = bt_mbet_get_float_property(node, "coeff");
    selection->selkey = bt_mbet_get_string_property(node, "selkey");
    selection->score_home = bt_mbet_get_integer_property(node, "scoreHome");
    selection->score_away = bt_mbet_get_integer_property(node, "scoreAway");
    selection->uid = bt_mbet_get_string_property(node, "uid");

    return 0;
}

static int
bt_mbet_init_market(bt_mbet_list_item *item, xmlNode *node)
{
    bt_mbet_market *market;
    // Allocate space
    item->freefn = bt_mbet_market_free;
    item->data = bt_malloc(sizeof(*market));

    if (item->data == NULL)
        return -1;
    // Make a poitner with the appropriate type
    market = item->data;
    // Ensure this is null in case no selections are found
    market->selections = NULL;
    // Fill other members
    market->model = bt_mbet_get_string_property(node, "model");
    market->name = bt_mbet_get_string_property(node, "name");
    market->type = bt_mbet_get_string_property(node, "type");
    market->value = bt_mbet_get_float_property(node, "value");
    // Get all the selections for this market
    market->selections = bt_mbet_nodes_foreach(node,
                     (const xmlChar *) "./sel", market, bt_mbet_init_selection);
    // Sort selections to find them quickly
    bt_mbet_sort_list(market->selections, bt_mbet_compare_selections);
    return 0;
}

int
bt_get_player_name_from_id(bt_mbet_member *member)
{
    MYSQL_STMT *stmt;
    const char *category_name;
    int result;
    char name[256];
    char flag[4];
    char *query;
    int ranking;

    category_name = bt_get_category_name(member->category);

    member->name = NULL;
    member->flag = NULL;

    if (category_name == NULL)
        return -1;
    query = bt_load_query("player data", "%category%", category_name, NULL);
    if (query == NULL)
        return -1;
    stmt = bt_mysql_easy_query(query, "%d|%256a%4a%d",
                                           &member->ocid, name, flag, &ranking);
    bt_free(query);
    if (stmt == NULL)
        return -1;
    result = mysql_stmt_fetch(stmt);

    mysql_stmt_free_result(stmt);
    mysql_stmt_close(stmt);

    if (result == 0) {
        member->name = bt_strdup(name);
        member->flag = bt_strdup(flag);
        member->ranking = ranking;
    }

    if (result == 100)
        fprintf(stderr, "cannot find `%d' in the database\n", member->ocid);
    return result;
}

static int
bt_get_player_from_mbet(bt_tennis_category *category, const char *const name)
{
    struct bt_oc_loader {
        bt_oc_map_ru **map;
        bt_tennis_category category;
        bt_oc_map_ru *(*loader)(void);
    } tables[2] = {
        {&atp_ru_map, CategoryATP, bt_oc_map_load_atp},
        {&wta_ru_map, CategoryWTA, bt_oc_map_load_wta}
    };

    for (size_t idx = 0; idx < countof(tables); ++idx) {
        struct bt_oc_loader *table;
        bt_oc_map_item_ru *item;
        table = &tables[idx];
        if (*(table->map) == NULL)
            *(table->map) = table->loader();
        item = bt_oc_map_ru_find(*(table->map), name);
        if (item == NULL)
            continue;
        // Set it to `ATP`, if it is we return and all is done
        *category = table->category;
        // This was it, return the id
        return bt_oc_map_item_ru_get_id(item);
    }
    return -1;
}

static bt_mbet_member *
bt_mbet_init_member(xmlNode *node)
{
    bt_mbet_member *member;
    char *name;
    // Allocate space
    member = bt_calloc(1, sizeof(bt_mbet_member));
    if (member == NULL)
        return NULL;
    // Fill the structure members
    name = bt_mbet_get_string_property(node, "name");
    if (name == NULL)
        goto error;
    member->selkey = bt_mbet_get_string_property(node, "selkey");
    member->id = bt_mbet_get_long_property(node, "id");
    member->role = bt_mbet_get_string_property(node, "role");
    member->ocid = bt_get_player_from_mbet(&member->category, name);
    if (member->ocid == -1)
        goto error;
    if (bt_get_player_name_from_id(member) == -1)
        goto error;
    xmlFree(name);
    return member;
error:
    if (name != NULL)
        xmlFree(name);
    bt_mbet_member_free(member);
    return NULL;
}

static int
bt_mbet_update_tournament_id(bt_mbet_group *group, bt_mbet_event *event)
{
    bt_mbet_member *home;
    bt_mbet_member *away;

    home = event->home;
    away = event->away;

    if (home->category != away->category) {
        log("error: categories do not match, this is weird.\n");
        return -1;
    }

    event->category = home->category;

    // Try to guess to tournament id from the players
    bt_get_tournament_id_from_players(event->category, home->ocid,
                   away->ocid, &event->octour, &event->ocround, &event->ocrank);
    switch (event->ocrank) {
    case 0:
        event->category |= CategoryITF;
        break;
    case 1:
        event->category |= CategoryChallenger;
        break;
    case 2:
    case 3:
    case 4:
        break;
    }

    bt_database_get_tournament_name(event->category,
                      event->octour, &group->name, &group->flag, &group->court);
    group->category = event->category;
    group->ocround = event->ocround;
    group->ocid = event->octour;
    group->ocrank = event->ocrank;

    return (event->octour != -1);
}

static void
bt_mbet_get_player_odds(const bt_mbet_member *const home,
                          const bt_mbet_member *const away, int tour, int round)
{
    MYSQL_STMT *stmt;
    char *query;
    const char *category;

    category = bt_get_category_name(home->category);
    query = bt_load_query("odds", "%category%", category, NULL);
    if (query == NULL)
        return;
    stmt = bt_mysql_easy_query(query, "%d%d%d%d%d%d%d%d|%f%f",
                               &home->ocid, &home->ocid,
                               &home->ocid, &away->ocid,
                               &away->ocid, &away->ocid,
                               &tour, &round,
                               &home->odds, &away->odds
    );
    bt_free(query);
    if (stmt == NULL)
        return;
    mysql_stmt_fetch(stmt);
    mysql_stmt_free_result(stmt);
    mysql_stmt_close(stmt);
}

static int
bt_mbet_init_event(bt_mbet_list_item *item, xmlNode *node)
{
    bt_mbet_event *event;
    bt_mbet_group *group;
    xmlNode *member;
    if (item->parent == NULL)
        return -1;
    // Allocate space
    item->freefn = bt_mbet_event_free;
    item->data = bt_malloc(sizeof(bt_mbet_event));
    if (item->data == NULL)
        return -1;
    // Make a poitner with the appropriate type
    event = item->data;
    // Ensure this can be tested against `NULL'
    event->markets = NULL;
    event->home = NULL;
    event->away = NULL;
    // Fill the structure members
    event->name = bt_mbet_get_string_property(node, "name");
    event->tree_id = bt_mbet_get_long_property(node, "treeId");
    event->url = bt_mbet_get_node_conent_string(node, "./url");
    event->score = bt_score_parse_mbet(node);
    // Get home member
    member = bt_mbet_get_node_xpath(HOME_XPATH, node);
    if (member == NULL)
        return -1;
    event->home = bt_mbet_init_member(member);
    xmlFreeNode(member);
    if (event->home == NULL)
        return -1;
    // Get away member
    member = bt_mbet_get_node_xpath(AWAY_XPATH, node);
    if (member == NULL)
        return -1;
    event->away = bt_mbet_init_member(member);
    xmlFreeNode(member);
    if (event->away == NULL)
        return -1;
    // Get the list of all markets for this event
    event->markets = bt_mbet_nodes_foreach(node,
              (const xmlChar *) "./markets/market", event, bt_mbet_init_market);
    // Extract the date
    bt_mbet_get_date_property(node, "date", &event->date);
    // Update the parent (the group) with data that is only available
    // in the events.
    //
    // We check if the `octour` has been set and it would mean
    // that we already did this. Avoiding to query the database
    // quite a large number of times is necessary.
    group = item->parent;
    // Get the category for this member
    // TODO: It would be nice to check if the other member belongs
    //       to the same category. In case it doesn't, some error
    //       happened and we could inspect what was it.
    if ((group->ocid == -1) && (bt_mbet_update_tournament_id(group, event) == -1))
        goto error;
    event->ocround = group->ocround;
    event->octour = group->ocid;
    event->ocrank = group->ocrank;
    event->group = group;
    event->category = group->category;

    bt_mbet_get_player_odds(event->home, event->away, group->ocid, group->ocround);
    // This means that this item is added to it's parent's list
    return 0;
error:
    bt_mbet_event_free(event);
    return -1;
}

static int
bt_mbet_init_group(bt_mbet_list_item *item, xmlNode *node)
{
    const xmlChar *expression;
    bt_mbet_group *group;
    // Set the free function
    item->freefn = bt_mbet_group_free;
    // Allocate space
    item->data = bt_malloc(sizeof(bt_mbet_group));
    if (item->data == NULL)
        return -1;
    // Make a poitner with the appropriate type
    group = item->data;
    // This is the expression for the events children
    expression = (const xmlChar *) "./events/event";
    // Group name
    group->name = NULL;
    group->flag = NULL;
    // Make sure, the events field setter can test this in order to
    // initialize the other membrs, that depend on this
    group->ocid = -1;
    group->ocround = -1;
    group->ocrank = -1;
    group->court = NULL;
    // An extra thing to know!!!
    group->altitude = 0.0;
    // If everything goes as expected, this will be overwritten
    // in the next line
    group->category = NoCategory;
    // Get all the events for this group
    group->events = bt_mbet_nodes_foreach(node,
                                         expression, group, bt_mbet_init_event);
    // Fill other members
    group->tree_id = bt_mbet_get_long_property(node, "treeId");
    group->is_american = bt_mbet_get_integer_property(node, "isAmerican");
    // Sort the events by id, so we can quickly find them
    bt_mbet_sort_list(group->events, bt_mbet_eventcmp);
    // This means that this item is added to it's parent's list
    return 0;
}

static int
bt_mbet_init_sport(bt_mbet_list_item *item, xmlNode *node)
{
    const xmlChar *expression;
    bt_mbet_sport *sport;
    // Allocate space
    item->freefn = bt_mbet_sport_free;
    item->data = bt_malloc(sizeof(bt_mbet_sport));
    if (item->data == NULL)
        return -1;
    // Make a poitner with the appropriate type
    sport = item->data;
    // This is the expression to get all the group children
    expression = (const xmlChar *) "./groups/group";
    // Ensure this is NULL
    sport->groups = NULL;
    // Fill the structure
    sport->code = bt_mbet_get_string_property(node, "code");
    sport->name = bt_mbet_get_string_property(node, "name");
    // List all the groups in this ssport
    sport->groups = bt_mbet_nodes_foreach(node,
                                         expression, sport, bt_mbet_init_group);
    // Sort the groups so we can quickly find one
    bt_mbet_sort_list(sport->groups, bt_mbet_compare_groups);
    return 0;
}

static bt_mbet_feed *
bt_mbet_parse_root(xmlNode *root)
{
    const xmlChar *expression;
    bt_mbet_feed *result;
    // TODO: This xpath will match all the "sports" in the lsit
    //       only tennis is listed for this software so we must
    //       remove this from the tree and start at groups.
    //
    //       It does serve as the root node though, and for now
    //       there is no interest in fixing this.
    expression = (const xmlChar *) "//sport";
    // Allocate space for the result object
    result = bt_malloc(sizeof(*result));
    if (result == NULL)
        return NULL;
    // List all the sports
    result->sports = bt_mbet_nodes_foreach(root,
                                          expression, NULL, bt_mbet_init_sport);
    // Return the `bt_mbet_live` object
    return result;
}

static bt_mbet_feed *
bt_mbet_parse_document(xmlChar *data, bt_mbet_feed_handler_fn handler)
{
    bt_mbet_feed *live;
    xmlDoc *document;
    xmlNode *root;
    // Make a document from the raw XML
    document = xmlParseDoc(data);
    if (document == NULL)
        return NULL;
    // Make a pointer to the root element
    root = xmlDocGetRootElement(document);
    live = NULL;
    if (root != NULL) {
        // Parse the document, from the root element
        live = bt_mbet_parse_root(root);
        if ((live != NULL) && (handler != NULL)) {
            // Handle the whole `bt_mbet_live` object
            handler(live);
        }
    }
    // Destroy the XML document
    xmlFreeDoc(document);
    return live;
}

bt_mbet_feed *
bt_mbet_feed_get(enum bt_mbet_feed_type ft, bt_mbet_feed_handler_fn handler)
{
    bt_mbet_feed *live;
    const char *type[2] = {"pre", "liv"};
    char *url;
    char *xml;
    // Connect to the mbet feed.
    // TODO: This should be the mbet url instead, but
    //       since we are still testing this software
    //       we have a wrapper script at `www.betenis.com'
    //       and we use it meanwhile.
    //
    //       The release version of this software
    //       should use the original url.
    live = NULL;
    // Build the URL
    url = bt_strdup_printf(FEED_URL, type[ft]);
    if (url == NULL)
        return NULL;
    // Perform the GET request
    xml = bt_http_get(url, false, NULL, NULL);
    if (xml == NULL)
        goto error;
    live = bt_mbet_parse_document((xmlChar *) xml, handler);
error:
    bt_free(xml);
    bt_free(url);
    return live;
}

static void
bt_mbet_generic_group_handler(const bt_mbet_group *const group,
                                   bt_mbet_event_handler_fn handler, void *data)
{
    bt_mbet_list *events;
    events = group->events;
    for (size_t edx = 0; edx < events->count; ++edx) {
        bt_mbet_list_item *item;
        item = events->items[edx];
        if (item == NULL)
            continue;
        handler(item->data, data);
    }
}

void
bt_mbet_generic_sport_handler(const bt_mbet_sport *const sport,
                                   bt_mbet_event_handler_fn handler, void *data)
{
    bt_mbet_list *groups;
    groups = sport->groups;
    for (size_t gdx = 0; gdx < groups->count; ++gdx) {
        bt_mbet_list_item *item;
        item = groups->items[gdx];
        if (item == NULL)
            continue;
        bt_mbet_generic_group_handler(item->data, handler, data);
    }
}

static size_t
bt_mbet_group_count_events(bt_mbet_group *group)
{
    bt_mbet_list *list;
    list = group->events;
    if (list != NULL)
        return list->count;
    return 0;
}

static size_t
bt_mbet_sport_count_events(bt_mbet_sport *sport)
{
    bt_mbet_list *list;
    size_t count;
    count = 0;
    list = sport->groups;
    for (size_t gdx = 0; gdx < list->count; ++gdx) {
        bt_mbet_list_item *item;
        item = list->items[gdx];
        if (item == NULL)
            continue;
        count += bt_mbet_group_count_events(item->data);
    }
    return count;
}

size_t
bt_mbet_count_events(const bt_mbet_feed *const live)
{
    bt_mbet_list *sports;
    size_t count;
    // Initialize `count'
    count = 0;
    // Make a poitner to the sports object
    sports = live->sports;
    // Start counting events, for each sport
    for (size_t sdx = 0; sdx < sports->count; ++sdx) {
        bt_mbet_list_item *item;
        item = sports->items[sdx];
        if (item == NULL)
            continue;
        count += bt_mbet_sport_count_events(item->data);
    }
    return count;
}

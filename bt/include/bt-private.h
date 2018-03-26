#ifndef __BT_MBET_PRIVATE_HEADER_H__
#define __BT_MBET_PRIVATE_HEADER_H__

#include <bt-util.h>
#include <stdlib.h>
#include <time.h>

typedef struct bt_mbet_list bt_mbet_list;
typedef struct bt_mbet_list_item {
    void *parent;
    void *data;
    /* Free function */
    void (*freefn)(void *);
} bt_mbet_list_item;

typedef struct bt_mbet_list {
    bt_mbet_list_item **items;
    size_t count;
} bt_mbet_list;

typedef struct bt_mbet_market bt_mbet_market;
typedef struct bt_mbet_selection {
    char *name;
    float coeff;
    float value;
    long int coeff_id;
    char *selkey;
    char *uid;
    int score_home;
    int score_away;
} bt_mbet_selection;

typedef struct bt_mbet_event bt_mbet_event;
typedef struct bt_mbet_market {
    /* porperties */
    char *model;
    char *name;
    char *type;
    /* Value */
    float value;
    /* child nodes */
    bt_mbet_list *selections;
} bt_mbet_market;

typedef struct bt_mbet_member {
    long int id;
    char *name;
    char *role;
    int ocid;
    char *selkey;
    char *flag;
    float odds;
    int ranking;
    bt_tennis_category category;
} bt_mbet_member;

typedef struct bt_mbet_score_item {
    int8_t home;
    int8_t away;
} bt_mbet_score_item;

typedef struct bt_mbet_score {
    bt_mbet_score_item *sets;
    bt_mbet_score_item score;
    bt_mbet_score_item game;
    int8_t nsets;
    uint8_t service;
} bt_mbet_score;

typedef struct bt_mbet_group bt_mbet_group;
typedef struct bt_mbet_event {
    /* properties */
    long int tree_id;
    char *name;
    struct tm date;
    /* child nodes */
    char *url;
    bt_mbet_score *score;
    bt_mbet_member *home;
    bt_mbet_member *away;
    bt_mbet_list *markets;

    const bt_mbet_group *group;

    /* oncourt **/
    int ocround;
    int ocrank;
    int octour;

    bt_tennis_category category;
} bt_mbet_event;

typedef struct bt_mbet_sport bt_mbet_sport;
typedef struct bt_mbet_group {
    bt_mbet_list *events;

    long int tree_id;
    char *name;
    char *flag;
    bool is_american;

    /* OnCourt */
    int ocround;
    int ocid;
    int ocrank;

    char *court;
    /* Extra data */
    float altitude;

    bt_tennis_category category;
} bt_mbet_group;

typedef struct bt_mbet_feed bt_mbet_feed;
typedef struct bt_mbet_sport {
    bt_mbet_list *groups;

    char *code;
    char *name;
} bt_mbet_sport;

typedef struct bt_mbet_feed {
    bt_mbet_list *sports;
} bt_mbet_feed;

typedef enum bt_mbet_feed_type {
    PreMatchFeed,
    LiveFeed,
    FeedTypesCount
} bt_mbet_feed_type;

enum bt_mbet_market_types {
    MATCH_INVALID_MARKET,
    MATCH_RESULT,
    MATCH_TOTAL_SETS,
    MATCH_TOTAL_GAMES,
    MATCH_TOTAL_GAMES_FIRST_SET,
    MATCH_TOTAL_GAMES_SECOND_SET,
    MATCH_TOTAL_GAMES_THIRD_SET,
    MATCH_HANDICAP_PER_SET,
    MATCH_HANDICAP_PER_GAME
};

typedef struct bt_mbet_market_descriptor {
    const char *message;
    const char *model;
    enum bt_mbet_market_types type;
} bt_mbet_market_descriptor;

extern bt_mbet_market_descriptor s_bt_mbet_markets[];


typedef struct bt_websocket_connection {
    struct httpio *ws;
    bt_context *context;
} bt_websocket_connection;

void *bt_mbet_list_get_item_data(const bt_mbet_list *list, size_t idx);
#endif // __BT_MBET_PRIVATE_HEADER_H__

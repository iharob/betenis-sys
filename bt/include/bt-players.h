#ifndef __BETENIS_PLAYERS_H__
#define __BETENIS_PLAYERS_H__

#include <bt-util.h>

typedef struct bt_score_set {
    int8_t games;
} bt_score_set;

typedef struct bt_score {
    bt_score_set sets[5];
} bt_score;

typedef struct bt_player {
    int c_mto_count;
    int t_mto_count;
    bt_score score;
    char *name;
    int id;
    int last;
    int serving;
} bt_player;

#endif // __BETENIS_PLAYERS_H__

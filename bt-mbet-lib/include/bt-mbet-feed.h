#ifndef __bt_mbet_live_results_FEED__
#define __bt_mbet_live_results_FEED__

/** @file
 */

#include <stdbool.h>
#include <time.h>


#include <bt-util.h>

enum bt_mbet_feed_type;

typedef struct bt_mbet_feed bt_mbet_feed;
typedef struct bt_mbet_member bt_mbet_member;
typedef struct bt_mbet_sport bt_mbet_sport;
typedef struct bt_mbet_event bt_mbet_event;
typedef struct bt_mbet_list bt_mbet_list;

typedef void (*bt_mbet_sport_handler_fn)(const bt_mbet_sport *const ,void *);
typedef void (*bt_mbet_event_handler_fn)(const bt_mbet_event *const ,void *);
typedef void (*bt_mbet_feed_handler_fn)(const bt_mbet_feed *const);

bt_mbet_feed *bt_mbet_feed_get(enum bt_mbet_feed_type feed_type, bt_mbet_feed_handler_fn handler);
void bt_mbet_feed_free(bt_mbet_feed *feed);
void bt_mbet_generic_sport_handler(const bt_mbet_sport *const sport, bt_mbet_event_handler_fn handler, void *data);
size_t bt_mbet_count_events(const bt_mbet_feed *const live);
int bt_mbet_eventcmp(const void * const, const void * const);

int bt_get_player_name_from_id(bt_mbet_member *member);
#endif /* __bt_mbet_live_results_FEED__ */

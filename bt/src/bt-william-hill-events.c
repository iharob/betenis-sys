#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <time.h>
#include <math.h>
#include <sys/time.h>

#include <pthread.h>

#include <http-connection.h>
#include <http-protocol.h>

#include <bt-william-hill-events.h>
#include <bt-database.h>
#include <bt-players.h>
#include <bt-util.h>
#include <bt-memory.h>
#include <bt-debug.h>
#include <bt-william-hill.h>
#include <bt-string-builder.h>
#include <bt-context.h>

typedef struct bt_event {
    int id;
    bt_tennis_category category;
    char *tour;
    bt_player *players[2];
    int current_set;
    int ready4incidents;
    bool subscribed;
    char *date;
} bt_event;

typedef struct bt_event_list {
    bt_event **items;
    int size;
    int count;
} bt_event_list;

static int
bt_william_hill_compare_events(const void *const _a, const void *const _b)
{
    const bt_event *a;
    const bt_event *b;
    // Cast the pointers so we can dereference them correctly
    a = *((const bt_event **) _a);
    b = *((const bt_event **) _b);
    // Check for `NULL`, unlikely but not impossible
    if ((a == NULL) || (b == NULL))
        return (a != NULL) ? -1 : 1;
    // Compare two events by id
    return a->id - b->id;
}

bt_event *
bt_william_hill_event_list_find(bt_event_list *list, int value)
{
    bt_event **found;
    bt_event find;
    bt_event *pointer;
    // Sanity check
    if (list == NULL)
        return NULL;
    // Build the `key` parameter for `bsearch()`
    find.id = value;
    // Make a pointer to pass a pointer to pointer
    pointer = &find;
    // Execute binary search
    found = bsearch(&pointer, list->items, list->count,
                    sizeof(*list->items), bt_william_hill_compare_events);
    // Check the result before dereferencing
    if (found == NULL)
        return NULL;
    // Return a pointer to the found element
    return *found;
}

void
bt_william_hill_event_list_remove(bt_event_list *const list,
                                             const bt_event *const event)
{
    int count;
    // FIXME: write an alternative version of this function, using binary search

    // Save current elements count
    count = list->count;
    // Iterate through all the elements until we find the one we want
    for (int index = 0; index < count; ++index) {
        bt_event *current;
        size_t size;
        // Make a pointer to the "current" element
        current = list->items[index];
        // Check it's id, if it's not the element we want, continue
        if (event->id != current->id)
            continue;
        log("removing event: \033[34m%d\033[0m\n", event->id);
        // Calculate the size we must move the list of items
        size = (count - index - 1) * sizeof(*list->items);
        if (size != 0)
            memmove(&list->items[index], &list->items[index + 1], size);
        // Free the event's memory
        bt_william_hill_event_free(current);
        // Update the count
        list->count = count - 1;
        // It's over, nothing else to do here
        return;
    }
}

static void
bt_player_free(bt_player *player)
{
    if (player == NULL)
        return;
    bt_free(player->name);
    bt_free(player);
}

void
bt_william_hill_event_free(bt_event *event)
{
    if (event == NULL)
        return;
    bt_free(event->tour);
    // Free players memory
    bt_player_free(event->players[0]);
    bt_player_free(event->players[1]);
    bt_free(event->date);
    // Free the event memory
    bt_free(event);
}

void
bt_william_hill_event_list_clear(bt_event_list *list)
{
    if (list == NULL)
        return;
    // Iterate through the events freeing each one
    for (size_t i = 0; i < list->count; ++i)
        bt_william_hill_event_free(list->items[i]);
    // Free the container of list items
    bt_free(list->items);
    // Update the count
    list->count = 0;
}


void
bt_william_hill_event_list_free(bt_event_list *list)
{
    if (list == NULL)
        return;
    // Clear the list
    bt_william_hill_event_list_clear(list);
    // Free the actual obejct
    bt_free(list);
}

static int
bt_william_hill_event_list_resize(bt_event_list *list)
{
    bt_event **items;
    size_t size;
    // Always duplicate the list size. Note, this will make the list very
    // big fairly quickly, so it will be called 3 times maximum in case of
    // very special situations. Normally just a single call per list.
    size = 2 * list->size;
    // Allocate the new space
    items = bt_realloc(list->items, size * sizeof(*items));
    if (items == NULL)
        return -1;
    list->items = items;
    // Update the list size
    list->size = size;
    // Return success
    return 0;
}

void
bt_william_hill_event_list_append(bt_event_list *list, bt_event *event)
{
    // Check if we need to resize the list, and in case of resize failure
    // simply return.
    if ((list->count + 1 >= list->size) &&
                                 (bt_william_hill_event_list_resize(list) == -1)) {
        // FIXME: perhaps we should log a warning here
        return;
    }
    // Insert the event into the list, and increase `count` by 1
    list->items[list->count++] = event;
}

static bool
bt_william_hill_event_extract_name_and_id_from_json(json_object *object, int *id)
{
    json_object *item;

    if (json_object_object_get_ex(object, "event", &item) == false)
        return false;
    *id = json_object_get_int(item);
    return true;
}

static bool
bt_william_hill_check_event_is_running(json_object *object)
{
    json_object *item;
    // Check if this is a running event
    if (json_object_object_get_ex(object, "is_running", &item) == false)
        return 0;
    // Boolean value
    return (json_object_get_int(item) == 1);
}

static bt_player *
bt_william_hill_json_event_get_players(json_object *object, int event, int index)
{
    bt_player *player;
    json_object *selections;
    json_object *selection;
    json_object *item;
    int id;
    const char *name;
    // Get the `selections' object
    if (json_object_object_get_ex(object, "selections", &selections) == false)
        return NULL;
    // Check it's length, it has to be 2 to find the players
    if (json_object_array_length(selections) != 2)
        return NULL;
    // Get the appropriate element from the array
    selection = json_object_array_get_idx(selections, index);
    if (selection == NULL)
        return NULL;
    // Now get the `name' attribute from the `selection' object
    if (json_object_object_get_ex(selection, "ev_oc_id", &item) == false)
        return NULL;
    id = json_object_get_int(item);
    // Now get the `name' attribute from the `selection' object
    if (json_object_object_get_ex(selection, "name", &item) == false)
        return NULL;
    // Extract the value as a string
    name = json_object_get_string(item);
    if (name == NULL)
        return NULL;
    // Allocate space for the new `bt_player' object
    player = bt_malloc(sizeof(*player));
    if (player == NULL)
        return NULL;
    // Initialize everything
    player->t_mto_count = bt_database_count_mto(event, name);
    player->c_mto_count = player->t_mto_count;
    player->name = bt_strdup(name);
    player->id = id;
    player->last = 0;
    player->serving = false;

    memset(&player->score, 0, sizeof(player->score));
    return player;
}

char *
bt_william_hill_get_tournament_name(json_object *object)
{
    json_object *type;
    json_object *type_name;
    const char *name;
    if (json_object_object_get_ex(object, "type", &type) == false)
        return NULL;
    if (json_object_object_get_ex(type, "type_name", &type_name) == false)
        return NULL;
    name = json_object_get_string(type_name);
    if (name == NULL)
        return NULL;
    return bt_strdup(name);
}

static bt_tennis_category
bt_william_hill_get_category(const char *const name)
{
    bt_tennis_category category;
    category = NoCategory;
    // Check for every possible category
    if (strcasestr(name, "itf") != NULL) {
        category = CategoryITF;
    } else if ((strcasestr(name, "wta") != NULL) || (strcasestr(name, "women") != NULL)) {
        category = CategoryWTA;
    } else if ((strcasestr(name, "atp") != NULL) || (strcasestr(name, "men") != NULL)) {
        category = CategoryATP;
    }
    // Check for doubles
    if (strcasestr(name, "doubles") != NULL) {
        category |= DoublesMask;
    }
    return category;
}

char *
bt_william_hill_get_event_date(json_object *object)
{
    json_object *start_time;
    const char *string;
    if (json_object_object_get_ex(object, "start_time", &start_time) == false)
        return NULL;
    string = json_object_get_string(start_time);
    if (string == NULL)
        return NULL;
    return bt_strdup(string);
}

static bt_event *
bt_william_hill_create_event(json_object *object, long int id)
{
    bt_event *event;
    bt_player *players[2];
    char *tourname;
    // Obtain the internal "oncourt" database ids of these players
    players[0] = bt_william_hill_json_event_get_players(object, id, 0);
    players[1] = bt_william_hill_json_event_get_players(object, id, 1);
    // In case of failure, for example the players were not found in the
    // database or they are in different categories, these pointers would
    // be `NULL`
    if ((players[0] == NULL) || (players[1] == NULL))
        goto error;
    tourname = bt_william_hill_get_tournament_name(object);
    if (tourname == NULL)
        goto error;
    // All is ok, so allocate memory now
    event = bt_malloc(sizeof(*event));
    // In case memory was allocated, fill the structure
    if (event != NULL) {
        event->subscribed = false;
        event->ready4incidents = 2;
        event->tour = tourname;
        event->id = id;
        event->players[0] = players[0];
        event->players[1] = players[1];
        event->current_set = -1;
        event->category = bt_william_hill_get_category(tourname);
        event->date = bt_william_hill_get_event_date(object);
        if (event->category == NoCategory) {
            log("warning: cannot determine the category of `%s'\n", tourname);
        }
    }
    return event;
error:
    // Something bad occurred, this is a long running program so clean up
    bt_free(players[0]);
    bt_free(players[1]);

    return NULL;
}

static json_object *
bt_william_hill_extract_json_from_string(const char *const json, size_t length)
{
    json_tokener *tokener;
    json_object *object;
    // Create a new Json tokener
    tokener = json_tokener_new();
    if (tokener == NULL)
        return NULL;
    // Parse the Json string `json`
    object = json_tokener_parse_ex(tokener, json, length);
    if (object == NULL)
        goto error;
error:
    // We should clean up now
    json_tokener_free(tokener);
    return object;
}

static void
bt_william_hill_next_json_from_html(const char *const data,
                                           const char **head, const char **tail)
{
    char current;
    const char *next;
    size_t count;

    // Initialize the return values to reflect failure
    *head = NULL;
    *tail = NULL;
    // Search for the next '{' character
    next = strchr(data, '{');
    if (next == NULL)
        return;
    // Point to it with head since this means we migth have Json here
    *head = next;
    // Initialize the count to '1' because we already found a '{'
    count = 1;
    while ((count != 0) && ((current = *(++next)) != '\0')) {
        switch (current) {
        case '{': // Increase the open braces count
            count += 1;
            break;
        case '}': // Decrease the open braces count (we found a close)
            count -= 1;
            break;
        }
    }
    // If we're here to things could have happened:
    //
    //   1. `count` is 0, so we have a candidate Json object.
    //   2. We reached the end of the string, so we must make
    //      `*tail` `NULL`.
    if (*next == '\0')
        *tail = NULL;
    else  // Point to the next element
        *tail = next + 1;
}

static bt_event *
bt_william_hill_extract_event_from_json(json_object *object)
{
    bt_event *event;
    int id;
    // If it's not a "running" event, it's not interesting
    if (bt_william_hill_check_event_is_running(object) == false)
        return NULL;
    // Ensure this because we are returning this pointer
    event = NULL;
    // Check if we can understand this Json object and extract
    // the William Hill event `id`
    if (bt_william_hill_event_extract_name_and_id_from_json(object, &id) == true) {
        // Build the `event` object because this can be a good
        // Json object. More checks will be performed later.
        event = bt_william_hill_create_event(object, id);
    }
    return event;
}

void
bt_william_hill_events_json_handler(json_object *object,
                                               bt_event_list *const list)
{
    bt_event *event;
    // Extract the event from the Json obejct
    event = bt_william_hill_extract_event_from_json(object);
    if (event == NULL)
        return;
    // It was extracted correctly, so append it to the list
    bt_william_hill_event_list_append(list, event);
}

static void
bt_william_hill_extract_events_helper(const char *const html,
                                               bt_event_list *const list)
{
    const char *head;
    const char *tail;
    const char *magic;
    // This is a javascript function that creates events on the
    // william hill site, we scan the HTML and every call to
    // this function contains a parameter which is a Json object
    // that has the needed information to ask the websocket about
    // the event and start "watching" it.
    magic = "document.aip_list.create_prebuilt_event";
    // Point to the start of the HTML
    head = html;
    // Start searching for occurrences of `magic' in the HTML
    while ((head != NULL) && ((tail = strstr(head, magic)) != NULL)) {
        // Try to extract the Json string. If we succeed `tail` will
        // be NOT-NULL.
        bt_william_hill_next_json_from_html(tail, &head, &tail);
        // If tail is `NULL' we got nothing so continue to the next item
        if (tail != NULL) {
            json_object *object;
            // Parse the Json string
            object = bt_william_hill_extract_json_from_string(head, tail - head);
            // Check if we obtained a valid Json obejct
            if (object != NULL) {
                // Try to insert this potential event into the list
                bt_william_hill_events_json_handler(object, list);
                // Clean up the temporary Json obejct
                json_object_put(object);
            }
            // Advance from the next element
            head = tail + 1;
        } else {
            // This will end the loop
            head = NULL;
        }
    }
}

bt_event_list *
bt_william_hill_event_list_new()
{
    bt_event_list *list;
    // Allocate the new list object
    list = bt_malloc(sizeof((*list)));
    if (list == NULL)
        return NULL;
    // Initialize and fill the structure
    list->count = 0;
    // Allocate space for list elements
    list->items = bt_malloc(200 * sizeof(*list->items));
    if (list->items != NULL)
        list->size = 200;
    else
        list->size = 0;
    // Return the new list
    return list;
}

static bt_event_list *
bt_william_hill_extract_events(const char *const data)
{
    bt_event_list *list;
    // Sanity check
    if (data == NULL)
        return NULL;
    // Create a new events list
    list = bt_william_hill_event_list_new();
    if (list == NULL)
        return NULL;
    // Parse the HTML string to extract list elements
    bt_william_hill_extract_events_helper(data, list);
    // Return the potential list (it can be empty)
    return list;
}

void
bt_william_hill_event_list_sort(bt_event_list *list)
{
    // Simply call `qsort` with the appropriate parameters.
    qsort(list->items, list->count,
                             sizeof(*list->items), bt_william_hill_compare_events);
}

void
bt_william_hill_event_list_merge(bt_event_list *dst,
                                                      bt_event_list *src)
{
    int count;
    // Check of there are no events to merge, and return in that case
    if (src->count == 0)
        return;
    // Store current event count
    count = src->count;
    for (int index = 0; index < count; ++index) {
        bt_event *event;
        size_t size;
        // Make a pointer to current event
        event = src->items[index];
        // Try to find the event in the destination list, if it's
        // found continue to the next item.
        if (bt_william_hill_event_list_find(dst, event->id) != NULL)
            continue;
        // Extract the event from `src` in order to insert it into
        // `dst`. To do this, we first compute the size of the items
        // to the right of the interesting element and move all the
        // elements from the right to the actual position.
        size = (count - index - 1) * sizeof(*src->items);
        if (size != 0)
            memmove(&src->items[index], &src->items[index + 1], size);
        // Append the event to the destination list
        bt_william_hill_event_list_append(dst, event);
        // Sort it in order for binary search to work on subsequent
        // calls
        bt_william_hill_event_list_sort(dst);
        // Update the number of elements and reset the position to
        // reflect the fact that there is one less element at that
        // precise position and continue scanning the list from
        // there. Note that now the next element is at the previous
        // position, and of course count has decreased by 1.
        count -= 1;
        index -= 1;
    }
    // Update the list element count
    src->count = count;
}

bt_event_list *
bt_william_hill_events_list_fetch()
{
    bt_event_list *list;
    char *html;
    const char *url;
    url = "http://sports.williamhill.com/bet/es-es/betlive/24";
    html = bt_http_get(url, bt_william_hill_use_tor(), NULL, NULL);
    if (html == NULL)
        return NULL;
    list = bt_william_hill_extract_events(html);
    bt_free(html);
    return list;
}

size_t
bt_william_hill_event_list_get_count(const bt_event_list *const list)
{
    return list->count;
}

bt_event *
bt_william_hill_event_list_get_item(const bt_event_list *const list,
                                                                   size_t index)
{
    return list->items[index];
}

bt_tennis_category
bt_william_hill_event_get_category(const bt_event *const event)
{
    return event->category;
}

int
bt_william_hill_event_get_current_set(const bt_event *const event)
{
    return event->current_set;
}

void
bt_william_hill_event_set_current_set(bt_event *const event, int set)
{
    event->current_set = set;
}

void
bt_william_hill_event_swap_players(bt_event *const event)
{
    bt_player *player;

    player = event->players[0];
    event->players[0] = event->players[1];
    event->players[1] = player;
}

bt_player *
bt_william_hill_event_get_player(const bt_event *const event,
                                                                   size_t index)
{
    return event->players[index];
}

int
bt_william_hill_event_get_id(const bt_event *const event)
{
    // Allow `NULL' as a value to this function
    // so it can be called in all cases.
    if (event == NULL)
        return -1;
    return event->id;
}

const char *
bt_william_hill_event_get_tour(const bt_event *const event)
{
    return event->tour;
}

void
bt_william_hill_event_list_unsubscribe_all(bt_event_list *list)
{
    // Iterate through all the elements
    for (size_t idx = 0; idx < list->count; ++idx) {
        bt_player *player[2];
        bt_event *event;
        // Make a pointer to the event
        event = list->items[idx];
        if (event == NULL)
            continue;
        // Unsubscribe the event
        event->current_set = -1;
        event->ready4incidents = 2;
        event->subscribed = false;
        // Make a pointer to the first player
        player[0] = event->players[0];
        // Make a pointer to the second player
        player[1] = event->players[1];
        // Reset the MTO count value
        player[0]->c_mto_count = player[0]->t_mto_count;
        player[1]->c_mto_count = player[1]->t_mto_count;

        memset(&player[0]->score, 0, sizeof(player[0]->score));
        memset(&player[1]->score, 0, sizeof(player[0]->score));
    }
}

int
bt_william_hill_event_list_subscribe_all(
                   const bt_websocket_connection *const ws, bt_event_list *list)
{
    size_t count;
    bool invalid;
    // Lock the mutex to access the context variables
    bt_global_lock();
    // Check if the list is valid, store the number of elements
    // of the list in `count` and check whether it's `0`.
    invalid = ((list == NULL) || ((count = list->count) == 0));
    // Unlock the mutex and let other threads work meanwhile
    bt_global_unlock();
    // Check if this is a valid request to subscribe the events
    if (invalid == true)
        return 0;
    // Iterata
    for (size_t i = 0; i < count; ++i) {
        bt_event *event;
        bool subscribed;
        // Lock the mutex to access shared data
        bt_global_lock();
        // Make a pointer to ehe event
        event = list->items[i];
        // Check if it's alredy subscribed
        subscribed = ((event != NULL) && (event->subscribed == true));
        // Unlock the mutex and let other threads advance
        bt_global_unlock();
        // If it's subscribed already, go to the next item
        if (subscribed == true)
            continue;
        // Try to subscribe it and store the success result in `subscribed`
        // in order to update the event in a safe place
        subscribed = bt_william_hill_topics_subscribe_event(event, ws);
        // Abort this on failure, because it might imply that `websocket'
        // became a dangling pointer.
        if (subscribed == false)
            return  -1;
        // Lock the mutex to alter the shared data
        bt_global_lock();
        // Update the subscription status
        if (event != NULL)
            event->subscribed = subscribed;
        // Unlock the mutex so other threads can continue using
        // the context.
        bt_global_unlock();
    }
    return 0;
}

bool
bt_william_hill_event_is_ready_for_incidents(bt_event *const event)
{
    if (event->ready4incidents == 0)
        return false;
    return (--event->ready4incidents == 0);
}

const char *
bt_william_hill_event_get_date(const bt_event * const event)
{
    return event->date;
}

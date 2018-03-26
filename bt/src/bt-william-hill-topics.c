#include <stdio.h>
#include <stdbool.h>
#include <string.h>

#include <bt-william-hill-topics.h>
#include <bt-william-hill-events.h>
#include <bt-private.h>
#include <bt-memory.h>
#include <bt-context.h>
#include <bt-debug.h>

typedef int (*comparison_fn)(const void *const, const void *const);
typedef struct bt_topic_descriptor {
    const char *name;
    enum bt_topic_type type;
    int enabled;
} bt_topic_descriptor;

// List of all watchable topics in the william hill websocket
static const bt_topic_descriptor AllTopics[] = {
      {"animation", AnimationTopic, NoCategory}
    , {"competitors/A/teamName", TeamANameTopic, AllCategories}
    , {"competitors/B/teamName", TeamBNameTopic, AllCategories}
    , {"currentGame/pointsWon/A", CurrentGamePointsWonA, NoCategory}
    , {"currentGame/pointsWon/B", CurrentGamePointsWonB, NoCategory}
    , {"currentSet", CurrentSetTopic, AllCategories}
    , {"currentSet/gamesWon/A", CurrentSetGamesWonA, AllCategories}
    , {"currentSet/gamesWon/B", CurrentSetGamesWonB, AllCategories}
    , {"incidents", IncidentsTopic, AllCategories}
    , {"phase", PhaseTopic, NoCategory}
    , {"previousSets/1/gamesWon/A", PreviousSet1GamesWonA, AllCategories}
    , {"previousSets/1/gamesWon/B", PreviousSet1GamesWonB, AllCategories}
    , {"previousSets/1/tieBreakPointsWon/A", PreviousSet1TiebreakPointsWonA, NoCategory}
    , {"previousSets/1/tieBreakPointsWon/B", PreviousSet1TiebreakPointsWonB, NoCategory}
    , {"previousSets/2/gamesWon/A", PreviousSet2GamesWonA, AllCategories}
    , {"previousSets/2/gamesWon/B", PreviousSet2GamesWonB, AllCategories}
    , {"previousSets/2/tieBreakPointsWon/A", PreviousSet2TiebreakPointsWonA, NoCategory}
    , {"previousSets/2/tieBreakPointsWon/B", PreviousSet2TiebreakPointsWonB, NoCategory}
    , {"previousSets/3/gamesWon/A", PreviousSet3GamesWonA, AllCategories}
    , {"previousSets/3/gamesWon/B", PreviousSet3GamesWonB, AllCategories}
    , {"previousSets/3/tieBreakPointsWon/A", PreviousSet3TiebreakPointsWonA, NoCategory}
    , {"previousSets/3/tieBreakPointsWon/B", PreviousSet3TiebreakPointsWonB, NoCategory}
    , {"previousSets/4/gamesWon/A", PreviousSet4GamesWonA, AllCategories}
    , {"previousSets/4/gamesWon/B", PreviousSet4GamesWonB, AllCategories}
    , {"previousSets/4/tieBreakPointsWon/A", PreviousSet4TiebreakPointsWonA, NoCategory}
    , {"previousSets/4/tieBreakPointsWon/B", PreviousSet4TiebreakPointsWonB, NoCategory}
    , {"previousSets/5/gamesWon/A", PreviousSet5GamesWonA, AllCategories}
    , {"previousSets/5/gamesWon/B", PreviousSet5GamesWonB, AllCategories}
    , {"previousSets/5/tieBreakPointsWon/A", PreviousSet5TiebreakPointsWonA, NoCategory}
    , {"previousSets/5/tieBreakPointsWon/B", PreviousSet5TiebreakPointsWonB, NoCategory}
    , {"teamServing", TeamServingTopic, NoCategory}
    , {"totalSets", TotalSetsTopic, NoCategory}
};

typedef struct bt_topic {
    char *alias;
    bt_event *event;
    enum bt_topic_type type;
} bt_topic;

typedef struct bt_topic_list {
    bt_topic **items;
    size_t size;
    size_t count;
} bt_topic_list;

static int
bt_william_hill_compare_topics_descriptions_by_name(const void *const _A, const void *const _B)
{
    const bt_topic_descriptor *A_;
    const bt_topic_descriptor *B_;
    A_ = _A;
    B_ = _B;
    return strcmp(A_->name, B_->name);
}

static int
bt_william_hill_topics_compare_by_alias(const void *const _a, const void *const _b)
{
    const bt_topic *a;
    const bt_topic *b;
    a = *((const bt_topic **) _a);
    b = *((const bt_topic **) _b);
    if ((a == NULL) || (b == NULL))
        return (a == NULL) ? -1 : 1;
    if ((a->alias == NULL) || (b->alias == NULL))
        return (a->alias == NULL) ? -1 : 1;
    return strcmp(a->alias, b->alias);
}

bt_topic *
bt_william_hill_topic_new(char *alias, bt_event *event, enum bt_topic_type type)
{
    bt_topic *topic;
    if (event == NULL)
        return NULL;
    // Allocate space for the new topic
    topic = bt_malloc(sizeof(*topic));
    if (topic ==  NULL)
        return NULL;
    // Fill the structure with initial values
    topic->alias = alias;
    topic->event = event;
    topic->type = type;
    // Return the new topic object
    return topic;
}

void
bt_william_hill_topic_free(bt_topic *const topic)
{
    if (topic == NULL)
        return;
    // Free the alias, which is allocated dynamically
    bt_free(topic->alias);
    // Free the topic object
    bt_free(topic);
}

void
bt_william_hill_topic_list_free(bt_topic_list *const list)
{
    // Behave correctly
    if (list == NULL)
        return;
    // Iterate through each item in the list and free it
    for (size_t index = 0; index < list->count; ++index)
        bt_william_hill_topic_free(list->items[index]);
    // Free the items container
    bt_free(list->items);
    // Free the list
    bt_free(list);
}

enum bt_topic_type
bt_william_hill_topic_get_type_from_description_name(const char *const name)
{
    const bt_topic_descriptor *found;
    bt_topic_descriptor needle;
    // Generate the key object to search for the `topic` named `name`
    needle.name = name;
    // Do a binary search
    found = bsearch(&needle, AllTopics, sizeof(AllTopics) / sizeof(*AllTopics),
           sizeof(needle), bt_william_hill_compare_topics_descriptions_by_name);
    // Check we found one before dereferencing
    if (found != NULL)
        return found->type;
    // Return this as a default value indicating failure
    return InvalidTopic;
}

const bt_topic *
bt_william_hill_topic_list_find(const bt_topic_list *const list,
                                                        const char *const alias)
{
    bt_topic **found;
    bt_topic pattern;
    bt_topic *pointer;
    // Make a pointer to the "key" object
    pointer = &pattern;
    // Fill the "key" object with the interesting data
    pattern.alias = (char *) alias;
    // Perform a binary search
    found = bsearch(&pointer, list->items, list->count,
                      sizeof(pointer), bt_william_hill_topics_compare_by_alias);
    // Check this to avoid dereferencing `NULL`
    if (found == NULL)
        return NULL;
    return *found;
}

bt_topic_list *
bt_william_hill_topics_list_create(void)
{
    bt_topic_list *list;
    // Allocate space for the new list
    list = bt_malloc(sizeof(*list));
    if (list == NULL)
        return NULL;
    // Fill the structure with default values
    list->count = 0;
    // Allocate space for list items
    list->items = bt_malloc(100 * sizeof(*list->items));
    if (list->items != NULL) {
        list->size = 100;
    } else {
        list->size = 0;
    }
    // Return the newly allocated list
    return list;
}

static int
bt_william_hill_topic_list_resize(bt_topic_list *list)
{
    bt_topic **items;
    // Check if resize is required
    if (list->count + 1 < list->size)
        return 0;
    // Allocate space for the new size
    items = bt_realloc(list->items, 2 * list->size * sizeof(*items));
    if (items == NULL)
        return -1;
    // Reassign the items pointer
    list->items = items;
    // Update the size member
    list->size *= 2;
    // Return success
    return 0;
}

void
bt_william_hill_topic_list_sort(bt_topic_list *list)
{
    // Simple qsort call wrapper
    qsort(list->items, list->count,
                    sizeof(*list->items), bt_william_hill_topics_compare_by_alias);
}

bool
bt_william_hill_topic_list_append(bt_topic_list *list,
                                                   bt_topic *const topic)
{
    const bt_topic *found;
    // Check if this item is already in the list
    found = bt_william_hill_topic_list_find(list, topic->alias);
    if (found != NULL)
        return false;
    // It's not in the list, so try to append it
    if (bt_william_hill_topic_list_resize(list) != 0)
        return false;
    // Set the approprpiate element and increase `count`
    list->items[list->count++] = topic;
    return true;
}

void
bt_william_hill_topic_list_remove(bt_topic_list *const list,
                                         const bt_event *const to_remove)
{
    size_t count;
    int id;
    // Get the id of the interesting event
    id = bt_william_hill_event_get_id(to_remove);
    // Store the list count to update it without affecting the "real" count
    count = list->count;
    // Iterate through every element
    for (int index = 0; index < count; ++index) {
        bt_topic *topic;
        bt_event *event;
        size_t size;
        // Make a pointer to current item
        topic = list->items[index];
        // Make a pointer to the associated event
        event = topic->event;
        // Compare the event id with the pattern
        if (bt_william_hill_event_get_id(event) != id)
            continue;
        log("removing topic: \033[33m%s\033[0m\n", topic->alias);
        // It's one of them, so remove the item.
        //
        // First we compute the size of the items to the
        // "right" of the current one, to be able to move
        // all of them one step to the left using
        // `memmove()`.
        size = (count - index - 1) * sizeof(*list->items);
        // Now we move the items one item to the left. Note that
        // `size` can be `0` here and the behavior is perfectly
        // defined
        memmove(&list->items[index], &list->items[index + 1], size);
        // Decrease count, and reset index to search from the new
        // "current" item and forward
        count -= 1;
        index -= 1;
        // Free the extracted item
        bt_william_hill_topic_free(topic);
    }
    // Update the list count to reflect the changes
    list->count = count;
}

void
bt_william_hill_topic_reset_alias(bt_topic *topic, char *alias)
{
    // Free the old alias
    bt_free(topic->alias);
    // Set the new one, taking charge of the allocated memory for it
    topic->alias = alias;
}

static bool
bt_william_hill_subscribe_single_topic(struct httpio *const ws, const bt_event *const event, const char *const name)
{
    bool result;
    int id;
    char *path;
    id = bt_william_hill_event_get_id(event);
    if (id == -1)
        return false;
    path = bt_strdup_printf("\x16tennis/matches/%d/%s", id, name);
    if (path == NULL)
        return false;
    result = httpio_websocket_send_string(ws, path);
    bt_free(path);

    return result;
}

void
bt_william_hill_topic_subscribe_incidents(struct httpio *const ws, bt_event *const event)
{
    bt_william_hill_subscribe_single_topic(ws, event, "incidents");
}

void
bt_william_hill_topic_subscribe_previous_set(struct httpio *const ws, const bt_event *const event, int set, enum bt_player_idx idx)
{
    char *path;
    const char *names[] = {
        "previousSets/%d/gamesWon/A",
        "previousSets/%d/gamesWon/B"
    };
    path = bt_strdup_printf(names[idx], set);
    if (path == NULL)
        return;
    bt_william_hill_subscribe_single_topic(ws, event, path);
    bt_free(path);
}

void
bt_william_hill_topic_subscribe_current_set_gameswon(struct httpio *const ws, const bt_event *const event, enum bt_player_idx idx)
{
    const char *names[] = {
        "currentSet/gamesWon/A",
        "currentSet/gamesWon/B"
    };
    bt_william_hill_subscribe_single_topic(ws, event, names[idx]);
}

bool
bt_william_hill_topics_subscribe_event(const bt_event *const event,
                                 const bt_websocket_connection *const wsc)
{
    const char *names[] = {
        "competitors/A/teamName",
        "competitors/B/teamName",
        "currentSet"
    };

    for (size_t idx = 0; idx < countof(names); ++idx) {
        struct httpio *ws;
        const char *name;
        // Pointer to the websocket object
        ws = wsc->ws;
        // Name of the topic to subscribe;
        name = names[idx];
        // Try subscribing it
        if (bt_william_hill_subscribe_single_topic(ws, event, name) == true)
            continue;
        // This means, we failed so get out of here to give
        // a chance in the next scan.
        return false;
    }
    return true;
}

// These are simple "accessor" functions
bt_event *
bt_william_hill_topic_get_event(const bt_topic *const topic)
{
    return topic->event;
}

size_t
bt_william_hill_topic_list_get_count(const bt_topic_list *const topics)
{
    return topics->count;
}

enum bt_topic_type
bt_william_hill_topic_get_type(const bt_topic *const topic)
{
    return topic->type;
}

bt_topic *
bt_william_hill_topic_list_get_item(const bt_topic_list *const topics,
                                                                   size_t index)
{
    return topics->items[index];
}

const char *
bt_william_hill_topic_get_alias(const bt_topic *const topic)
{
    return topic->alias;
}

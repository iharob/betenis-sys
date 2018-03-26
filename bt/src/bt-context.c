#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <signal.h>

#include <bt-william-hill-events.h>
#include <bt-william-hill-topics.h>
#include <bt-util.h>
#include <bt-database.h>
#include <bt-context.h>
#include <bt-memory.h>
#include <bt-players.h>
#include <bt-memory.h>
#include <bt-debug.h>
#include <bt-channel-settings.h>
#include <bt-private.h>

#include <execinfo.h>

pthread_mutex_t GLOBAL_MUTEX = PTHREAD_MUTEX_INITIALIZER;

typedef struct bt_context {
    bt_event_list *events;
    bt_topic_list *topics;
    bool running;
} bt_context;


bt_context *
bt_create_context(void)
{
    bt_context *context;
    // Allocate space for the context
    context = bt_malloc(sizeof(*context));
    if (context == NULL)
        return NULL;
    // Fill the structure
    context->events = bt_william_hill_event_list_new();
    context->running = true;
    context->topics = bt_william_hill_topics_list_create();
    // Return the newly allocated context
    return context;
}

void
bt_context_free(bt_context *context)
{
    // Behave correctly
    if (context == NULL)
        return;
    // Release events resources
    bt_william_hill_event_list_free(context->events);
    // Release topics resources
    bt_william_hill_topic_list_free(context->topics);
    // Free the context object
    bt_free(context);
}

void
bt_context_remove_bt_william_hill_events(bt_context *const context)
{
    // Simple wrapper
    bt_william_hill_event_list_clear(context->events);
}

bt_event *
bt_find_bt_william_hill_event(const bt_context *const context, int match)
{
    // Simple wrapper
    return bt_william_hill_event_list_find(context->events, match);
}

void
bt_transfer_new_bt_william_hill_events(bt_context *const context,
                                             bt_event_list *const events)
{
    // Sanity check
    if (context->events == NULL)
        return;
    // Attempt to merge the events
    bt_william_hill_event_list_merge(context->events, events);
}

void
bt_context_stop(bt_context *const context)
{
    bt_global_lock();
    context->running = false;
    bt_global_unlock();
}

bt_event_list *
bt_context_get_events(const bt_context *const context)
{
    return context->events;
}

void
bt_context_remove_bt_william_hill_topic(bt_context *const context,
                                             const bt_topic *const topic)
{
    bt_event *event;
    // Get the event associated to this topic
    event = bt_william_hill_topic_get_event(topic);
    // Remove the topics for this event
    bt_william_hill_topic_list_remove(context->topics, event);
    // Remove the event from the list too
    bt_william_hill_event_list_remove(context->events, event);
}

bt_topic *
bt_find_topic_for_event(const bt_context *const ctx, int match, int type)
{
    bt_topic_list *topics;
    size_t count;
    // Make a pointer to the topics list
    topics = ctx->topics;
    if (topics == NULL)
        return NULL;
    // Get the count of topics in the list
    count = bt_william_hill_topic_list_get_count(topics);
    // Iterate through all the topics
    for (size_t idx = 0; idx < count; ++idx) {
        bt_topic *topic;
        bt_event *event;
        // Make a pointer to the current item
        topic = bt_william_hill_topic_list_get_item(topics, idx);
        if (topic == NULL)
            return NULL;
        // Check if it's the appropriate type
        if (bt_william_hill_topic_get_type(topic) != type)
            continue;
        // Make a pointer to the corresponding event
        event = bt_william_hill_topic_get_event(topic);
        // Check if it's the event we are searching for
        if (bt_william_hill_event_get_id(event) != match)
            continue;
        // It was, so return this object
        return topic;
    }
    // Not found so return `NULL`
    return NULL;
}

void
bt_context_sort_topics(bt_context *const context)
{
    bt_william_hill_topic_list_sort(context->topics);
}

bool
bt_context_append_topic(bt_context *const context,
                                                         bt_topic *topic)
{
    if (topic == NULL)
        return false;
    if (bt_william_hill_topic_list_append(context->topics, topic) == false)
        return false;
    return true;
}

const bt_topic *
bt_william_hill_find_topic(const bt_context *const context,
                                                           const char *const id)
{
    return bt_william_hill_topic_list_find(context->topics, id);
}

void
bt_unsubscribe_events(bt_context *const context)
{
    bt_william_hill_event_list_unsubscribe_all(context->events);
}

bool
bt_isrunning(const bt_context *const context)
{
    bool running;
    // Lock the mutex to make this thread safe
    bt_global_lock();
    // Store the value to unlock the mutex before return
    running = context->running;
    // Unlock the mutex
    bt_global_unlock();
    // Return the new value
    return running;
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

void
bt_global_lock()
{
    pthread_mutex_lock(&GLOBAL_MUTEX);
}

void
bt_global_unlock()
{
    pthread_mutex_unlock(&GLOBAL_MUTEX);
}

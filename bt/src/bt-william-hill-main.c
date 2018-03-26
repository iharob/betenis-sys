#include <http-connection.h>
#include <http-websockets.h>

#include <bt-database.h>
#include <bt-context.h>
#include <bt-channel-settings.h>
#include <bt-william-hill-events.h>
#include <bt-william-hill.h>
#include <bt-private.h>
#include <bt-daemon.h>

#include <pthread.h>
#include <unistd.h>

#include <bt-debug.h>
#include <bt-util.h>

#include <string.h>
#include <stdio.h>

static void bt_william_hill_websocket_reconnect(bt_websocket_connection *wsc);
static int
bt_william_hill_websocket_error_handler(struct httpio *const link,
                                                          int error, void *data)
{
    (void) link;
    log("websocket connection error\n");
    if (error == 0)
        return -1;
    // An error occurred so reconnect
    bt_william_hill_websocket_reconnect(data);
    return 0;
}

static int
bt_william_hill_websocket_onclose(struct httpio *const websocket,
                                                         int reason, void *data)
{
    // TODO: Implement this funcion
    return 0;
}

static int
bt_william_hill_websocket_onerror(struct httpio *const websocket,
                                                          int error, void *data)
{
    // TODO: Implement this funcion
    return 0;
}

static void
bt_william_hill_websocket_reconnect(bt_websocket_connection *wsc)
{
    struct httpio *previous;
    // Grab the pointer to the previous socket
    previous = wsc->ws;
    // Create the WebSocket connection
    wsc->ws = bt_william_hill_websocket_connect();
    while ((wsc->ws == NULL) && (bt_isrunning(wsc->context) == true)) {
        wsc->ws = bt_william_hill_websocket_connect();
        bt_sleep(5);
    }
    // Lock the MUTEX
    bt_global_lock();
    // Remove the events so they can be re-subscribed
    bt_unsubscribe_events(wsc->context);
    // Unlock the MUTEX
    bt_global_unlock();
    // Setup the websocket paramters. This will allow the
    // program to control the connection status and errors
    // that might occur so it can recover.
    httpio_websocket_set_onclose_handler(wsc->ws, bt_william_hill_websocket_onclose, NULL);
    httpio_websocket_set_onerror_handler(wsc->ws, bt_william_hill_websocket_onerror, NULL);
    httpio_set_error_handler(wsc->ws, bt_william_hill_websocket_error_handler, wsc);
    // It's safe to do this because it has been replaced.
    httpio_disconnect(previous);
}

void *
bt_william_hill_events_listener(void *context)
{
    bt_websocket_connection wsc;
    double timeout;
    // This is a good timeout, the websocket sends the current
    // Unix Time every 15 seconds, if nothing is received within
    // 20 seconds, it means we probably lost the websocket so
    // a reconnection is required.
    timeout = 30.0E9;
    // Grab a pointer to the context variable
    wsc.context = context;
    // Avoid undefined behavior
    wsc.ws = NULL;
    // Make the connection
    bt_william_hill_websocket_reconnect(&wsc);
    // Initialize the database connection for this thread
    bt_database_initialize();
    // Start the main loop for the event listener
    while (bt_isrunning(context) == true) {
        // Subscribe all the events currently in the queue
        if (bt_william_hill_subscribe_events(&wsc, context) == -1) {
            bt_william_hill_websocket_reconnect(&wsc);
        } else if (httpio_has_data(wsc.ws, timeout) == true) {
            // Read the data from the server
            if (bt_william_hill_handle_websocket_frame(wsc.ws, context) == -1)
            // On error reconnect
                bt_william_hill_websocket_reconnect(&wsc);
        } else {
            log("WebSocket timed out, reconnecting!!!\n");
            // This means that there was no data in 20 seconds
            // so as mentioned before, reconnection is needed
            bt_william_hill_websocket_reconnect(&wsc);
        }
    }
    // Release channel settings
    bt_channel_settings_finalize();
    // Close the connection, if this is reached someone
    // has asked the whole program to stop
    httpio_disconnect(wsc.ws);
    // Release database resources for this thread
    bt_database_finalize();
    bt_notify_thread_end();
    return NULL;
}

void *
bt_william_hill_events_provider(void *data)
{
    bt_context *context;
    // Initialize database connection for this thread
    bt_database_initialize();
    // Make a pointer with the valid type to the running context
    context = data;
    // Start the main loop of this thread
    while (bt_isrunning(context) == true) {
        bt_event_list *list;
        // List available events at the william hill website
        list = bt_william_hill_events_list_fetch();
        if (list != NULL) {
            // Lock the context mutex so it's safe to access the data
            bt_global_lock();
            // Move the events from this thread the other,
            // feed the listener
            bt_transfer_new_bt_william_hill_events(context, list);
            // Free the list
            bt_william_hill_event_list_free(list);
            // Release the mutex so the listener can continue
            bt_global_unlock();
        }
        // Wait one minute to check the website again
        bt_sleep(60);
    }
    // Release resources used by the database connection
    bt_database_finalize();
    bt_notify_thread_end();
    return NULL;
}


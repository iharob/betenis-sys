#include <pthread.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include <http-connection.h>
#include <http-protocol.h>

#include <bt-players.h>
#include <bt-util.h>
#include <bt-william-hill-events.h>
#include <bt-william-hill.h>
#include <bt-debug.h>
#include <bt-string-builder.h>
#include <bt-mbet-feed.h>
#include <bt-telegram-channel.h>
#include <bt-database.h>
#include <bt-memory.h>

#ifdef WITH_CURL
#include <curl/curl.h>
#endif

#define SEND_MSG_URL "https://api.telegram.org/bot187211327:AAEidqiYii2rq_53EJLrhyRWTfxuNKZiDLE/sendMessage?chat_id=%s&parse_mode=HTML&disable_web_page_preview=true&text=%s"
#define EDIT_MSG_URL "https://api.telegram.org/bot187211327:AAEidqiYii2rq_53EJLrhyRWTfxuNKZiDLE/editMessageText?chat_id=%s&message_id=%d&parse_mode=HTML&disable_web_page_preview=true&text=%s"

static bool
bt_telegram_check_status_ok(json_object *object)
{
    const char *truth;
    json_object *value;
    // Extract the `ok' json string object
    if (json_object_object_get_ex(object, "ok", &value) == 0)
        return false;
    // Store the string somewhere to check it
    truth = json_object_get_string(value);
    if (truth == NULL)
        return false;
    // This is all we need to know
    if (strcmp(truth, "true") != 0)
        return false;
    return true;
}

static int
bt_telegram_message_id(json_object *object)
{
    json_object *result;
    json_object *id;
    // Extract the `result' json object where the
    // message_id is
    if (json_object_object_get_ex(object, "result", &result) == 0)
        return -1;
    // Now get the message id
    if (json_object_object_get_ex(result, "message_id", &id) == 0)
        return -1;
    // Convert to int and return
    return json_object_get_int(id);
}

static char *
bt_urlencode(const char *const raw, size_t length)
{
    char *result;
    // Allocate space for the URL encoded string.
    // Since each character can be represented by
    // it's ascii value in HEX plus the '%' characters
    // the maximum necessary size would be 3 * length
    // of the original string
    //
    // We are not converting everything, but it's
    // more efficient to use some extra memory than
    // trying to compute the needed length.
    result = bt_malloc(3 * length + 1);
    if (result == NULL)
        return NULL;
    // Set the length value to 0, to be able to `null'
    // terminate the resulting string
    length = 0;
    for (size_t i = 0; raw[i] != '\0'; ++i) {
        // Check the range to encode it correctly
        // only these characters will be sent as
        // they are
        if ((raw[i] >= 'a') && (raw[i] <= 'z')) {
            result[length++] = raw[i];
        } else if ((raw[i] >= 'A') && (raw[i] <= 'Z')) {
            result[length++] = raw[i];
        } else if ((raw[i] >= '0') && (raw[i] <= '9')) {
            result[length++] = raw[i];
        } else { // Any thing else is encoded
            const char *hex = "0123456789ABCDEF";
            result[length++] = '%';
            result[length++] = hex[(raw[i] >> 4) & 0x0F];
            result[length++] = hex[raw[i] & 0x0F];
        }
    }
    // Always `null' terminate
    result[length] = '\0';
    // Return the urlencoded string
    return result;
}

static char *
bt_telegram_create_message(const char *const format, va_list args)
{
    va_list copy;
    char *result;
    char *raw;
    ssize_t length;
    // Copy the arguments because we need
    // them twice
    va_copy(copy, args);
    // Get the length of the string if it were
    // printed by `vsnprinf'
    length = vsnprintf(NULL, 0, format, copy);
    // Cleanup
    va_end(copy);
    // Allocate space
    raw = bt_malloc(length + 1);
    if (raw == NULL)
        return NULL;
    // Perform the printing
    vsnprintf(raw, length + 1, format, args);
    // Url encode the message so telegram
    // understands
    result = bt_urlencode(raw, length);
    // Free the temporary
    bt_free(raw);
    // Return the acceptable message string
    return result;
}

static int
bt_telegram_check_status(const char *const json)
{
    json_object *object;
    int value;
    // Create a json parser
    object = json_tokener_parse((const char *) json);
    if (object == NULL)
        return -1;
    // Check if the result is "ok"
    value = (bt_telegram_check_status_ok(object) == false) ? -1 : 0;
    if (value == -1) {
        // It's not, tell notify the error
        log("telegram bot: %s\n", json_object_to_json_string(object));
    } else {
        // Get the message id
        value = bt_telegram_message_id(object);
    }
    // Release resources
    json_object_put(object);
    // Return the message id or -1 on error
    return value;
}

static int
bt_telegram_vsend_message(int id, const char *const channel,
                                         const char *const format, va_list args)
{
    char *url;
    char *message;
    int result;
    // Create the message from the parameters
    message = bt_telegram_create_message(format, args);
    if (message == NULL)
        return -1;
    // Ensure this is initialized
    result = -1;
    if (id == -1) // Make the url to send or edit
        url = bt_strdup_printf(SEND_MSG_URL, channel, message);
    else
        url = bt_strdup_printf(EDIT_MSG_URL, channel, id, message);
    // Check if the url was correctly created
    if (url != NULL) {
        char *json;
        // Perform the HTTP GET request
        json = bt_http_get(url, false, NULL, NULL);
        // Check if we have a response
        if (json != NULL) // Handle the response
            result = bt_telegram_check_status(json);
        // Release memory
        bt_free(json);
        bt_free(url);
    }
    // Free temporary memory used by the message
    bt_free(message);
    // Return the message id or -1 on error
    return result;
}



int
bt_telegram_send_message(const char *const channel, const char *const format, ...)
{
    va_list args;
    int id;
    va_start(args, format);
    id = bt_telegram_vsend_message(-1, channel, format, args);
    va_end(args);
    return id;
}

int
bt_telegram_edit_message(int id, const char *const channel, const char *const format, ...)
{
    va_list args;
    va_start(args, format);
    id = bt_telegram_vsend_message(id, channel, format, args);
    va_end(args);
    return id;
}

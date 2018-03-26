#include <string.h>
#include <ctype.h>

#include <mysql.h>
#include <json.h>

#include <http-connection.h>
#include <http-protocol.h>

#include <bt-william-hill.h>
#include <bt-string-builder.h>
#include <bt-william-hill-events.h>
#include <bt-memory.h>
#include <bt-debug.h>
#include <bt-util.h>
#include <bt-database.h>
#include <bt-context.h>
#include <bt-telegram-channel.h>
#include <bt-william-hill-topics.h>
#include <bt-players.h>
#include <bt-channel-settings.h>

typedef struct bt_topic_data {
    char *alias;
    char *name;
    int match;
} bt_topic_data;

typedef struct bt_mto {
    bt_player *victim;
    bt_player *oponent;
    const char *tour;
    const char *category;
} bt_mto;

enum Incidents {
    MedicalBreak,
    OtherIncident
};


/*static int
bt_get_player_from_bt_william_hill_sg(const char *const category,
                                                         const char *const name)
{
    int id;
    MYSQL_STMT *stmt;
    char *query;
    // Need to initialize this
    id = -1;
    // Build the query
    query = bt_load_query("player william hill sg", "%category%", category, NULL);
    if (query == NULL)
        return -1;
    // Execute it
    stmt = bt_mysql_easy_query(query, "%s%s%s|%d", name, name, name, &id);
    if (stmt == NULL)
        goto error;
    // Fetch the result, after this `id' would not be -1 ideally
    mysql_stmt_fetch(stmt);
    // Release used resources
    mysql_stmt_free_result(stmt);
    mysql_stmt_close(stmt);
error:
    // Release space used by the query
    bt_free(query);
    return id;
}

static int
bt_get_player_from_bt_william_hill(bt_tennis_category *category,
                                                        const char *const name)
{
    int id;
    switch (*category) {
    case InvalidCategory:
        // Set category to ATP
        *category = CategoryATP;
        // Check if it's an ATP player
        id = bt_get_player_from_bt_william_hill_sg("atp", name);
        // If it was, return the id
        if (id != -1)
            return id;
        *category = CategoryWTA;
        // It wasn't, check WTA's table
        id = bt_get_player_from_bt_william_hill_sg("wta", name);
        if (id == -1)
            *category = InvalidCategory;
        if (strchr(name, '/') != NULL)
            *category <<= CategoryDoublesShift;
        break;
    case CategoryDoublesATP:
    case CategoryATP:
        return bt_get_player_from_bt_william_hill_sg("atp", name);
    case CategoryDoublesWTA:
    case CategoryWTA:
        return bt_get_player_from_bt_william_hill_sg("wta", name);
    default:
        id = -1;
        break;
    }

    if (id == -1) {
        log("ERROR: \033[37mno encuentro a `%s' (W-H)\033[0m\n", name);
    }
    return id;
}

static bt_player *
bt_get_player_from_incident(json_object *incident, const bt_event *const event)
{
    json_object *competitor;
    bt_player *players[2];
    const char *category;
    const char *name;
    json_object *detail;
    int id;
    // Get which category this event belongs to
    category = bt_get_category_name(bt_william_hill_event_get_category(event));
    if (category == NULL)
        return NULL;
    // Initialize the array of pointers
    players[0] = bt_william_hill_event_get_player(event, 0);
    players[1] = bt_william_hill_event_get_player(event, 1);
    // Extract the "detail" part of the Json
    if (json_object_object_get_ex(incident, "detail", &detail) == false)
        return NULL;
    // Extract the "detail.competitor" part of the Json
    if (json_object_object_get_ex(detail, "competitor", &competitor) == false)
        return NULL;
    // Get the name
    name = json_object_get_string(competitor);
    if (name == NULL)
        return NULL;
    // And the player id
    id = bt_get_player_from_bt_william_hill_sg(category, name);
    if (players[0]->id == id)
        return players[0];
    else if (players[1]->id == id)
        return players[1];
    return NULL;
}*/

static enum Incidents
bt_william_hill_incident_get_type(json_object *incident)
{
    json_object *object;
    const char *type;
    // Extract the `type` object from the Json
    if (json_object_object_get_ex(incident, "type", &object) == false)
        return OtherIncident;
    // Obtain the string representation of the type
    type = json_object_get_string(object);
    if (type == NULL)
        return OtherIncident;
    if (json_object_object_get_ex(incident, "detail", &object) == false)
        return OtherIncident;
    if (json_object_object_get_ex(object, "competitor", &object) == false)
        return OtherIncident;
    // Check wheter it's a medical break
    if (strcmp(type, "MedicalBreak") == 0) {
        const char *player;
        // Get player's name
        player = json_object_get_string(object);
        // Log a message
        log("\t\033[32m%s\033[0m/\033[34m%s\033[0m\n", player, type);
        // Return the appropriate enumerator
        return MedicalBreak;
    }
    // No other types are checked for now because the only
    // interesting type is `MedicalBreak`
    return OtherIncident;
}

static bt_player *
bt_william_hill_get_player(bt_event *const event, json_object *incident)
{
    json_object *detail;
    bt_player *player;
    json_object *competitor;
    const char *name;
    if (json_object_object_get_ex(incident, "detail", &detail) == false)
        return NULL;
    if (json_object_object_get_ex(detail, "competitor", &competitor) == false)
        return NULL;
    name = json_object_get_string(competitor);
    if (name == NULL)
        return NULL;
    player = bt_william_hill_event_get_player(event, 0);
    if (strcmp(player->name, name) == 0)
        return player;
    player = bt_william_hill_event_get_player(event, 1);
    if (strcmp(player->name, name) == 0)
        return player;
    return NULL;
}

static int
bt_william_hill_send_mto(const bt_event *const event, const bt_mto *const mto)
{
    const char *message;
    const char *link;
    bt_string_builder *sb[2];
    bt_tennis_category category;
    bt_player *oponent;
    bt_player *victim;
    int event_id;
    int result;
    char *score;
    victim = mto->victim;
    oponent = mto->oponent;
    result = -1;
    // Get the link to www.mbet.com if exists
    link = "\n\nir a <a href=\"http://abs-cdn.org/Click?bid="
           "19943_19536_12401_false&siteid=\">Marathon Bet</a>";
    // Get the event id
    event_id = bt_william_hill_event_get_id(event);
    // Get the category
    category = bt_william_hill_event_get_category(event);
    // Get the MTO count
    victim->t_mto_count = bt_database_count_mto(event_id, victim->name);
    // Creat a string builder
    sb[0] = bt_string_builder_new();
    if (sb[0] == NULL)
        return -1;
    sb[1] = bt_string_builder_new();
    if (sb[1] == NULL)
        goto error;
    // This means it's the first time, so we have no message
    // id to edit the message that we didn't send yet
    if (victim->t_mto_count <= 1) {
        bt_string_builder_printf(sb[0], MEDICAL_TIMEOUT,
                                        victim->name, oponent->name, mto->tour);
        bt_string_builder_printf(sb[1], MEDICAL_TIMEOUT_NO_PLAYER,
                                        victim->name, oponent->name, mto->tour);
    } else {
        bt_string_builder_printf(sb[0], MEDICAL_TIMEOUT_MULTI,
                   victim->name, victim->t_mto_count, oponent->name, mto->tour);
        bt_string_builder_printf(sb[1], MEDICAL_TIMEOUT_MULTI_NO_PLAYER,
                   victim->t_mto_count, victim->name, oponent->name, mto->tour);
    }
    // Get the match score
    score = bt_william_hill_score_to_str(event, victim, oponent);
    // Marathon bet link
    bt_string_builder_printf(sb[0], "%s", link);
    bt_string_builder_printf(sb[1], "%s", link);

    log("%s", sb[0]);
    log("%s", sb[1]);

    for (size_t idx = 0; idx < bt_channel_settings_count(); ++idx) {
        int status;
        char channel[32];
        int id;
        if (bt_channel_settings_mto_show_player(category, idx) == true) {
            message = bt_string_builder_string(sb[0]);
        } else {
            message = bt_string_builder_string(sb[1]);
        }
        if ((message == NULL) || (message[0] == '\0'))
            continue;
        // Get the channel id
        bt_channel_settings_get_id(channel, sizeof(channel), idx);
        // Get the message id (if -1, send a new message else edit)
        id = bt_database_mto_message_id(event_id, channel);
        // Make the default status (-1) i.e. do not send anything
        // -1. Ignore (do not send anything)
        //  0. Send the message
        //  1. Send the message + score
        // **. Others to be added
        status = -1;
        // Check if ATP Doubles is enabled in config
        if (bt_channel_settings_mto_enabled(category, idx) == false)
            continue;
        // Increment status because we have to send this
        status += 1;
        // Get whether to show the score
        if (bt_channel_settings_mto_score_enabled(category, idx) == true) {
            // Increment because we have to show the score
            status += 1;
        }
        if (((score == NULL) || (score[0] == '\0')) && (status == 1))
            status = 0;
        // Check what to do with this
        switch (status) {
        case 0:
            // Send the message
            id = bt_telegram_edit_message(id, channel, "%s", message);
            break;
        case 1:
            // Send the message
            id = bt_telegram_edit_message(id, channel,
                                    "%s\n\n<b>Marcador</b> %s", message, score);
            break;
        default:
            id = -1;
            break;
        }
        // If msgid == -1, we don't need to save anything
        if (id == -1)
            continue;
        // Save the message id
        //
        // Note: By design if the channel has a given ID
        //       then this will always be the same id.
        //
        //       Thus, every message for this channel is coalesced
        //       into a signle message.
        bt_database_save_mto_message_id(id, event_id, channel);
    }
    result = 0;
error:
    bt_string_builder_free(sb[0]);
    bt_string_builder_free(sb[1]);
    bt_free(score);

    return result;
}

static int
bt_william_hill_parse_mto(bt_mto *const mto,
                           const bt_event *const event, bt_player *const victim)
{
    bt_tennis_category category;
    category = bt_william_hill_event_get_category(event);
    mto->tour = bt_william_hill_event_get_tour(event);
    if (mto->tour == NULL)
        return -1;
    // The other player
    mto->victim = victim;
    mto->oponent = bt_william_hill_event_get_player(event, 0);
    if (mto->oponent == mto->victim)
        mto->oponent = bt_william_hill_event_get_player(event, 1);
    // Tournament category
    mto->category = bt_get_category_name(category);
    return 0;
}

static void
bt_william_hill_handle_mto(bt_player *victim, bt_event *const event)
{
    bt_player *oponent;
    struct bt_mto mto;
    if ((event == NULL) || (victim == NULL))
        return;
    if (bt_william_hill_parse_mto(&mto, event, victim) == -1)
        return;
    oponent = mto.oponent;
    // Check if the message is new or it was already sent
    if (victim->c_mto_count == 0) {
        MYSQL_STMT *stmt;
        const char *date;
        int id;
        char *query;
        if (bt_william_hill_send_mto(event, &mto) == -1)
            return;
        // Find the SQL query
        query = bt_load_query("save mto", "%category%", mto.category, NULL);
        if (query == NULL)
            return;
        // Get event id
        id = bt_william_hill_event_get_id(event);
        // Get the date
        date = bt_william_hill_event_get_date(event);
        // Execute it
        stmt = bt_mysql_easy_query(query, "%d%s%s%s%s",
                              &id, victim->name, oponent->name, mto.tour, date);
        // Free the SQL query
        bt_free(query);
        if (stmt == NULL)
            return;
        // Close the statement and release resources
        mysql_stmt_close(stmt);
    } else {
        // This is not a new one, just decrease the count
        victim->c_mto_count -= 1;
    }
}

static void
bt_william_hill_handle_incidents(bt_event *const event,
                                                      const char *const message)
{
    bt_player *player;
    char **jsons;
    // Split at the character 0x02 (STX start of text)
    jsons = bt_string_splitchr(message, 0x02);
    for (int index = 0; jsons[index] != NULL; ++index) {
        json_object *incident;
        enum Incidents type;
        // This is surprising, it means that we have an empty object?
        if (jsons[index][0] == '\0')
            continue;
        // Parse the corresponding Json
        incident = json_tokener_parse(jsons[index]);
        if (incident == NULL)
            continue;
        // Obtain the type
        type = bt_william_hill_incident_get_type(incident);
        switch (type) {
        case MedicalBreak: // It's a medical break, save and notify
            player = bt_william_hill_get_player(event, incident);
            bt_william_hill_handle_mto(player, event);
            break;
        default:
            break;
        }
        // Clean up the temporary object
        json_object_put(incident);
    }
    // Free the list of strings
    bt_string_list_free(jsons);
}

static void
bt_william_hill_update_set_score(bt_event *const event,
                    enum bt_player_idx idx, int setidx, const char *const value)
{
    bt_player *player;
    bt_score *score;
    bt_score_set *set;
    bt_mto mto;

    player = bt_william_hill_event_get_player(event, idx);
    if (player == NULL)
        return;
    score = &player->score;
    set = &score->sets[setidx];
    set->games = atoi(value);

    if ((player->t_mto_count > 0) &&
                         (bt_william_hill_parse_mto(&mto, event, player) == 0)) {
        bt_william_hill_send_mto(event, &mto);
    }
}

char *
bt_william_hill_score_to_str(const bt_event *const event,
                              bt_player *const victim, bt_player *const oponent)
{
    int current_set;
    char *string;
    current_set = bt_william_hill_event_get_current_set(event);
    if (current_set == -1)
        return bt_strdup("");
    string = bt_malloc(4 * current_set);
    for (size_t setidx = 0; setidx < current_set; ++setidx) {
        bt_score_set *set;
        bt_score *score;

        score = &victim->score;
        set = &score->sets[setidx];
        string[4 * setidx] = set->games + '0';
        string[4 * setidx + 1] = '-';

        score = &oponent->score;
        set = &score->sets[setidx];
        string[4 * setidx + 2] = set->games + '0';
        string[4 * setidx + 3] = ' ';
    }
    string[4 * current_set - 1] = '\0';
    return string;
}

static int
bt_william_hill_previous_set_from_type(enum bt_topic_type type)
{
    switch (type) {
    case PreviousSet1GamesWonA:
    case PreviousSet1GamesWonB:
        return 0;
    case PreviousSet2GamesWonA:
    case PreviousSet2GamesWonB:
        return 1;
    case PreviousSet3GamesWonA:
    case PreviousSet3GamesWonB:
        return 2;
    case PreviousSet4GamesWonA:
    case PreviousSet4GamesWonB:
        return 3;
    case PreviousSet5GamesWonA:
    case PreviousSet5GamesWonB:
        return 4;
    default:
        return -1;
    }
    return -1;
}

static void
bt_william_hill_dispatch_message_final(struct httpio *const ws, const bt_topic *const topic, char **parts)
{
    enum bt_topic_type type;
    enum bt_player_idx pidx;
    int currset;
    int prevset;
    bt_player *player;
    bt_event *evt;
    int event_id;
    // Get the event associated with this topic
    evt = bt_william_hill_topic_get_event(topic);
    if (evt == NULL)
        return;
    prevset = bt_william_hill_event_get_current_set(evt) - 1;
    type = bt_william_hill_topic_get_type(topic);
    event_id = bt_william_hill_event_get_id(evt);
    pidx = Home;
    // Check what type of topic in order to decide what to do
    switch (type) {
    case IncidentsTopic: // It's an incident, check whether we are interested
                         // in it or not
        log("incident: \033[34m%d\033[0m\n", event_id);
        bt_william_hill_handle_incidents(evt, parts[1]);
        break;
    case TeamANameTopic:
    case TeamBNameTopic:
        player = bt_william_hill_event_get_player(evt, type - TeamANameTopic);
        if ((player != NULL) && (strcmp(player->name, parts[1]) == 0))
            return;
        bt_william_hill_event_swap_players(evt);
        break;
    case TotalSetsTopic:
        break;
    case CurrentSetTopic:
        // If this is the first time CurrentSetTopic sends the value
        // then `current_set' will be -1
        prevset = bt_william_hill_event_get_current_set(evt);
        // Convert the value to int
        currset = atoi(parts[1]);
        // Update the `current_set' value in the event
        bt_william_hill_event_set_current_set(evt, currset);
        // If this is not the first set, we're done here.
        // When all the previous sets arrive then we will
        // subscribe the current set games_win
        if (currset > 1) {
            // Subscribe previous set to get the complete score
            bt_william_hill_topic_subscribe_previous_set(ws, evt, currset, Home);
            bt_william_hill_topic_subscribe_previous_set(ws, evt, currset, Away);
        } else {
            // If this is not the first time we should not subscribe
            // other events now
            if (prevset != -1)
                return;
            // Subscribe the games won in current set topic
            bt_william_hill_topic_subscribe_current_set_gameswon(ws, evt, Home);
            bt_william_hill_topic_subscribe_current_set_gameswon(ws, evt, Away);
        }
        break;
    case TeamServingTopic:
        break;
    case AnimationTopic:
        break;
    case PreviousSet1GamesWonB:
    case PreviousSet2GamesWonB:
    case PreviousSet3GamesWonB:
    case PreviousSet4GamesWonB:
    case PreviousSet5GamesWonB:
        pidx = Away;
    case PreviousSet1GamesWonA:
    case PreviousSet2GamesWonA:
    case PreviousSet3GamesWonA:
    case PreviousSet4GamesWonA:
    case PreviousSet5GamesWonA:
        // Determine which set to update
        currset = bt_william_hill_previous_set_from_type(type);
        // Get current set value
        bt_william_hill_update_set_score(evt, pidx, currset, parts[1]);
        if (currset == 0) {
            bt_william_hill_topic_subscribe_current_set_gameswon(ws, evt, pidx);
        } else {
            bt_william_hill_topic_subscribe_previous_set(ws, evt, currset, pidx);
        }
        break;
    case CurrentSetGamesWonB:
        pidx = Away;
    case CurrentSetGamesWonA:
        if (prevset == -1)
            prevset = 0;
        bt_william_hill_update_set_score(evt, pidx, prevset, parts[1]);
        if (bt_william_hill_event_is_ready_for_incidents(evt) == false)
            return;
        bt_william_hill_topic_subscribe_incidents(ws, evt);
        break;
    default:
        break;
    }
    // Indicate that there is nothing to free/release
    return;
}

static void
bt_william_hill_dispatch_message_ecs(struct httpio *const ws, struct httpio_websocket_frame *frame, const bt_topic *const topic)
{
    const char *data;
    char **header;
    // Obtain the frame text
    data = (const char *) httpio_websocket_frame_data(frame);
    if (data == NULL)
        return;
    // Make a list by splitting this string at 0x01 (SOH start of heading)
    header = bt_string_splitchr(&data[1], 0x01);
    if (header == NULL)
        return;
    // Check if the list has the necessary 2 elements, in that case
    // call the final function, that will ulitmately do something
    // with this frame.
    if ((header[0] != NULL) && (header[1] != NULL))
        bt_william_hill_dispatch_message_final(ws, topic, header);
    // Clean up the list and release resources
    bt_string_list_free(header);
    // Return the result with the meaning explained
    // (rules are the same as in `bt_william_hill_dispatch_message_final`)
    return;
}

static char *
bt_william_hill_get_topic_alias_from_message(
                                 const struct httpio_websocket_frame *const frame)
{
    char **parts;
    const char *data;
    char *alias;
    size_t length;

    alias = NULL;
    if (frame == NULL)
        return NULL;
    // Obtain the frame text
    data = (char *) httpio_websocket_frame_data(frame);
    if (data == NULL)
        return NULL;
    // Make a list by splitting this string at 0x01 (SOH start of heading)
    parts = bt_string_splitchr(&data[1], 0x01);
    if (parts == NULL)
        return NULL;
    // Check the list has at least 2 elements. It will have
    // exactly 2 elements if this is valid data. But this test
    // is sufficient rather than necessary.
    if ((parts[0] != NULL) && (parts[1] != NULL)) {
        const char *tail;
        // Find this character to split the object and obtain the id
        tail = strchr(parts[0], '!');
        if (tail != NULL) {
            // It was found, so copy the string to the right of
            // it
            alias = bt_stripdup(tail + 1, &length);
        }
    }
    // As always, release resources
    httpio_string_list_free(parts);
    // Return the `alias' to the caller
    return alias;
}

static void
bt_william_hill_topic_status_changed(struct httpio_websocket_frame *frame,
                                                     bt_context *context)
{
    const bt_topic *topic;
    char *alias;
    char *command;
    char *tail;
    // Obtain the message topic alias
    alias = bt_william_hill_get_topic_alias_from_message(frame);
    if (alias == NULL)
        return;
    // Find the delimiter
    if ((command = strchr(alias, 0x02)) != NULL)
        *(command++) = '\0'; // Make alias `null' terminated (correctly)
    else // An error occurred
        goto error;
    // Find the end of command
    if ((tail = strchr(alias, 0x01)) != NULL)
        *tail = '\0'; // `null' terminate the command string
    // Check if this is the `R' command
    // (currently the only command supported)
    if (strcmp(command, "R") == 0) {
        // Find the corresponding `topic` obejct
        topic = bt_william_hill_find_topic(context, alias);
        if (topic != NULL) {
            // Remove the `topic' from the list
            // this will also remove the associated
            // event (if there are more events)
            // linked to this topic, then there is
            // no problem, because this means that all
            // the topics will eventually be removed
            // so the event is not needed anymore
            bt_context_remove_bt_william_hill_topic(context, topic);
        }
    } else {
        log("command `\033[31m%s\033[0m' for "
                           "`\033[34m%s\033[0m' not handled\n", command, alias);
    }

error:
    // Free temporary storage
    bt_free(alias);
}

static void
bt_william_hill_dispatch_message(struct httpio *const ws, struct httpio_websocket_frame *frame, bt_context *context)
{
    const bt_topic *topic;
    char *alias;
    // Obtain the message topic alias
    alias = bt_william_hill_get_topic_alias_from_message(frame);
    if (alias == NULL)
        return;
    // Find the corresponding `topic` obejct
    topic = bt_william_hill_find_topic(context, alias);
    if (topic != NULL) {
        // If it was found, process the fram with that topic
        // and in case it's returned back we need to remove
        // it from the topic list, and clean it up.
        bt_william_hill_dispatch_message_ecs(ws, frame, topic);
    } else {      
        log("ERROR: did not find topic `\033[33m%s\033[0m\n", alias);
    }
    // Release resources
    bt_free(alias);
}

static char *
bt_william_hill_parse_topic_glue(char **parts, char glue)
{
    bt_string_builder *sb;
    char *string;
    // Create a string builder
    sb = bt_string_builder_new();
    for (size_t index = 0; parts[index] != NULL; ++index) {
        size_t length;
        char *part;
        // Make a pointer to the part
        part = parts[index];
        // Get the length of this particular part
        length = strlen((char *) part);
        // Append it to the string builder
        bt_string_builder_append(sb, part, length);
        // Is it the last one?
        if (parts[index + 1] == NULL)
            continue;
        // It's not so insert a "glue" character
        bt_string_builder_append(sb, &glue, 1);
    }
    // Extract the interesting string
    string = bt_string_builder_take_string(sb);
    // Release resources
    bt_string_builder_free(sb);
    // Return it to the caller
    return string;
}

static bool
bt_william_hill_parse_topic(char *const data, bt_topic_data *topic)
{
    size_t length;
    char *head;
    char *tail;
    char *endptr;
    char **parts;
    // This has to be wrong, there is no apparent need for it
    memset(topic, 0, sizeof(*topic));
    // Make this `NULL`
    parts = NULL;
    // Find the next SOH characeter
    tail = strrchr(data, 0x01);
    if (tail == NULL)
        goto error;
    // Make the bytes bofore the SOH character a string
    *tail = '\0';
    // Find the '!' character in the new "string"
    head = strrchr(data, '!');
    if (head == NULL)
        goto error;
    // This is the alias, copy it
    topic->alias = bt_stripdup(head + 1, &length);
    // Mark this as the new end of the string
    *head = '\0';
    // Now split it as the parts in a path string
    parts = bt_string_splitchr(data, '/');
    if (parts == NULL)
        goto error;
    // Iterate through all the parts
    for (size_t index = 0; parts[index] != NULL; ++index) {
        switch (index) {
        case 0:
        case 1:
            // Ignore the first two
            break;
        case 2:
            // This one contains the `id` of the event
            topic->match = strtol(parts[index], &endptr, 10);
            if (*endptr != '\0')
                goto error;
            break;
        case 3:
            // And this one, the name of the topic
            topic->name = bt_william_hill_parse_topic_glue(&parts[index], '/');
            break;
        }
    }
    // Release resources
    httpio_string_list_free(parts);
    // Inform success
    return true;
error:
    // Release resources
    httpio_string_list_free(parts);
    bt_free(topic->name);
    bt_free(topic->alias);
    // Reset the freed poitners to avoid dangling pointers
    topic->alias = NULL;
    topic->name = NULL;
    // Inform failure
    return false;
}

static void
bt_william_hill_set_topic_alias(const struct httpio_websocket_frame *const frame,
                                                            bt_context *context)
{
    const uint8_t *pointer;
    bt_topic_data data;
    size_t length;
    char *source;
    bt_topic *topic;
    enum bt_topic_type type;
    // Sanity check
    if ((context == NULL) || (frame == NULL))
        return;
    // Make a pointer to the frame text
    pointer = (uint8_t *) httpio_websocket_frame_data(frame);
    if (pointer == NULL)
        return;
    // Copy the data after the first character (which is text)
    source = bt_stripdup((const char *) &pointer[1], &length);
    if (source == NULL)
        return;
    // Parse the string and store the data in `data`
    if (bt_william_hill_parse_topic(source, &data) == false)
        goto error;
    // Check the type of this topic
    type = bt_william_hill_topic_get_type_from_description_name(data.name);
    // Find the corresponding topic in this context, and for this match
    topic = bt_find_topic_for_event(context, data.match, type);
    // If found check it's alias and reset it if necessary
    if (topic != NULL) {
        const char *alias;
        // Obtain the original alias
        alias = bt_william_hill_topic_get_alias(topic);
        // Check whether it changed
        if (strcmp(alias, data.alias) != 0) {
            // Reset it in this case
            bt_william_hill_topic_reset_alias(topic, data.alias);
        } else {
            // We are not interested, so release memory
            bt_free(data.alias);
        }
    } else {
        bt_event *event;
        // Find the corresponding event with id `data.match`
        event = bt_find_bt_william_hill_event(context, data.match);
        // Find the appropriate topic
        topic = bt_william_hill_topic_new(data.alias, event, type);
        // Try to append it to the topics in the context
        if (bt_context_append_topic(context, topic) == false) {
            // On failure, release resources
            bt_william_hill_topic_free(topic);
        }
    }
    // Sort the topics in the context in order to find them with binary search
    bt_context_sort_topics(context);
    // Release resources
    bt_free(data.name);
error:
    bt_free(source);
}

static int
bt_william_hill_handle_message(struct httpio *link,
                  bt_context *const context, struct httpio_websocket_frame *frame)
{
    const uint8_t *data;
    uint8_t type;
    // Get the frame data as text
    data = httpio_websocket_frame_data(frame);
    if (data == NULL)
        return -1;
    // Check the frame type (this is William Hill specific)
    type = data[0];
    if ((type & 0x40) == 0x40)
        return -1;
    // Obtain the "pure" type?
    type &= ~0x40;
    switch (type) {
    case 20: // Control message
        bt_global_lock();
        bt_william_hill_set_topic_alias(frame, context);
        bt_global_unlock();
    case 21: // Message
        bt_global_lock();
        bt_william_hill_dispatch_message(link, frame, context);
        bt_global_unlock();
        break;
    case 24: // On ping?
        log("websocket sent a \033[33mon-ping\033[0m message\n");
        break;
    case 25: // Ping message
        log("websocket \033[31mping\033[0m\n");
        httpio_websocket_send_string(link, (char *) data);
        break;
    case 27: // Server rejected
        break;
    case 28: // Abort
        break;
    case 29: // Connection Lost
        break;
    case 35: // What the hell?
        bt_global_lock();
        bt_william_hill_topic_status_changed(frame, context);
        bt_global_unlock();
        break;
    default:
        log("\033[31mmensaje desconocido\033[0m `%d'\n", type);
        break;
    }

    return 0;
}

bool
bt_william_hill_websocket_handshake(struct httpio *link)
{
    struct httpio_websocket_frame *frame;
    int version;
    int status;
    char *authtoken;
    size_t length;
    char **list;
    size_t index;
    char *endptr;

    list = NULL;
    authtoken = NULL;
    // Get the websocket frame
    frame = httpio_websocket_get_frame(link);
    if (frame == NULL)
        return false;
    // Ensure this has a value in case something goes wrong
    index = -1;
    if (httpio_websocket_frame_type(frame) != 1)
        goto error;
    // Split the string at 0x02 (STX start of text) each
    // item in the least has a meaning as interpreted below
    // in the switch statement
    list = bt_string_splitchr((char *) httpio_websocket_frame_data(frame), 0x02);
    if (list == NULL)
        goto error;
    // Iterate through all the items in the list
    for (index = 0; list[index] != NULL; ++index) {
        switch (index) {
        case 0:
            // Get the protocl version MUST be 4
            version = (int) strtol(list[index], &endptr, 10);
            if (version != 4)
                goto error;
            break;
        case 1:
            // Get the status which has to be 100 or 105
            status = (int) strtol(list[index], &endptr, 10);
            if ((*endptr != '\0') || ((status != 100) && (status != 105)))
                goto error;
            break;
        case 2:
            // Obtain the authorization token (we should probably use this)
            authtoken = bt_stripdup(list[index], &length);
            break;
        }
    }
    // This means that not all the elements where parsed
    if (index < 2)
        goto error;
    // Release frame resources
    httpio_websocket_frame_free(frame);
    // Release list resources
    bt_string_list_free(list);
    // Release the authtoken? Then what is it for?
    bt_free(authtoken);

    return true;

error:
    switch (index) {
    case -1:
        log("error: problema interno.\n");
        break;
    case 0:
        log("error: version del protocolo no soportada.\n");
        break;
    case 1:
        log("error: conexion rechazada.\n");
        break;
    }
    // Release all resources
    httpio_websocket_frame_free(frame);
    bt_string_list_free(list);
    bt_free(authtoken);
    return false;
}

struct httpio *
bt_william_hill_websocket_connect(void)
{
    const char *host;
    struct httpio *link;
    char *secret;
    const httpio_header_list *headers;
    httpio_response *response;
    const char *path;
    const char *key;
    // This is the websocket "link"
    path = "ws://scoreboards.williamhill.es/diffusion?v=4";
    // The host is of course
    host = "scoreboards.williamhill.com";
    response = NULL;
    secret = NULL;
    // Show message
    log("connecting to the `scoreboards.williamhill.com' websocket\n");
    if (bt_william_hill_use_tor() == true) {
        link = httpio_connection_open_socks5(host, "http", httpio_socks5_tor_proxy());
    } else {
        link = httpio_connect(host, "http");
    }
    if (link == NULL)
        return NULL;
    log("link established, doing the handshake\n");
    // Make the secret key
    secret = httpio_websocket_secret();
    if (secret == NULL)
        goto error;
    // Start the actual connection and negotiation
    if (httpio_write_line(link, "GET %s HTTP/1.1", path) == -1)
        goto error;
    if (httpio_write_line(link, "Host: %s", host) == -1)
        goto error;
    if (httpio_write_line(link, "Origin: http://%s", host) == -1)
        goto error;
    if (httpio_write_line(link, "Pragma: no-cache") == -1)
        goto error;
    if (httpio_write_line(link, "Cache-Control: no-cache") == -1)
        goto error;
    if (httpio_write_line(link, "Connection: Upgrade") == -1)
        goto error;
    if (httpio_write_line(link, "Upgrade: websocket") == -1)
        goto error;
    if (httpio_write_line(link, "Sec-WebSocket-Version: 13") == -1)
        goto error;
    if (httpio_write_line(link, "Sec-WebSocket-Key: %s", secret) == -1)
        goto error;
    if (httpio_write_newline(link) == -1)
        goto error;
    // Check the response
    response = httpio_read_response(link);
    if (response == NULL)
        goto error;
    // Get HTTP headers
    headers = httpio_response_get_headers(response);
    // Find the transformed key (accept)
    key = httpio_header_list_get(headers, "sec-websocket-accept");
    // If it's not found, something is wrong
    if (key == NULL)
        goto error;
    // Check it's validity
    if (httpio_websocket_check_key(key, secret) == 0)
        goto error;
    // Make the initial setup with the connected websocket
    if (bt_william_hill_websocket_handshake(link) == false)
        goto error;
    // Release resources
    httpio_response_free(response);
    bt_free(secret);
    log("connected to the websocket\n");
    // Return the connection object for further communication
    return link;
error:
    httpio_response_free(response);
    httpio_disconnect(link);
    bt_free(secret);
    return NULL;
}

int
bt_william_hill_handle_websocket_frame(struct httpio *websocket, bt_context *const context)
{
    struct httpio_websocket_frame *frame;
    // Extract the websocket frame
    frame = httpio_websocket_get_frame(websocket);
    if (frame == NULL)
        return -1;
    // Send it to the system of functions that will validate
    // and react to the frame
    if (bt_william_hill_handle_message(websocket, context, frame) != 0)
        return -1;
    // Release resources
    httpio_websocket_frame_free(frame);
    // Return success
    return 0;
}

int
bt_william_hill_subscribe_events(
    const bt_websocket_connection *const wsc, bt_context *const context)
{
    bt_event_list *list;
    list = NULL;
    // Extract the events list (thread safe)
    bt_global_lock();
    list = bt_context_get_events(context);
    bt_global_unlock();
    // Subscribe all the events here
    return bt_william_hill_event_list_subscribe_all(wsc, list);
}

bool
bt_william_hill_use_tor()
{
    static __thread const char *envvar;
    if (envvar == NULL)
        envvar = getenv("WILLIAM_HILL_SOCKS5_PROXY");
    if (envvar == NULL)
        return false;
    return (strcmp(envvar, "tor") == 0);
}

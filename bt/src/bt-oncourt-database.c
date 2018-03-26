#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>

#include <http-connection.h>
#include <http-protocol.h>

#include <bt-channel-settings.h>

#include <mysql.h>

#include <libxml/parser.h>

#include <bt-timer.h>
#include <bt-util.h>
#include <bt-debug.h>
#include <bt-database.h>
#include <bt-daemon.h>
#include <bt-context.h>
#include <bt-telegram-channel.h>
#include <bt-mbet-feed.h>
#include <bt-oncourt-database.h>
#include <bt-oncourt-dogs.h>
#include <bt-oncourt-retires.h>
#include <bt-memory.h>

#define ONCOURT_USER_ID "jm6429"
#define ONCOURT_DOWNLOAD_ID "W6bH03dM"

static my_bool mytrue = 1;
typedef struct bt_mysql_params {
    size_t size;
    MYSQL_BIND *bind;
    size_t count;
} bt_mysql_params;

enum bt_oncourt_command_type {
    InsertCommand,
    UpdateCommand,
    ComplexCommand
};

typedef struct bt_oncourt_cmd {
    const char *id;
    const char *table;
    const char *fields;
    enum bt_oncourt_command_type type;
    int columns;
} bt_oncourt_cmd;

#define TodayColumns "TOUR,DATE_GAME,ID1,ID2,ROUND,DRAW,RESULT,COMPLETE,LIVE,TIME_GAME,RESERVE_INT,RESERVE_CHAR"
#define INSERT_WF "INSERT INTO %s (%s) VALUES %%values%%"
#define INSERT_WOF "INSERT INTO %s VALUES %%values%%"

static bt_oncourt_cmd Commands[] = {
    {"a", "atp", NULL, ComplexCommand, -1},
    {"iaa", "stat_atp", NULL, InsertCommand, 45}, /* Monitorizar Jugadores Agregados */
    {"iaw", "stat_wta", NULL, InsertCommand, 45},
    {"ida", "today_atp", TodayColumns, InsertCommand, 12},
    {"idw", "today_wta", TodayColumns, InsertCommand, 12},
    {"ifa", "facts_atp", NULL, InsertCommand, 19},
    {"ifw", "facts_wta", NULL, InsertCommand, 19},
    {"iga", "games_atp", NULL, InsertCommand, 6},
    {"igw", "games_wta", NULL, InsertCommand, 6},
    {"ija", "injury_atp", NULL, InsertCommand, 3},
    {"ijw", "injury_wta", NULL, InsertCommand, 3},
    {"ioa", "odds_atp", NULL, InsertCommand, 24},
    {"iow", "odds_wta", NULL, InsertCommand, 24},
    {"ipa", "players_atp", NULL, InsertCommand, 23},
    {"ipw", "players_wta", NULL, InsertCommand, 23},
    {"ira", "ratings_atp", NULL, InsertCommand, 4},
    {"irw", "ratings_wta", NULL, InsertCommand, 4},
    {"isa", "seed_atp", NULL, InsertCommand, 3},
    {"isw", "seed_wta", NULL, InsertCommand, 3},
    {"ita", "tours_atp", NULL, InsertCommand, 22},
    {"itw", "tours_wta", NULL, InsertCommand, 22},
    {"uda", "today_atp", NULL, UpdateCommand, -1},
    {"udw", "today_wta", NULL, UpdateCommand, -1},
    {"uga", "games_atp", NULL, UpdateCommand, -1},
    {"ugw", "games_wta", NULL, UpdateCommand, -1},
    {"upa", "players_atp", NULL, UpdateCommand, -1},
    {"upw", "players_wta", NULL, UpdateCommand, -1},
    {"uta", "tours_atp", NULL, UpdateCommand, -1},
    {"utw", "tours_wta", NULL, UpdateCommand, -1},
    {"w", "wta", NULL, ComplexCommand, -1}
};

static int
bt_oncourt_database_compare_commands(const void *const __A, const void *const __B)
{
    const bt_oncourt_cmd *A_;
    const bt_oncourt_cmd *B_;

    A_ = __A;
    B_ = __B;

    return strcmp(A_->id, B_->id);
}

static int
bt_oncourt_query_replace_date(char **const query)
{
    char *head;
    head = *query;
    while ((head = strchr(head, '#')) != NULL) {
        char *tail;
        // Replace the `#' character with a '
        head[0] = ' ';
        // Point to the next valid character
        head = head + 1;
        // Find the closing `#' if it's not found this
        // string is invalid?
        tail = strchr(head, '#');
        if (tail != NULL) {
            int month;
            int day;
            int year;
            int matches;
            int cnt;
            // `null` terminate the portion of interest
            *tail = '\0';
            // Extract the numbers from this portion, storing the length
            // to avoid UB
            matches = sscanf(head, "%d/%d/%d%n", &month, &day, &year, &cnt);
            if (matches != 3) // If this fails, something is really wrong
                return -1;
            // Store the result to check that it's good
            cnt = snprintf(head, cnt + 1, "%04d-%02d-%02d", year, month, day);
            if (cnt < 0) // It means that the replacement is not a valid date
                return -1;
            // Replace the trainling `#'
            *tail = ' ';
        } else {
            return 0;
        }
    }
    return 0;
}

static char **
bt_oncourt_database_extract_parameters(char **command, const char *line)
{
    char *head;
    size_t length;
    size_t count;
    // Ensure this in case of a problem and an early return
    // the caller can check
    *command = NULL;
    // Get the length of the "query" line
    length = strlen(line);
    // Check the length of the command, to classify it
    if ((length > 3) && (line[3] == ' ')) {
        // It's a 3 character command
        count = 3;
        // Allocate space for the command string
        *command = bt_malloc(count + 1);
        if (*command != NULL) {
            // In case of success copy the string
            memcpy(*command, line, count);
            // And of course `null` terminate it
            (*command)[count] = '\0';
        } else {
            goto error;
        }
        // Check what kind of command is this
        if (**command == 'i') {
            char *tail;
            // It's an INSERT command, so we need to break the
            // input string after every '(' with the ',' delimiter
            if ((head = strchr(line, '(')) == NULL)
                return NULL;
            // If the previous condition wasn't true, this is
            // perfectly safe and we need to do it
            head += 1;
            // If we're here, then we're good for now
            tail = strrchr(head, ')');
            // If this was found, then it's a healthy command. Next
            // we need to add the `null` terminator to help the
            // `bt_string_splitchr` work;
            if (tail != NULL)
                *tail = '\0';
            // Break the string, and return a list of "tokens"
            // interpreted as parameters to a MySQL query
            return bt_string_splitchr(head, ',');
        }
        // This is invalid so return `NULL`
        return NULL;
    } else if ((length > 2) && (line[1] == ',')) {
        // The count is `1`
        count = 1;
        // Allocate space for the command string
        *command = bt_malloc(count + 1);
        if (*command != NULL) {
            // In case of success copy the string
            memcpy(*command, line, count);
            // And of course `null` terminate it
            (*command)[count] = '\0';
        } else {
            goto error;
        }
        // It's a single character command so
        // check for the first ',' which comes
        // right before the parameters list
        if ((head = strchr(line, ',')) == NULL)
            return NULL;        
        // Break the string, and return a list of "tokens"
        // interpreted as parameters to a MySQL query
        return bt_string_splitchr(head + 1, ',');
    }
    // No one was satisfied, so return `NULL`
error:
    return NULL;
}

static bt_oncourt_cmd *
bt_oncourt_database_find_command(const char *const id)
{
    bt_oncourt_cmd *found;
    bt_oncourt_cmd needle;
    // WTF?
    if (id == NULL)
        return NULL;
    // Make a key object
    needle.id = id;
    // Do a binary search
    found = bsearch(&needle, Commands, countof(Commands),
                       sizeof(*Commands), bt_oncourt_database_compare_commands);
    // Return the possible result
    return found;
}

static void
bt_oncourt_database_append_parameter(bt_mysql_params *list,
                                                       const char *const buffer)
{
    size_t size;
    MYSQL_BIND *bind;
    MYSQL_BIND *current;
    size_t count;
    // Store current size
    size = list->size;
    // Check if we need to resize it
    if (size < list->count + 1) {
        // Try to resize it
        bind = bt_realloc(list->bind, (list->size + 0x100) * sizeof(*bind));
        if (bind == NULL)
            return;
        // On success update the size
        list->size += 0x100;
    } else {
        // We didn't need to resize it, point to the valid structure
        bind = list->bind;
    }
    // Store current parameter count
    count = list->count;
    // Make a pointer to current parameter
    current = &bind[count];
    // This is mandatory or MySQL will cause troubles
    memset(current, 0, sizeof(*bind));
    if ((buffer != NULL) && (strcmp(buffer, "NULL"))) {
        // Make valid `MYSQL_BIND` objects, here copy
        // the string since for simplicity we are treating
        // everything as string parameters.
        current->buffer = bt_stripdup(buffer, &current->buffer_length);
        // Set the appropriate type
        current->buffer_type = MYSQL_TYPE_STRING;
    } else {
        // It's a null parameter so simply set `is_null`
        // to true
        current->buffer = NULL;
        current->is_null = &mytrue;
    }
    // Update the `MYSQL_BIND` object list
    list->bind = bind;
    // Increase the count because this parameter
    // made it to the list
    list->count += 1;
}

static bt_mysql_params *
bt_oncourt_database_parse_parameters(char **values)
{
    bt_mysql_params *params;
    // Allocate space for the parameter list
    params = bt_malloc(sizeof(*params));
    if (params == NULL)
        return NULL;
    // This is required by MySQL C API
    memset(params, 0, sizeof(*params));
    // Iterate through the value list and create a
    // MYSQL_BIND object for each parameter
    for (size_t idx = 0; values[idx] != NULL; ++idx) {
        int count;
        // Initialize the `NULL` count value to be sure
        // there is a 0 if nothing is found
        count = 0;
        // Replace the double qoutes. We ignore the return
        // value because it has no important meaning
        bt_strreplace_all(&values[idx], "\"", "");
        // Replace any possible date to meet the ISO format
        // that MySQL can understand
        if (bt_oncourt_query_replace_date(&values[idx]) == -1)
            goto error;
        // Check for any `NULL`s and how many of them are
        // there
        if (strstr(values[idx], "NU") != NULL)
            count = strtol(values[idx], NULL, 10);
        // Add this parameter to the list (it's not `NULL`)
        // if `count` is equal to zero
        if (count == 0)
            bt_oncourt_database_append_parameter(params, values[idx]);
        // This will only enter the loop if `count` is larger
        // than zero, that would indicate one or more `NULL`
        // values, so iterate until we have appended every one
        // to the parameters
        while (count-- > 0)
            bt_oncourt_database_append_parameter(params, NULL);
    }
    // Return the built parameters
    return params;
error:
    // Release resources and return `NULL`
    // it was impossible to understand the
    // strings and determine the parameters
    bt_free(params);
    return NULL;
}

static char *
bt_oncourt_database_nulls(int n)
{
    char *nulls;
    // Get `n` `NULL`s separated by comma
    nulls = bt_malloc(5 * n);
    if (nulls == NULL)
        return NULL;
    // Iterate from `0` to `n`
    for (int i = 0; i < n; ++i) {
        memcpy(nulls + 5 * i, "NULL,", 5);
    }
    // NULL terminate it
    nulls[5 * n - 1] = '\0';
    return nulls;
}

static char *
bt_oncourt_database_insert_query(const char *const table, const char *const fields)
{
    size_t length;
    char *result;
    // Sanity check
    if (table == NULL)
        return NULL;    
    // Sanity check
    if (fields == NULL) {
        // Get the length of the table name
        length = strlen(table);
        // Allocate space to hold the query string
        result = bt_malloc(length + 30);
        if (result == NULL)
            return NULL;
        // Make the query string
        snprintf(result, length + 30, INSERT_WOF, table);
    } else {
        // Get the length of the table name + the fields
        length = strlen(table) + strlen(fields);
        // Allocate space to hold the query string
        result = bt_malloc(length + 33);
        if (result == NULL)
            return NULL;
        // Make the query string
        snprintf(result, length + 33, INSERT_WF, table, fields);
    }
    return result;
}

static void
bt_oncourt_database_replace_nulls(int value, char **parameters)
{
    char string[16];
    ssize_t result;
    char *nulls;
    // WTF?
    if (value == 0)
        return;
    // Replace the `NULL`s in the
    result = snprintf(string, sizeof(string), "%dNU", value);
    // Some error occurred?
    if (result >= (ssize_t) sizeof(string))
        return;
    // Make a string with `NULL`s repeated to feed MySQL
    nulls = bt_oncourt_database_nulls(value);
    if (nulls != NULL) {
        // Replace the parameters
        bt_strreplace_all(parameters, string, nulls);
        // Release resources
        bt_free(nulls);
    }
    // On failure, there is no reason to handle it here
    // because it will be an error later in MySQL
}

static char *
bt_oncourt_database_fix_parameters(const char *data)
{
    char *head;
    char *parameters;
    char *tail;
    // Maje a copy of the parameters string
    parameters = bt_strdup(data);
    if (parameters == NULL)
        return NULL;
    // Replace the date entries
    if (bt_oncourt_query_replace_date(&parameters) == -1)
        goto error;
    // Point to the start of the parameters string
    head = parameters;
    while ((head != NULL) && ((tail = strstr(head, "NU")) != NULL)) {
        int value;
        ptrdiff_t offset;
        int power;
        // Compute the current offset
        offset = tail - parameters;
        // Initialize al the values
        value = 0;
        power = 1;
        tail -= 1;
        // Make an integer value from the digits
        while ((tail > parameters) && (isdigit((unsigned char) tail[0]) != 0)) {
            // Add the digit multiplied by 10
            value += (tail[0] - '0') * power;
            // Update the power
            power *= 10;
            // Move to the previous character
            tail--;
        }
        // Replace `NULL`s in the parameters string
        bt_oncourt_database_replace_nulls(value, &parameters);
        // Update the `head' value to point past the current
        // (NU) expandible expression
        head = parameters + offset + 2;
    }
    // Return the parameter string
    return parameters;
error:
    free(parameters);
    return NULL;
}

static int
bt_oncourt_execute_query(const char *const query, size_t length, MYSQL_BIND *bind)
{
    MYSQL_STMT *stmt;
    int result;
    // Create a MySQL statement
    stmt = bt_database_new_stmt();
    if (stmt == NULL)
        return -1;
    // Prepare the query
    result = mysql_stmt_prepare(stmt, query, length);
    if (result != 0)
        goto error;
    // Check if we do need to bind parameters
    if (bind != NULL)
        result = mysql_stmt_bind_param(stmt, bind);
    // In case binding parameters was executed and failed
    if (result != 0)
        goto error;
    // Execute the query
    result = mysql_stmt_execute(stmt);
    if (mysql_stmt_errno(stmt) == 1062) {
        log("MySQL %s: %s\n", __FUNCTION__, mysql_stmt_error(stmt));
        result = 0;
    }
error:
    // Check for errors and display them in case they happened
    if (result != 0)
        log("MySQL %s: %s\n", __FUNCTION__, mysql_stmt_error(stmt));
    // Release resources
    mysql_stmt_close(stmt);
    return (result == 0) ? 0 : -1;
}

static int
bt_oncourt_execute_query_string(const char *const query, MYSQL_BIND *bind)
{
    return bt_oncourt_execute_query(query, strlen(query), bind);
}

static int
bt_oncourt_database_execute_update_command(const char *const data,
                           const bt_oncourt_cmd *const found)
{
    int result;
    char *parameters;
    char *query;
    // Make the parameters string
    parameters = bt_oncourt_database_fix_parameters(data);
    if (parameters == NULL)
        return -1;
    // Build the query string
    query = bt_strdup_printf("UPDATE %s SET %s", found->table, parameters);
    // Free the parameter list
    bt_free(parameters);
    if (query == NULL)
        return -1;
    // Execute the query
    result = bt_oncourt_execute_query_string(query, NULL);
    // Free the query string
    bt_free(query);
    return result;
}

static int
bt_oncourt_database_handle_insert_command(bt_mysql_params *parameters,
                                     bt_oncourt_cmd *command)
{
    size_t length;
    int result;
    char *places;
    char *query;
    // Ensure this has a value
    result = -1;
    // Make the parameters string
    places = bt_mysql_parameters_sql(command->columns, 1);
    // Make the INSERT query
    query = bt_oncourt_database_insert_query(command->table, command->fields);
    // Replace the '%values%' placeholder with the parameters
    length = bt_strreplace_all(&query, "%values%", places);
    // Free the places string
    bt_free(places);
    // Execute the query
    if (length > 0)
        result = bt_oncourt_execute_query(query, length, parameters->bind);
    // Free the query string
    bt_free(query);
    return result;
}

static int
bt_oncourt_database_complex_command_execute(const char *const source,
                 bt_mysql_params *params, bt_oncourt_cmd *command)
{
    char *query;
    int result;
    /* Build the query */
    query = bt_strdup_printf(source, command->table);
    if (query == NULL)
        return -1;
    /* Execute it */
    result = bt_oncourt_execute_query_string(query, params->bind);
    /* Free the query string */
    bt_free(query);
    return result;
}

static int
bt_oncourt_database_complex_command_delete(bt_mysql_params *params,
                                                 bt_oncourt_cmd *command)
{
    const char *query;
    query = "DELETE FROM games_%s WHERE ID1_G=? AND ID2_G=? AND "
            "ID_T_G=? AND ID_R_G=?";
    return bt_oncourt_database_complex_command_execute(query, params, command);
}

static int
bt_oncourt_database_complex_command_insert(bt_mysql_params *params,
                                                 bt_oncourt_cmd *command)
{
    const char *query;
    query = "INSERT INTO games_%s (ID1_G,ID2_G,ID_T_G,ID_R_G,RESULT_G,DATE_G) "
            "VALUES (?,?,?,?,?,?)";
    return bt_oncourt_database_complex_command_execute(query, params, command);
}

static int
bt_oncourt_database_update_todays_result(bt_mysql_params *parameters,
                                     bt_oncourt_cmd *command)
{
    MYSQL_BIND *parameter;
    MYSQL_BIND bind[9];
    int *map;
    const char *update;
    char *query;
    int flag;
    int result;
    int options[2][9] = {
        // I don't know (copied from the perl script)
        {0,  1, 4, -1, 1, 0, 3,  7,  2},
        {4, -1, 0,  1, 3, 7, 2, -2, -2}
    };
    // Initialize MYSQL_BIND object
    memset(bind, 0, sizeof(bind));
    // Make a pointer the the parameters
    parameter = &parameters->bind[6];
    if (*(char *) parameter->buffer == '1') {
        flag = 5; // I don't know (copied from the perl script)
        map = options[0]; // I don't know (copied from the perl script)
        update = " SET ID1=?, ID2=?, RESULT=?, RESERVE_INT=?"
                     " WHERE ID1=? AND ID2=? AND ROUND=? AND DRAW=? AND TOUR=?";
    } else {
        flag = 4; // I don't know (copied from the perl script)
        map = options[1]; // I don't know (copied from the perl script)
        update = " SET RESULT=?, RESERVE_INT=? WHERE ID1=? AND ID2=? AND"
                                               " ROUND=? AND DRAW=? AND TOUR=?";
    }
    // Build the query
    query = bt_strdup_printf("UPDATE today_%s%s", command->table, update);
    for (size_t index = 0; index < 9; ++index) {
         // I don't know (copied from the perl script)
        if (map[index] == -1) {
            bind[index].buffer = &flag;
            bind[index].buffer_type = MYSQL_TYPE_LONG;
        } else if (map[index] != -2) {
            parameter = &parameters->bind[map[index]];
            bind[index].buffer = parameter->buffer;
            bind[index].buffer_length = parameter->buffer_length;
            bind[index].buffer_type = parameter->buffer_type;
        }
    }
    // Execute this query
    result = bt_oncourt_execute_query_string(query, bind);
    // Free the query string
    bt_free(query);
    return result;
}

static int
bt_oncourt_database_update_todays_players(bt_mysql_params *parameters,
                                     bt_oncourt_cmd *command)
{
    MYSQL_BIND *parameter;
    MYSQL_BIND bind[6];
    int *map;
    int val[4];
    int draw[4];
    char *endptr;
    const char *set;
    char *query;
    int result;
    int options[2][6] = {
        {0, 8, -2, 2, -1, 1},
        {0, 8, -3, 2, -1, 1}
    };

    parameter = &parameters->bind[3];
    val[0] = strtol((char *) parameter->buffer, &endptr, 10);
    if (*endptr != '\0')
        return -1;
    parameter = &parameters->bind[7];
    val[1] = strtol((char *) parameter->buffer, &endptr, 10);
    if (*endptr != '\0')
        return -1;
    parameter = &parameters->bind[8];
    val[2] = strtol((char *) parameter->buffer, &endptr, 10);
    if (*endptr != '\0')
        return -1;
    parameter = &parameters->bind[9];
    val[3] = strtol((char *) parameter->buffer, &endptr, 10);
    if (*endptr != '\0')
        return -1;
    // ALl the values copied from the perl script
    if ((val[0] == 3) || (val[0] > 10) || (val[0] == 8) || (val[2] == 0))
        return 0;
    // Initialize the bind structure
    memset(bind, 0, sizeof(bind));
    // Also copied from the perl script
    draw[0] = 3700 - val[3];
    draw[1] = val[1] / 2;
    draw[2] = draw[1] + 1;
    if ((val[1] % 2) == 0) {
        map = options[0];
        set = "SET ID2=? WHERE ROUND=? AND DRAW=? AND TOUR=? AND (ID2=? OR ID2=?)";
    } else {
        map = options[1];
        set = "SET ID1=? WHERE ROUND=? AND DRAW=? AND TOUR=? AND (ID1=? OR ID1=?)";
    }
    query = bt_strdup_printf("UPDATE today_%s %s", command->table, set);

    for (size_t index = 0; index < 6; ++index) {
        if (map[index] < 0) {
            bind[index].buffer = &draw[-(map[index] + 1)];
            bind[index].buffer_type = MYSQL_TYPE_LONG;
        } else {
            parameter = &parameters->bind[map[index]];

            bind[index].buffer = parameter->buffer;
            bind[index].buffer_length = parameter->buffer_length;
            bind[index].buffer_type = parameter->buffer_type;
        }
    }
    // Execute the query
    result = bt_oncourt_execute_query_string(query, bind);
    // Free the query string
    bt_free(query);
    return result;
}

static int
bt_oncourt_database_handle_complex_command(bt_mysql_params *params,
                                     bt_oncourt_cmd *command)
{
    // In this situation, all these 4 have to be executed
    if (bt_oncourt_database_complex_command_delete(params, command) == -1)
        return -1;
    if (bt_oncourt_database_complex_command_insert(params, command) == -1)
        return -1;
    if (bt_oncourt_database_update_todays_result(params, command) == -1)
        return -1;
    if (bt_oncourt_database_update_todays_players(params, command) == -1)
        return -1;
    return 0;
}

static int
bt_oncourt_database_execute_command(bt_mysql_params *params, struct
                                            bt_oncourt_cmd *command)
{
    int result;
    switch (command->type) {
    case InsertCommand: // Execute an insert command
        result = bt_oncourt_database_handle_insert_command(params, command);
        break;
    case ComplexCommand: // Execute an COMPLEX command
        result = bt_oncourt_database_handle_complex_command(params, command);
        break;
    default:
        log("shouldn't we handle this?");
        result = -1;
        break;
    }
    return result;
}

static int
bt_oncourt_database_handle_command_update(const char *const line,
                                                           const char *const id)
{
    bt_oncourt_cmd *found;
    char *start;
    // Find the next '(' character
    start = strchr(line, '(');
    // Determine the command for thes line
    found = bt_oncourt_database_find_command(id);
    if ((start == NULL) || (found == NULL))
        return -1;
    // We got it, it's a valid command so execute ot
    return bt_oncourt_database_execute_update_command(start + 1, found);
}

static void
bt_oncourt_database_free_parameters(bt_mysql_params *parameters)
{
    // WTF?
    if (parameters == NULL)
        return;
    // Iterate through each MYSQL_BIND object and
    // free it's `buffer' member that was allocated
    // dynamically somewhere
    for (size_t i = 0; i < parameters->count; ++i) {
        MYSQL_BIND *parameter;
        // Make a pointer
        parameter = &parameters->bind[i];
        // Free the buffer
        bt_free(parameter->buffer);
    }
    // Free the container
    bt_free(parameters->bind);
    // Free the parameters
    bt_free(parameters);
}

static int
bt_oncourt_database_handle_command_insert(
                        bt_oncourt_cmd *command, char **list)
{
    bt_mysql_params *parameters;
    int result;
    // Make the parameters
    parameters = bt_oncourt_database_parse_parameters(list);
    if (parameters == NULL)
        return -1;
    // Execute the "command"
    result = bt_oncourt_database_execute_command(parameters, command);
    // Free the parameters object
    bt_oncourt_database_free_parameters(parameters);
    return result;
}

static int
bt_oncourt_database_handle_command_raw(const char *const line)
{
    char *query;
    int result;
    // Ensure this has a  value
    result = -1;
    // Direct command simply generate the SQL query
    query = bt_oncourt_database_fix_parameters(line);
    if (query != NULL) {
        // Execute it
        result = bt_mysql_execute_query(query);
        // Free the query string
        bt_free(query);
    }
    return result;
}

static int
bt_oncourt_database_handle_command(const char *const source)
{
    char *id;
    char *line;
    size_t length;
    int result;
    // Ensure this is initialized
    result = -1;
    // Pass a valid pointer to `free()`
    id = NULL;
    // Extract the line from the input without leading
    // or trailing white spaces
    line = bt_stripdup(source, &length);
    if (line != NULL) {
        bt_oncourt_cmd *cmd;
        char **params;
        // Build the parameters
        params = bt_oncourt_database_extract_parameters(&id, line);
        // Fetch which command is this
        cmd = bt_oncourt_database_find_command(id);
        // Check the type of the command and execute
        if ((cmd != NULL) && (cmd->type != UpdateCommand))
            result = bt_oncourt_database_handle_command_insert(cmd, params);
        else if ((cmd != NULL) && (cmd->type == UpdateCommand))
            result = bt_oncourt_database_handle_command_update(line, id);
        else if (*line != '\0')
            result = bt_oncourt_database_handle_command_raw(line);
        // Free the parameters obejct
        bt_string_list_free(params);
        // Free the line string
        bt_free(line);
        // Free the command id
        bt_free(id);
    }
    return result;
}

int
bt_oncourt_database_parse_update(const char *const data)
{
    char **lines;
    // Break the text in lines
    lines = bt_util_string_splitstr(data, "\n");
    if (lines == NULL)
        return -1;
    // Handle each line
    for (size_t idx = 0; lines[idx] != NULL; ++idx) {
        const char *line;
        // Make a pointer to this line
        line = lines[idx];
        // Check for erros
        if ((*line == '\0') || (bt_oncourt_database_handle_command(line) == 0))
            continue;
        // Free the list
        bt_string_list_free(lines);
        // An error occurred
        return -1;
    }
    // Free the list
    bt_string_list_free(lines);
    return 0;
}

static char *
bt_oncourt_database_get_update(bt_http *http, int id)
{
    char *data;
    char *url;
    // Send the request
    url = bt_strdup_printf("http://www.oncourt.org/" ONCOURT_DOWNLOAD_ID "/up%06d.onc", id);
    if (url == NULL)
        return NULL;
    data = bt_http_get(url, false, http, NULL);
    bt_free(url);
    return data;
}


static int
bt_oncourt_database_remote_version(bt_http *http)
{
    char *endptr;
    char *string;
    int value;
    const char *url;
    url = "http://www.oncourt.org/cgi-bin/getver5.cgi?id=" ONCOURT_USER_ID;
    string = bt_http_get(url, false, http, NULL);
    if (string == NULL)
        return -1;
    value = (int) strtol(string, &endptr, 10);
    if (*endptr != '\0')
        value = -1;
    bt_free(string);
    return value;

}

static void
bt_oncourt_database_auto_update(void)
{
    bt_http *http;
    int local;
    int remote;
    // Connect to www.oncourt.org
    http = bt_http_connect("http://www.oncourt.org", false);
    if (http == NULL)
        return;
    bt_database_begin();

    // Get the remote database version
    remote = bt_oncourt_database_remote_version(http);
    if (remote == -1)
        goto error;
    // Get the local database version
    local = bt_database_oncourt_local_version();
    // Start updating every version
    while (local < remote) {
        char *update;
        // Obtain the update from the oncourt server
        if ((update = bt_oncourt_database_get_update(http, local + 1)) == NULL)
            goto reconnect;
        log("\033[33moncourt\033[0m updated %d/%d\n", local + 1, remote);
        // Update the database
        if (bt_oncourt_database_parse_update(update) == -1) {
            // Free the temporary storage
            bt_free(update);
            goto error;
        }
        // Free the temporary storage
        bt_free(update);
        // Super DOGS
        oncourt_send_new_super_dogs();
        // Increase the local version to update the next
        local += 1;
        // It's ok, get the next update
        continue;
    reconnect:
        // Close this connection first
        bt_http_disconnect(http);
        // Attempt to reconnect
        http = bt_http_connect("http://www.oncourt.org", false);
        if (http != NULL)
            continue;
        goto error;
    }
    // Commit the changes
    bt_database_commit();
    // Close the connection
    bt_http_disconnect(http);
    return;
error:
    // Rollback the changes
    bt_database_rollback();
    // Close the connection
    bt_http_disconnect(http);
}

void *
bt_watch_oncourt_database(void *context)
{
    timer_t timer[2];
    // Initialize MySQL database
    bt_database_initialize();
    // Install timers to run these functions at a given time
    timer[0] = bt_setup_timer("22:00:00", oncourt_send_new_retires);
    timer[1] = bt_setup_timer("19:00:00", oncourt_send_new_normal_dogs);
    // Start the main loop
    while (bt_isrunning(context) == true) {
        // Update the database to the most recent version
        bt_oncourt_database_auto_update();
        // Wait reasonably for the next update
        bt_sleep(1800);
    }
    // Release settnigs structure
    bt_channel_settings_finalize();
    // Release database resources
    bt_database_finalize();
    // Release timers resources
    timer_delete(timer[0]);
    timer_delete(timer[1]);
    bt_notify_thread_end();
    return NULL;
}


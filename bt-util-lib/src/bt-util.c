#include <bt-util.h>
#include <bt-context.h>

#include <http-util.h>

#include <stdarg.h>

#include <signal.h>

#include <string.h>
#include <ctype.h>
#include <stddef.h>
#include <math.h>
#include <stdio.h>

#include <unistd.h>
#include <fcntl.h>

#include <pthread.h>

#include <bt-memory.h>
#include <bt-debug.h>
#include <bt-string-builder.h>
#include <bt-database.h>
#include <bt-debug.h>
#include <bt-http-headers.h>

#include <errno.h>

#ifndef WITH_CURL
#include <http-connection.h>
#include <http-protocol.h>
#else
#include <curl/curl.h>
#endif

typedef struct bt_http {
    struct httpio *link;
    bool own;
} bt_http;

typedef struct bt_http_url {
    char *host;
    char *service;
    char *uri;
} bt_http_url;

typedef struct bt_mysql_operation {
    char *query;
    MYSQL_BIND *bind;
    size_t count;
    size_t group_count;
} bt_mysql_operation;

typedef struct bt_mysql_transaction {
    bt_mysql_operation *operations;
    size_t count;
} bt_mysql_transaction;

#define MYSQL_BIND_INSERT_VALUE(type, MYSQL_TYPE)                         \
void                                                                      \
bt_mysql_bind_insert_ ## type(MYSQL_BIND *bind, size_t idx, type value)   \
{                                                                         \
    MYSQL_BIND *target;                                                   \
                                                                          \
    target = &bind[idx];                                                  \
    target->buffer = bt_malloc(sizeof(value));                            \
    if (target->buffer == NULL)                                           \
        return;                                                           \
    memcpy(target->buffer, &value, sizeof(value));                        \
                                                                          \
    target->buffer_type = MYSQL_TYPE;                                     \
}

MYSQL_BIND_INSERT_VALUE(long, MYSQL_TYPE_LONGLONG)
MYSQL_BIND_INSERT_VALUE(int, MYSQL_TYPE_LONG)
MYSQL_BIND_INSERT_VALUE(double, MYSQL_TYPE_DOUBLE)

typedef struct bt_mysql_query {
    const char *id;
    const char *query;
} bt_mysql_query;

__thread bt_mysql_query bt_queries[] = {
    {"drops", "SELECT * FROM (SELECT openning.iid, tournament, openning.home, openning.away, G5, I5, G6, I6, 100.0 * (G5 / (G5 + G6) - I5 / (I5 + I6)) AS dropped, category, telegram FROM (SELECT tournament, iid, home, away, home_price AS I5, away_price AS I6, created, category, telegram FROM mercado_ganador_partido latest WHERE isnew = FALSE AND created = (SELECT MIN(created) FROM mercado_ganador_partido WHERE latest.iid = iid AND latest.home = home AND latest.away = away) GROUP BY iid, home, away) openning JOIN (SELECT iid, home, away, home_price AS G5, away_price AS G6, created FROM mercado_ganador_partido latest WHERE isnew = TRUE AND created = (SELECT MAX(created) FROM mercado_ganador_partido WHERE latest.iid = iid AND latest.home = home AND latest.away = away) GROUP BY iid, home, away) latest ON latest.iid = openning.iid AND latest.home = openning.home AND latest.away = openning.away) drops WHERE dropped <> 0"},
    {"finished matches", "SELECT NAME_T AS TOUR, ID_T_G AS ID_T, COUNTRY_T AS FLAG, NAME_C AS COURT, P1.ID_P, P1.COUNTRY_P AS PFLAG1, COALESCE((SELECT POS_R FROM ratings_%category% WHERE ID_P_R = ID1_G ORDER BY DATE_R DESC LIMIT 1), 0) AS R1, P1.NAME_P, CASE WHEN ID1_G = ID1_O THEN K1 ELSE K2 END AS K1, P2.ID_P, P2.COUNTRY_P AS PFLAG1, COALESCE((SELECT COALESCE(POS_R, 0) FROM ratings_%category% WHERE ID_P_R = ID2_G ORDER BY DATE_R DESC LIMIT 1), 0) AS R2, P2.NAME_P, CASE WHEN ID2_G = ID1_O THEN K1 ELSE K2 END AS K2, (SELECT RANK_T FROM tours_%category% WHERE ID_T = ID_T_G) AS T_RANK, ID_R_G, RESULT_G FROM games_%category% JOIN players_%category% P1 ON P1.ID_P = ID1_G JOIN players_%category% P2 ON P2.ID_P = ID2_G JOIN tours_%category% ON ID_T = ID_T_G JOIN courts ON ID_C = ID_C_T LEFT JOIN odds_%category% ON ((ID1_O = ID1_G AND ID2_O = ID2_G) OR (ID2_O = ID1_G AND ID1_O = ID2_G)) AND ID_T_O = ID_T_G AND ID_R_O = ID_R_G AND ID_B_O = 1 WHERE P1.NAME_P NOT LIKE '%/%' AND P2.NAME_P NOT LIKE '%/%' AND DATE_G = CURRENT_DATE ORDER BY ID_T_G"},
    {"insert odds", "INSERT INTO odds_%category% VALUES (1, ?, ?, ?, ?, ?, ?, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00)"},
    {"list dogs", "SELECT ID1, ID2, TOUR, ROUND, P1.NAME_P, K1, P2.NAME_P, K2, RESULT, T.NAME_T, T.URL_T FROM today_%category% JOIN odds_%category% ON (ID1_O = ID1 OR ID1_O = ID2) AND ID_T_O = TOUR AND ID_R_O = ROUND JOIN players_%category% P1 ON P1.ID_P = ID1 JOIN players_%category% P2 ON P2.ID_P = ID2 JOIN tours_%category% T ON T.ID_T = ID_T_O WHERE BOT2 = FALSE AND ID_B_O = 1 AND K1 >  ? AND K1 <= ? AND K1 > K2 AND RESULT <> '' AND P1.NAME_P NOT LIKE '%/%' AND P2.NAME_P NOT LIKE '%/%'"},
    {"list dogs doubles", "SELECT ID1, ID2, TOUR, ROUND, P1.NAME_P, K1, P2.NAME_P, K2, RESULT, T.NAME_T, T.URL_T FROM today_%category% JOIN odds_%category% ON ID1_O = ID1 AND ID2_O = ID2 AND ID_T_O = TOUR AND ID_R_O = ROUND JOIN players_%category% P1 ON P1.ID_P = ID1 JOIN players_%category% P2 ON P2.ID_P = ID2 JOIN tours_%category% T ON T.ID_T = ID_T_O WHERE BOT2 = FALSE AND ID_B_O = 1 AND K1 >  ? AND K1 <= ? AND K1 > K2 AND RESULT <> '' AND P1.NAME_P LIKE '%/%' AND P2.NAME_P LIKE '%/%'"},
    {"list retires", "SELECT A.NAME_P, B.NAME_P, T.NAME_T, A.ID_P, B.ID_P, G.TOUR, (SELECT COUNT(*) FROM today_%category% JOIN players_%category% ON ((players_%category%.ID_P = today_%category%.ID1) OR (players_%category%.ID_P = today_%category%.ID2)) WHERE players_%category%.NAME_P LIKE CONCAT('%/', (SELECT NAME_P FROM players_%category% WHERE ID_P = G.ID2), '%') OR players_%category%.NAME_P LIKE CONCAT('%', (SELECT NAME_P FROM players_%category% WHERE ID_P = G.ID2), '/%')) FROM today_%category% G JOIN players_%category% A ON A.ID_P = G.ID2 JOIN players_%category% B ON B.ID_P = G.ID1 JOIN tours_%category% T ON G.TOUR = T.ID_T WHERE G.RESULT LIKE '%ret.' AND BOT1 = FALSE AND A.NAME_P NOT LIKE '%/%' AND B.NAME_P NOT LIKE '%/%'"},
    {"list retires doubles", "SELECT A.NAME_P, B.NAME_P, T.NAME_T, A.ID_P, B.ID_P, G.TOUR, (SELECT COUNT(*) FROM today_%category% JOIN players_%category% ON ((players_%category%.ID_P = today_%category%.ID1) OR (players_%category%.ID_P = today_%category%.ID2)) WHERE players_%category%.NAME_P LIKE CONCAT('%/', (SELECT NAME_P FROM players_%category% WHERE ID_P = G.ID2), '%') OR players_%category%.NAME_P LIKE CONCAT('%', (SELECT NAME_P FROM players_%category% WHERE ID_P = G.ID2), '/%')) FROM today_%category% G JOIN players_%category% A ON A.ID_P = G.ID2 JOIN players_%category% B ON B.ID_P = G.ID1 JOIN tours_%category% T ON G.TOUR = T.ID_T WHERE G.RESULT LIKE '%ret.' AND BOT1 = FALSE AND A.NAME_P LIKE '%/%' AND B.NAME_P LIKE '%/%'"},
    {"mbet player", "SELECT ID_P FROM players_%category% WHERE NAME_P LIKE CONCAT('%', ?, '%', ?, '%') AND NAME_P NOT LIKE '%/%'"},
    {"mbet url", "SELECT CONCAT('\n<a href=\"', mbet, '\">ver en www.mbet.com</a>') FROM partidos WHERE ((J1 = ? AND J2 = ?) OR (J1 = ? AND J2 = ?)) AND torneo = ? AND estado = 1"},
    {"mto count", "SELECT count FROM medical_timeout_%category% WHERE event_id = ? AND player = ?"},
    {"mto tgid", "SELECT id FROM medical_timeout_telegram_ids WHERE event_id = ? AND channel = ?"},
    {"new odds", "SELECT NAME_T, CASE (SELECT COUNT(DISTINCT DRAW) FROM today_%category% AS x WHERE x.TOUR = y.tour AND x.ROUND = y.round) WHEN 64 THEN 'Ronda de 128' WHEN 32 THEN 'Ronda de 64' WHEN 16 THEN 'Ronda de 32' WHEN  8 THEN 'Octavos de final' WHEN 4 THEN 'Cuartos de final' WHEN 2 THEN 'Semi-final' WHEN  1 THEN 'Final' END, P1.NAME_P, O1, P2.NAME_P, O2, url FROM bt_mbet_fresh_odds_%category% AS y JOIN tours_%category% ON ID_T = tour JOIN players_%category% AS P1 ON P1.ID_P = T1 JOIN players_%category% AS P2 ON P2.ID_P = T2 WHERE tour = ? AND round = ? AND fresh = TRUE category = ?"},
    {"odds", "SELECT CASE WHEN ID1_O = ? THEN K1 ELSE K2 END, CASE WHEN ID1_O = ? THEN K2 ELSE K1 END FROM odds_%category% WHERE ((ID1_O = ? AND ID2_O = ?) OR (ID1_O = ? AND ID2_O = ?)) AND ID_B_O = 1 AND ID_T_O = ? AND ID_R_O = ?"},
    {"player data", "SELECT NAME_P, COUNTRY_P, COALESCE((SELECT POS_R FROM ratings_%category% WHERE ID_P_R = ID_P ORDER BY DATE_R DESC LIMIT 1), 0) FROM players_%category% WHERE ID_P = ?"},
    {"player william hill sg", "SELECT ID_P FROM players_%category% WHERE NAME_P = ? OR CONCAT(NAME_P, ' ', SUBSTRING_INDEX(?, ' ', -1)) = ?"},
    /** FIXME: add result column **/
    {"save mto", "INSERT INTO medical_timeout_%category% (`event_id`, `player`, `oponent`, `tour`, `start_time`) VALUES (?,?,?,?,?) ON DUPLICATE KEY UPDATE count = count + 1"},
    {"tour data", "SELECT NAME_T, COUNTRY_T, NAME_C FROM tours_%category% JOIN courts ON ID_C = ID_C_T WHERE ID_T = ?"},
    {"tourid from ids", "SELECT TOUR, ROUND, RANK_T FROM today_%category% JOIN tours_%category% ON ID_T = TOUR WHERE (ID1 = ? AND ID2 = ?) OR (ID1 = ? AND ID2 = ?) ORDER BY ROUND DESC LIMIT 1"},
    {"update dogs", "UPDATE today_%category% SET BOT2 = TRUE WHERE ID1 = ? AND ID2 = ? AND TOUR = ? AND ROUND = ?"},
    {"update mto", "INSERT IGNORE INTO medical_timeout_telegram_ids VALUES (?,?,?)"},
    {"update retires", "UPDATE today_%category% SET BOT1 = TRUE WHERE BOT1 = FALSE AND RESULT LIKE '%ret.%'"},
    {"will play doubles?", " SELECT COUNT(*) FROM today_%category% JOIN players_%category% ON ((players_%category%.ID_P = today_%category%.ID1) OR  (players_%category%.ID_P = today_%category%.ID2)) WHERE players_%category%.NAME_P LIKE CONCAT('%/', (SELECT NAME_P FROM players_%category% WHERE ID_P = ?), '%') OR players_%category%.NAME_P LIKE CONCAT('%', (SELECT NAME_P FROM players_%category% WHERE ID_P = ?), '/%') "},
    {"yesterday matches", "SELECT NAME_T AS TOUR, ID_T_G AS ID_T, COUNTRY_T AS FLAG, NAME_C AS COURT, P1.ID_P, P1.COUNTRY_P AS PFLAG1, COALESCE((SELECT POS_R FROM ratings_%category% WHERE ID_P_R = ID1_G ORDER BY DATE_R DESC LIMIT 1), 0) AS R1, P1.NAME_P, CASE WHEN ID1_G = ID1_O THEN K1 ELSE K2 END AS K1, P2.ID_P, P2.COUNTRY_P AS PFLAG1, COALESCE((SELECT COALESCE(POS_R, 0) FROM ratings_%category% WHERE ID_P_R = ID2_G ORDER BY DATE_R DESC LIMIT 1), 0) AS R2, P2.NAME_P, CASE WHEN ID2_G = ID1_O THEN K1 ELSE K2 END AS K2, (SELECT RANK_T FROM tours_%category% WHERE ID_T = ID_T_G) AS T_RANK, ID_R_G, RESULT_G FROM games_%category% JOIN players_%category% P1 ON P1.ID_P = ID1_G JOIN players_%category% P2 ON P2.ID_P = ID2_G JOIN tours_%category% ON ID_T = ID_T_G JOIN courts ON ID_C = ID_C_T LEFT JOIN odds_%category% ON ((ID1_O = ID1_G AND ID2_O = ID2_G) OR (ID2_O = ID1_G AND ID1_O = ID2_G)) AND ID_T_O = ID_T_G AND ID_R_O = ID_R_G AND ID_B_O = 1 WHERE P1.NAME_P NOT LIKE '%/%' AND P2.NAME_P NOT LIKE '%/%' AND DATE_G = SUBDATE(CURRENT_DATE, 1) ORDER BY ID_T_G"}
};
#define bt_query_size (sizeof(*bt_queries))
#define bt_query_count (sizeof(bt_queries) / bt_query_size)

/**
 * @brief bt_string_list_append Append a string to a list of strings
 * Note: This is a helper function, with very specific requirements.
 * @param list The list to append the string to
 * @param string The string to append to the list
 * @param length The length of string
 * @param count The number of elements currently in the list
 * @return The number of elements in the resulting list or -1 on error
 */
static ssize_t
bt_string_list_append(char ***list, const char *const string,
                                                    size_t length, size_t count)
{
    char **pointer;
    // Re-allocate space for the list
    pointer = bt_realloc(*list, (count + 1) * sizeof(*pointer));
    if (pointer == NULL)
        return -1;
    // If the allocation was, succesful assign the result
    // to the pointee
    *list = pointer;
    // If the string is not NULL' or if it is a string
    // copy it.
    if (string != NULL) {
        // Allocate space in the right place inside the list
        pointer[count] = bt_malloc(length + 1);
        // Of course check if it was possible
        if (pointer[count] == NULL)
            return -1;
        // Copy the contents of the string
        memcpy(pointer[count], string, length);
        // Always, null' terminate it
        pointer[count][length] = '\0';
    } else {
        // Append the NULL' as the caller wishes
        pointer[count] = NULL;
    }
    // Return the updated count. Note, on error
    // this function returns a negative value to
    // inform the caller.
    return count + 1;
}

static int
bt_select(int nfds, fd_set *readfds, fd_set *writefds,
                              fd_set *exceptfds, const struct timespec *timeout)
{
    sigset_t sigset;
    sigemptyset(&sigset);
    return pselect(nfds, readfds, writefds, exceptfds, timeout, &sigset);
}

char **
bt_util_string_splitstr(const char *const string, const char *const delimiter)
{
    size_t count;
    const char *head;
    const char *tail;
    char **list;
    size_t size;

    // Allow NULL' to be passed
    if (string == NULL)
        return NULL;
    // Point to the start of the string
    head = string;
    // Initialize the list to NULL' so realloc()' understands
    list = NULL;
    // Initialize the count to 0' too (this is simply logical)
    count = 0;
    // Make the tail, point to the start to so that *tail != '\0'
    // or, if this fails it's an empty string so nothing to do
    tail = head;
    // Get the length of the delimiter to know how much to
    // advance past a given occurrence
    size = strlen(delimiter);
    // Start to scan the string, until we reach the end
    while (*tail != '\0') {
        ptrdiff_t length;
        ssize_t result;
        // Find the next delimiter
        tail = strstr(head, delimiter);
        // If it's not found just point to the end
        if (tail == NULL)
            tail = strchr(head, '\0');
        // If it's found, get the length of the string
        // from the previous position head' to the
        // delimiter position locate at tail'
        length = (ptrdiff_t) (tail - head);
        // Append the sub-string to the list. The append funcion
        // is smart enough to do what we ask it here, if
        // list is NULL' it will allocate new space and point
        // to it with the list' pointer whose address was
        // passed to it. If it returns a value < 0, it means
        // something went wrong.
        result = bt_string_list_append(&list, head, length, count);
        if (result < 0)
            goto error;
        // Update the count, which is returned by the append function
        count = result;
        // Point to the string after the delimiter
        head = tail + size;
    }
    // Append NULL' as a sentinel, at the end of the list
    bt_string_list_append(&list, NULL, 0, count);
    // And finally return it
    return list;
error:
    // If something did go wrong, essentially with memory
    // allocation, in line 172. Free the previously allocated
    // list.
    for (size_t idx = 0; idx < count; ++idx)
        bt_free(list[idx]);
    bt_free(list);

    return NULL;
}

char **
bt_string_splitchr(const char *const string, char character)
{
    char delimiter[2] = {character, '\0'};
    // Just a shortcut funcion, build a string from
    // the delimiter chr. This function is necessary
    // to split by a non-ascii character easily.
    return bt_util_string_splitstr(string, delimiter);
}

void
bt_string_list_free(char **list)
{
    // As a good mimic of free()' check if the argument is
    // NULL' and simply ignore it if it was.
    if (list == NULL)
        return;
    // Iterate through the elements, since they are copies
    // and free each one
    for (size_t idx = 0; list[idx] != NULL; ++idx)
        bt_free(list[idx]);
    // Free the container too.
    bt_free(list);
}

char *
bt_mysql_parameters_sql(size_t count, size_t group_count)
{
    char *group;
    char *result;
    size_t group_length;
    // There are no groups? WTF?
    if (group_count == 0)
        return NULL;
    // Each parameter group has an opening (?,?,? ... ?)
    // parentheses, count' ',' characters, and count '?'
    // characters, adding the closing parentheses and subtracting
    // the last ',', we get 2 * count + 1 characters. No '\0'
    // terminating byte is used because this is not intended
    // to be used as a string.
    group_length = 2 * count + 1;
    // Allocate space for a single group of parameters
    // inside the string.
    group = bt_malloc(group_length);
    if (group == NULL)
        return NULL;
    // Allocate the whole string. First group_length' * group_count'
    // characters are needed, and also one ',' for each group except
    // the last, that adds to (group_length' + 1) * group_count' - 1.
    // Adding 1 for the terminating '\0', we have a total of
    // (group_length' + 1) * group_count'
    result = bt_malloc((group_length + 1) * group_count);
    if (result != NULL) { // If the allocation fails, return NULL'
        size_t offset;
        // Initialize a single group.
        for (size_t idx = 0; idx < count; ++idx) {
            group[2 * idx + 0] = ',';
            group[2 * idx + 1] = '?';
        }
        // Overwrite the first character, with the opening parentehsis
        group[0] = '(';
        // The last one is too, the closing parenthesis.
        group[group_length - 1] = ')';
        // Start filling the whole string, at the begining
        offset = 0;
        for (size_t idx = 0; idx < group_count; ++idx) {
            // Copy the next group
            memcpy(result + offset, group, group_length);
            // Advance the offset
            offset += group_length;
            // Add the ',' and advance one more character
            result[offset++] = ',';
        }
        // Of course, null' terminate the result
        result[(group_length + 1) * group_count - 1] = '\0';
    }
    // Free the temporary group buffer
    bt_free(group);
    // Finally return the result
    return result;
}

static void
bt_mysql_operation_free(bt_mysql_operation *operations, size_t count)
{
    // Behave as you should
    if (operations == NULL)
        return;
    // Iterate over the operations to free each one
    for (size_t i = 0; i < count; ++i) {
        bt_mysql_operation *operation;
        size_t total;
        // Make a pointer to the element instead of of copying it
        operation = &operations[i];
        // Compute the total number of elements to free
        total = operation->count * operation->group_count;
        for (size_t j = 0; ((operation->bind != NULL) && (j < total)); ++j) {
            MYSQL_BIND *bind;
            // Free all the parameters that were used by mysql_stmt_bind_param
            bind = &operation->bind[j];
            if ((bind == NULL) || (bind->buffer == NULL))
                continue;
            bt_free(bind->buffer);
        }
        // Free the query string
        bt_free(operation->query);
        // Free the pointer to the parameters pointers.
        bt_free(operation->bind);
    }
    // Free the operations object itself.
    bt_free(operations);
}

void
bt_mysql_transaction_free(bt_mysql_transaction *transaction)
{
    // Behave correctly
    if (transaction == NULL)
        return;
    // Free the operations list
    bt_mysql_operation_free(transaction->operations, transaction->count);
    // Free the transaction object itself.
    bt_free(transaction);
}

bt_mysql_transaction *
bt_mysql_transaction_new(size_t count, ...)
{
    bt_mysql_transaction *transaction;
    va_list args;

    // Initialize the variable argument list
    va_start(args, count);
    // Allocate space for the transaction' object
    transaction = bt_malloc(sizeof(*transaction));
    if (transaction != NULL) {
        bt_mysql_operation *operations;
        // Allocate space for the contained operations in this
        // transaction
        operations = bt_malloc(count * sizeof(*operations));
        if (operations != NULL) {
            // Initialize the operations array member, and each of it's
            // elements.
            transaction->operations = operations;
            for (size_t idx = 0; idx < count; ++idx) {
                operations[idx].bind = NULL;
                operations[idx].group_count = 0;
                operations[idx].count = va_arg(args, int);
                operations[idx].query = bt_strdup(va_arg(args, const char *));
            }
            // Set the count of operations
            transaction->count = count;
            // Clean up
            va_end(args);
            // Return the newly allocated object
            return transaction;
        }
        // Free this object, since will return NULL'
        bt_free(transaction);
    }
    // Clean up
    va_end(args);
    return NULL;
}

bt_mysql_operation *
bt_transaction_get_operation(bt_mysql_transaction *transaction, size_t idx)
{
    // WTF?
    if (transaction == NULL)
        return NULL;
    // Check if the request, is not out of bounds
    if (transaction->count > idx)
        return &transaction->operations[idx];
    // It was out of bounds, return NULL'
    return NULL;
}

static size_t
bt_mysql_operation_replace_values(bt_mysql_operation *operation)
{
    char *parameters;
    size_t length;
    // Check for sanity
    if ((operation == NULL) || (operation->query == NULL))
        return 0;
    // Generate the parameters string
    parameters = bt_mysql_parameters_sql(operation->count, operation->group_count);
    if (parameters == NULL)
        return 0;
    // Replace the values in the query with the corresponding
    // placeholder.
    length = bt_strreplace_all(&operation->query, "%values%", parameters);
    // Free the parameters string that was used above
    bt_free(parameters);
    // Return the new length of the object
    return length;
}

static void
bt_mysql_transaction_run_operation(bt_mysql_operation *operation)
{
    int status;
    MYSQL_STMT *stmt;
    size_t length;
    // Store the status of the operation, so we can track it
    status = 0;
    // Check for sanity, avoid SIGSEV or Undefined Behavior in general
    if ((operation == NULL) || (operation->query == NULL) || (operation->bind == NULL))
        return;
    // Create an statement
    stmt = bt_database_new_stmt();
    if (stmt == NULL)
        return;
    // Replace the values with the parameters, and store
    // the resulting query length
    length = bt_mysql_operation_replace_values(operation);
    if (length == 0)
        goto error;
    // Prepare the query
    if ((status = mysql_stmt_prepare(stmt, operation->query, length)) != 0)
        goto error;
    // Bind the parameters
    if ((status = mysql_stmt_bind_param(stmt, operation->bind)) != 0)
        goto error;
    // Finally execute it
    status = mysql_stmt_execute(stmt);
error:
    // On error, check the mysql_error and display it
    bt_debug_mysql(__FILE__, __FUNCTION__, __LINE__, status, stmt);
    // Now we can release resources
    mysql_stmt_close(stmt);
}

void
bt_mysql_transaction_execute(bt_mysql_transaction *transaction)
{
    // Check for INsanity
    if (transaction == NULL)
        return;
    // Iterate through all the operations executing 1 by 1
    for (size_t idx = 0; idx < transaction->count; ++idx)
        bt_mysql_transaction_run_operation(&transaction->operations[idx]);
}

static void
bt_mysql_bind_insert_string(MYSQL_BIND *bind, size_t idx, const char *const string)
{
    MYSQL_BIND *target;
    size_t length;
    if (string == NULL)
        return;
    // Make a poitner to the MYSQL_BIND structure instance
    target = &bind[idx];
    // Get the length of the string, we need it
    length = strlen(string);
    // Allocate space top copy the string
    target->buffer = bt_malloc(length + 1);
    if (target->buffer == NULL)
        return;
    // Copy it
    memcpy(target->buffer, string, length + 1);
    // Setup de MYSQL_BIND structure for mysql_stmt_execute().
    target->buffer_length = length;
    target->buffer_type = MYSQL_TYPE_STRING;
}

void
bt_mysql_operation_put(bt_mysql_operation *op, const char *format, ...)
{
    char chr;
    va_list args;
    size_t offset;
    MYSQL_BIND *bind;
    size_t size;
    bool valid;
    bool islong;
    // Check for sanity
    if ((op == NULL) || (format == NULL))
        return;
    // Compute the total number of objects we will need
    size = (op->count * (op->group_count + 1)) * sizeof(*op->bind);
    // Reallocate the MYSQL_BIND array to add items
    bind = bt_realloc(op->bind, size);
    if (bind == NULL)
        return;
    // Set all the fields in the newly allocated part to 0
    // sonce realloc() doesn't and it's requiered or undefined
    // behavior occurs.
    memset(bind + op->count * op->group_count, 0, op->count * sizeof(*op->bind));
    // Assign our correcly built obejct where it belongs
    op->bind = bind;
    // Initialize the variable argument list
    va_start(args, format);
    // Compute our offset from the begining of the list
    // of MYSQL_BIND objects in the array
    offset = op->count * op->group_count;
    // Initialize these variables so we can use them
    valid = false;
    islong = false;
    // Start iterating through the format string
    while ((chr = *format++) != '\0') {
        switch (chr) {
        case '%': // It's the begining of a new format specifier
            if (valid == true)
                goto wrong_format;
            valid = true;
            continue;
        case 'l': // This is a length modifier
            if (valid == false)
                goto wrong_format;
            islong = true;
            continue;
        default: // What the hell is this?
            if (valid == false)
                goto wrong_format;
        }
        // Reset the valid variable now, we are going to process the
        // parameter so it's safe to do this here because next iteration
        // it will be checked above
        valid = false;
        switch (chr) {
        case 's': // The parameter is a string
            bt_mysql_bind_insert_string(op->bind, offset, va_arg(args, char *));
            break;
        case 'd': // The parameter is an integer (long, or otherwise)
            if (islong == true)
                bt_mysql_bind_insert_long(op->bind, offset, va_arg(args, long int));
            else
                bt_mysql_bind_insert_int(op->bind, offset, va_arg(args, int));
            break;
        case 'f': // The parameter is a float
                  // (or a double, since it's always promoted)
            bt_mysql_bind_insert_double(op->bind, offset, va_arg(args, double));
            break;
        }
        // Reset the islong variable so it's clear next iteration
        islong = false;
        // Update the position in the MYSQL_BIND array
        offset += 1;
    }
    // The group was appended correctly, update the group count
    op->group_count += 1;
    // Clean up
    va_end(args);
    return;
wrong_format:
    // Clean up
    va_end(args);
    // FIXME: perhaps free the stuff here
}

static size_t
bt_countsubstr(const char *string, const char *const needle)
{
    size_t count;
    char *next;
    size_t step;
    // Get the advance ammount
    step = strlen(needle);
    // Try next occurrence of needel, until there are no more
    for (count = 0; (next = strstr(string, needle)) != NULL; ++count) {
        // Advance by step, the number of characters of needle
        string = next + step;
    }
    // Finally return the count.
    return count;
}

size_t
bt_strreplace_all(char **output, const char *const needle, const char *const replacement)
{
    size_t count;
    size_t add;
    size_t take;
    size_t length;
    ptrdiff_t change;
    char *result;
    char *pointer;
    char *string;
    char *next;
    // FIXME: This can probably be made more efficient using memmove() and realloc()

    // Sanity checks
    if ((output == NULL) || (needle == NULL) || (replacement == NULL))
        return 0;
    // Get the current length of the output string
    length = strlen(*output);
    // Get the length of the part to be "removed" from the string
    take = strlen(needle);
    // Count how many times needle occurs in output
    count = bt_countsubstr(*output, needle);
    // Get the number of characters to add
    add = strlen(replacement);
    // Calculate the length of the new string and, allocate space for it
    result = bt_malloc(length + (add - take) * count + 1);
    if (result == NULL)
        return 0;
    // Make a pointer to the begining of result
    pointer = result;
    // Make a poitner to the begining of the input string
    string = *output;
    // Iterate while there are needles in string
    for (count = 0; (next = strstr(string, needle)) != NULL; ++count) {
        // Compute the length of the string up to the needle we found
        change = next - string;
        // Copy all the charecters before this needle
        memcpy(pointer, string, change);
        // Update the pointer to the source string, to copy
        // the next part to the right place
        pointer += change;
        // Copy the replacemente to teh correct place
        memcpy(pointer, replacement, add);
        // Update the poitner to the source string again
        pointer += add;
        // Advance the input string to search for the next needle
        string = next + take;
    }
    // Replace the last occurrence
    change = (*output + length) - string;

    memcpy(pointer, string, change);
    pointer[change] = '\0';
    // Free the original input string
    bt_free(*output);
    // Assign the new string to the original pointer
    *output = result;
    // Return the length of the new string
    return length + (add - take) * count;
}

void
bt_debug_mysql(const char *const file, const char *const function, int line, int status, MYSQL_STMT *stmt)
{
    if ((status == 0) || (stmt == NULL) || (mysql_stmt_errno(stmt) == 0) ||
                                             (mysql_stmt_errno(stmt) == 1062)) {
        return;
    }
    // Display a message if there's a MySQL error
    log("MySQL(%d/%ud:%s:%s:%d) %s\n", status,
          mysql_stmt_errno(stmt), file, function, line, mysql_stmt_error(stmt));
}

char *
bt_stripdup(const char *string, size_t *length)
{
    char *copy;
    // WTF?
    if (string == NULL)
        return NULL;
    // Strip all leading white spaces
    while ((string[0] != '\0') && (isspace((unsigned char) string[0]) != 0))
        string++;
    // Compute the length to target the "end" of the string and strip spaces too
    *length = strlen(string);
    // If it's 0' we stripped the whole input string
    if (*length == 0) // Always return a valid string
        return bt_strdup(string);
    // Strip all the trailing white spaces
    while ((string[*length - 1] != '\0') &&
                          (isspace((unsigned char) string[*length - 1]) != 0)) {
        --(*length);
    }
    // Make a copy with the appropriate length
    copy = bt_malloc(*length + 1);
    if (copy == NULL)
        return NULL;
    // Copy the exact ammount of characters
    memcpy(copy, string, *length);
    // null terminate the new string
    copy[*length] = '\0';
    // New return it
    return copy;
}

void
bt_sleep(double seconds)
{
    struct timespec ts;
    sigset_t set;
    // FIXME: This should abort sleeping when signals happen. It doesn't for now

    // Initialize signal set
    sigemptyset(&set);
    // Initialize the timeout structure
    ts.tv_sec = (long int) floor(seconds);
    ts.tv_nsec = (long int) ceil((seconds - (long int) floor(seconds)) / 1.0E9);
    // Call pselect() now
    bt_select(0, NULL, NULL, NULL, &ts);
}

static char *
bt_strdup_vprintf(const char *const format, va_list args)
{
    char *result;
    ssize_t length;
    va_list copy;
    // Make a copy of the variable argument list, because
    // it will be altered by vsnprintf().
    va_copy(copy, args);
    length = vsnprintf(NULL, 0, format, copy);
    va_end(copy);
    // Check for errors
    if (length == -1)
        return NULL;
    // Allocate the necessary space
    result = bt_malloc(length + 1);
    if (result == NULL)
        return NULL;
    // Now do print the new string in the dynamically
    // allocated space
    vsnprintf(result, length + 1, format, args);
    // Return the new string
    return result;
}

char *
bt_strdup_printf(const char *const format, ...)
{
    char *result;
    va_list args;
    // Initialize variable arguments
    va_start(args, format);
    // Call the real thing
    result = bt_strdup_vprintf(format, args);
    // Clean up
    va_end(args);
    return result;
}

static int
bt_querycmp(const void *const lhs, const void *const rhs)
{
    return strcmp(((const bt_mysql_query *) lhs)->id,
                  ((const bt_mysql_query *) rhs)->id);
}

void
bt_sort_queries(void)
{
    // Ensure queries are sorted to make bt_find_query' work
    qsort(bt_queries, bt_query_count, bt_query_size, bt_querycmp);
}

static const char *
bt_find_query(const char *const id)
{
    bt_mysql_query *found;
    bt_mysql_query key;
    // Simple binary search using the library functions
    key.id = id;
    found = bsearch(&key, bt_queries, bt_query_count, bt_query_size, bt_querycmp);
    if (found == NULL)
        return NULL;
    return found->query;
}

char *
bt_load_query(const char *const name, ...)
{
    va_list args;
    const char *source;
    const char *key;
    char *result;
    // Search for the source SQL statement
    source = bt_find_query(name);
    if (source == NULL) {
        log("WARNING: %s' is an invalid query id\n", name);
        return NULL;
    }
    // Make a copy so bt_strreplace_all can work with it
    result = bt_strdup(source);
    if (result == NULL)
        return NULL;
    // Initialize variable list of arguments
    va_start(args, name);
    while ((key = va_arg(args, const char *)) != NULL) {
        const char *value;
        value = va_arg(args, const char *);
        if (value == NULL)
            goto finished;
        // Replace the value if it's not NULL'.
        bt_strreplace_all(&result, key, value);
    }
finished:
    va_end(args);

    return result;
}

char *
bt_strdup(const char *const string)
{
    size_t length;
    char *result;
    // Compute the string length
    length = strlen(string);
    // Allocate space for it
    result = bt_malloc(length + 1);
    if (result == NULL)
        return NULL;
    // Copy the string without the terminating '\0'
    memcpy(result, string, length);
    // Add the terminating '\0'
    result[length] = '\0';
    // Return the resulting copy
    return result;
}

static char *
bt_http_get_copy_response_HTTP_IO(struct httpio *link)
{
    struct httpio_response *resp;
    struct httpio_body *body;
    char *result;
    // Read the response from the httpio object
    resp = httpio_read_response(link);
    if (resp == NULL)
        return NULL;
    // Check the response code
    // TODO: We could implement redirection here
    if (httpio_response_get_code(resp) != 200)
        goto error;
    // Make the result valid so if the data is never copied
    // it has a sensible value
    result = NULL;
    // Grab the response body
    body = httpio_response_get_body(resp);
    if (body != NULL) { // We take it from the body, so it's no longer there
        // FIXME: This would cause a big problem if the response
        //        is not plain text (html or something else but text).
        //
        //        Ideally, we could infer from the data whether
        //        it is text or not. After all, the null' terminator
        //        is guaranteed by this API.
        result = (char *) httpio_response_body_take_data(body);
    }
    // Free the response object
    httpio_response_free(resp);
    // Return the result (possibly NULL)
    return result;
error:
    httpio_response_free(resp);
    return NULL;
}

static int
bt_http_parse_url(const char *const source, bt_http_url *url)
{
    // First of all, make a copy
    url->service = bt_strdup(source);
    if (url->service == NULL)
        goto error;
    // Now, find the service/host delimiter
    url->host = strstr(url->service, "://");
    // If it's not there, this url is simply INVALID
    if (url->host == NULL)
        goto error;
    // Make service point to a string
    *url->host = '\0';
    // Make this variable point to the (almost) host part
    // of the url
    url->host += 3;
    // Try to find a path part
    url->uri = strchr(url->host, '/');
    if (url->uri != NULL)
        *(url->uri++) = '\0';
    else
        url->uri = strchr(url->host, '\0');
    return 0;
error:
    free(url->service);

    url->host = NULL;
    url->service = NULL;
    url->uri = NULL;

    return -1;
}

static struct httpio *
bt_HTTP_IO_connect(const bt_http_url *const url, bool tor)
{
    struct httpio *link;
    if (tor == true) {// Connect using TOR (standard host/port)
        const struct httpio_proxy *proxy;
        proxy = httpio_socks5_tor_proxy();
        link = httpio_connection_open_socks5(url->host, url->service, proxy);
    } else {// Connect normally without proxy
        link = httpio_connect(url->host, url->service);
    }
    return link;
}

static bt_http *
bt_http_connect_url(bt_http_url *url, bool tor)
{
    bt_http *http;
    // Check whether the caller wants to use TOR
    // TODO: ideally any proxy could be used, but
    //       there is no time to implement other
    //       proxies for now.
    //
    //       This kind of change would modify the
    //       whole API. So it must be made in a
    //       major release.
    http = bt_malloc(sizeof(*http));
    if (http == NULL)
        return NULL;
    http->link = bt_HTTP_IO_connect(url, tor);
    if (http->link == NULL) {
        bt_free(http);
        return NULL;
    }
    return http;
}

static void
bt_http_url_free(bt_http_url *url)
{
    free(url->service);
}

bt_http *
bt_http_connect(const char *const source, bool tor)
{
    bt_http_url url;
    bt_http *http;
    // Parse the URL first
    if (bt_http_parse_url(source, &url) == -1)
        return NULL;
    // Build the connection object
    http = bt_http_connect_url(&url, tor);
    // Free the temporary url object
    bt_http_url_free(&url);
    return http;
}

void
bt_http_disconnect(bt_http *http)
{
    if (http == NULL)
        return;
    httpio_disconnect(http->link);
    bt_free(http);
}

char *
bt_http_get(const char *const uri, bool tor, bt_http *ctx,
                             const bt_http_headers *const custom_headers)
{
    bt_http_url url;
    bt_http_headers *headers;
    char *result;
    // Ensure no garbage is returned
    result = NULL;
    if (bt_http_parse_url(uri, &url) == -1)
        return NULL;
    headers = bt_http_headers_new();
    if (headers == NULL)
        goto error;
    // Merge the custom headers with the defaults
    if (bt_http_headers_merge(headers, custom_headers, false) == -1)
        goto error;
    // If we are here, http is not NULL surely
    if (bt_http_headers_append(headers, "Host", url.host, false) == -1)
        goto error;
    if (bt_http_headers_append(headers, "Accept", "text/xml; charset=utf8", false) == -1)
        goto error;
    if (bt_http_headers_append(headers, "Accept-Encoding", "gzip;q=1,*;q=0", false) == -1)
        goto error;
    if (bt_http_headers_append(headers, "Connection", "keep-alive", false) == -1)
        goto error;
    if (bt_http_headers_append(headers, "User-Agent", BT_USER_AGENT, false) == -1)
        goto error;
    // Check if there is a pre allocated HTTP object
    // and use it. If there isn't one, we allocate
    // one and mark it as own, so we can deallocate
    // it and close the connection (that we created)
    if (ctx == NULL) {
        // Try to create the connection
        ctx = bt_http_connect_url(&url, tor);
        if (ctx == NULL)
            goto error;
        ctx->own = true;
    } else {
        ctx->own = false;
    }
    // FIXME: Search in the passed headers if there is one of
    //        the default headers to override it.
    // Build the GET request and put all the good headers
    if (httpio_write_line(ctx->link, "GET /%s HTTP/1.1", url.uri) == -1)
        goto error;
    if (bt_http_headers_write(headers, ctx->link) == -1)
        goto error;
    if (httpio_write_newline(ctx->link) == -1)
        goto error;
    // Extract the response from the request
    result = bt_http_get_copy_response_HTTP_IO(ctx->link);
    // This is not NULL, we allocated it, let's free it
    if (ctx->own == false)
        goto error;
    httpio_disconnect(ctx->link);
    bt_free(ctx);
error:
    bt_http_headers_free(headers);
    // Free URL object
    bt_http_url_free(&url);
    // Return the (possibly NULL) response
    return result;
}


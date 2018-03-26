#include <stdbool.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include <bt-mysql-easy.h>
#include <bt-util.h>
#include <bt-memory.h>
#include <bt-debug.h>

enum format_specifiers {
    InvalidFormat = 0,
    Integer = 'd',
    String = 's',
    CharArray = 'a',
    Double = 'f'
};

typedef struct format_item {
    enum format_specifiers type;
    ssize_t length;
    bool long_modifier;
} format_item;

#if 0 // _DEBUG
double
getseconds(const struct timespec *const end, const struct timespec *const start)
{
    return ((double) (end->tv_nsec - start->tv_nsec)) / 1000000000.0 + (double) (end->tv_sec - start->tv_sec);
}
#define QUERY_EXECUTION_DEBUG_START    \
    struct timespec start_time;        \
    struct timespec end_time;          \
    clock_gettime(CLOCK_MONOTONIC, &start_time);
#define QUERY_EXECUTION_DEBUG_END                             \
    clock_gettime(CLOCK_MONOTONIC, &end_time);                \
    double elapsed_time = getseconds(&end_time, &start_time); \
    if (elapsed_time > 0.05)                                  \
        fprintf(stderr, "%s\n\texecuted in %f\n", query, elapsed_time);
#else
#define QUERY_EXECUTION_DEBUG_START
#define QUERY_EXECUTION_DEBUG_END
#endif

static const char *
htisql_mysql_easy_query_fetch_format_item(format_item *item, const char *input)
{
    char next;
    // Get the next format character
    next = *(input++);
    if (next != '%')
        return NULL;
    // Initialize the format item
    memset(item, 0, sizeof(*item));
    // Check if this is a digit and in case it was extract the
    // length modifier that is used for the `CharArray`
    // specifier
    if (isdigit((unsigned char) *input) != 0)
        item->length = strtol(input, (char **) &input, 10);
    // Get the next character now, this can be one of two
    //
    // 1. The `l' modifier, that applies to the 'd', and 'f'
    //    specifiers only
    // 2. The actual specifier
    next = *(input++);
    // Check if it's the `l' modifier and in case it was
    // extract the specifier character and move the input
    // pointer forward
    if ((item->long_modifier = (next == 'l')) == true)
        next = *(input++);
    // Store the specifier type of `InvalidFormat'
    switch (next) {
    case Integer:
    case Double:
    case String:
    case CharArray:
        item->type = next;
        break;
    default:
        item->type = InvalidFormat;
    }
    // Return the pointer to the next specifer
    return input;
}

static size_t
htisql_mysql_easy_query_count_format(const char *format)
{
    size_t count;
    // Count format specifiers
    for (count = 0; (format = strchr(format, '%')) != NULL; ++count)
        format += 1;
    // Return the count
    return count;
}

static MYSQL_BIND *
htisql_mysql_easy_make_bind(const char *const format, va_list args)
{
    MYSQL_BIND *bind;
    size_t count;
    format_item item;
    const char *fmt;
    // WTF?
    if (format == NULL)
        return NULL;
    // Get the number of places to allocate
    count = htisql_mysql_easy_query_count_format(format);
    if (count == 0)
        return NULL;
    // Allocate the data
    bind = bt_calloc(count, sizeof(*bind));
    if (bind == NULL)
        return NULL;
    // Start parsing the format string
    fmt = htisql_mysql_easy_query_fetch_format_item(&item, format);
    for (size_t i = 0; ((i < count) && (fmt != NULL)); ++i) {
        switch (item.type) {
        case InvalidFormat:
            break;
        case Integer: // Add an integer
            if (item.long_modifier == false) {
                bind[i].buffer = va_arg(args, int *);
                bind[i].buffer_type = MYSQL_TYPE_LONG;
            } else {
                bind[i].buffer = va_arg(args, long int *);
                bind[i].buffer_type = MYSQL_TYPE_LONGLONG;
            }
            break;
        case String: // Add a "string"
            bind[i].buffer = va_arg(args, char *);
            bind[i].buffer_length = strlen((char *) bind[i].buffer);
            bind[i].buffer_type = MYSQL_TYPE_STRING;
            break;
        case CharArray:
            bind[i].buffer = va_arg(args, char *);
            bind[i].buffer_length = item.length;
            bind[i].buffer_type = MYSQL_TYPE_STRING;
            // Ensure this is a string even if it will be `NULL'
            ((char *) bind[i].buffer)[0] = '\0';
            break;
        case Double: // Add an double
            if (item.long_modifier == false) {
                bind[i].buffer = va_arg(args, float *);
                bind[i].buffer_type = MYSQL_TYPE_FLOAT;
            } else {
                bind[i].buffer = va_arg(args, double *);
                bind[i].buffer_type = MYSQL_TYPE_DOUBLE;
            }
            break;
        }
        // Get the next format specifier
        fmt = htisql_mysql_easy_query_fetch_format_item(&item, fmt);
    }
    // Return the data
    return bind;
}


MYSQL_STMT *
htisql_mysql_easy_vquery(MYSQL *mysql, const char *const query,
                                         const char *const format, va_list args)
{
    MYSQL_STMT *stmt;
    MYSQL_BIND *bind[2];
    char *parameters;
    char *results;

    parameters = bt_strdup(format);
    if (parameters == NULL)
        return NULL;
    stmt = NULL;
    if ((results = strchr(parameters, '|')) != NULL)
        *(results++) = '\0';
    // Initialize the parameters (this is the most horrible part)
    bind[0] = htisql_mysql_easy_make_bind(parameters, args);
    bind[1] = htisql_mysql_easy_make_bind(results, args);
    // Create a new MySQL statement object
    stmt = mysql_stmt_init(mysql);
    if (stmt == NULL)
        goto error;
    // Prepare the query (another annoying part)
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0)
        goto error;
    // Bind the parameters (if there are any)
    if ((bind[0] != NULL) && (mysql_stmt_bind_param(stmt, bind[0]) != 0))
        goto error;
    // Bind the results (if we expect any)
    if ((bind[1] != NULL) && (mysql_stmt_bind_result(stmt, bind[1]) != 0))
        goto error;
    QUERY_EXECUTION_DEBUG_START
    // Execute the query
    if (mysql_stmt_execute(stmt) != 0)
        goto error;
    QUERY_EXECUTION_DEBUG_END
    // Free the parameters
    for (size_t i = 0; i < 2; ++i)
        bt_free(bind[i]);
    // Free the copy of the format string
    bt_free(parameters);
    return stmt;
error:
    bt_free(parameters);
    for (size_t i = 0; i < 2; ++i)
        bt_free(bind[i]);
    if (stmt == NULL)
        return NULL;
    if (mysql_stmt_errno(stmt) != 0)
        log("%s:mysql: %s\n", __FILE__, mysql_stmt_error(stmt));
    mysql_stmt_close(stmt);
    return NULL;
}

MYSQL_STMT *
htisql_mysql_easy_query(MYSQL *mysql, const char *const query,
                                                  const char *const format, ...)
{
    MYSQL_STMT *stmt;
    va_list args;
    // Just a wrapper function
    va_start(args, format);
    stmt = htisql_mysql_easy_vquery(mysql, query, format, args);
    va_end(args);

    return stmt;
}

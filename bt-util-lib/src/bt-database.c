#include <mysql.h>
#include <errmsg.h>
#include <pthread.h>
#include <string.h>
#include <stdio.h>

#include <sys/stat.h>
#include <pthread.h>

#include <bt-william-hill-events.h>
#include <bt-players.h>
#include <bt-context.h>
#include <bt-debug.h>
#include <bt-database.h>
#include <bt-telegram-channel.h>
#include <bt-mbet-feed.h>
#include <bt-util.h>
#include <bt-memory.h>
#include <bt-mysql-easy.h>
#include <bt-channel-settings.h>
#include <bt-string-builder.h>

__thread MYSQL *mysql_global = NULL;
pthread_mutex_t mysql_mutex;

#ifndef SYSCONFDIR
#define SYSCONFDIR "/etc"
#endif

#define SETTINGS_FILE_PATH SYSCONFDIR "/bt/bt.conf"

static bool
bt_parse_settings_file(char **user, char **password, char **dbname, char **host)
{
    size_t length;
    struct stat st;
    FILE *file;
    // Initialize everything, in case of early return
    *user = NULL;
    *password = NULL;
    *dbname = NULL;
    *host = NULL;
    // Check if the file exists, and get information about it
    if (stat(SETTINGS_FILE_PATH, &st) == -1)
        return false;
    // Store it's length
    length = st.st_size;
    // Try to open the file (this shouldn't fail, except for permissions)
    file = fopen(SETTINGS_FILE_PATH, "r");
    if (file != NULL)
    {
        // Allocate space to read the whole file (it will be small so...)
        char content[length + 1];
        char *token;
        // Read the whole content of the file
        if (fread(content, 1, length, file) != length)
            goto error;
        // `null` terminate it so it's a "string"
        content[length] = '\0';
        // Tokenize the file and extract every thing. In the event
        // of an error simply go to the error label, cleanup and
        // get out.
        token = strtok(content, ";");
        if (token == NULL)
            goto error;
        *user = bt_stripdup(token, &length);
        token = strtok(NULL, ";");
        if (token == NULL)
            goto error;
        *password = bt_stripdup(token, &length);
        token = strtok(NULL, ";");
        if (token == NULL)
            goto error;
        *dbname = bt_stripdup(token, &length);
        token = strtok(NULL, ";");
        if (token == NULL)
            goto error;
        *host = bt_stripdup(token, &length);
        // Close the file
        fclose(file);
    }
    // Inform the caller that every thing is fine
    return true;
error:
    // Ensure that the caller wont do anything special
    // invoking undefined behavior
    bt_free(*user);
    *user = NULL;
    bt_free(*password);
    *password = NULL;
    bt_free(*dbname);
    *dbname = NULL;
    bt_free(*host);
    *host = NULL;
    // Close the file
    fclose(file);
    // Tell the user about the tragedy
    return false;
}

static MYSQL *
bt_connect_to_mysql_helper(const char *const user, const char *const password,
                               const char *const dbname, const char *const host)
{
    MYSQL *mysql;
    // Create a `MYSQL` object
    mysql = mysql_init(NULL);
    if (mysql == NULL)
        return NULL; //
    // Connect to the database given the parameters
    if (mysql_real_connect(mysql, host, user, password,
                                 dbname, 0, NULL, CLIENT_INTERACTIVE) == NULL) {
        // What the hell happened?
        log("%s\n", mysql_error(mysql));
        // Ok, release resources
        mysql_close(mysql);
        mysql = NULL;
    } else {
        // Great, set character set
        mysql_set_character_set(mysql, "utf8");
    }
    return mysql;
}

static MYSQL *
bt_connect_to_mysql()
{
    MYSQL *mysql;
    char *user;
    char *password;
    char *dbname;
    char *host;

    mysql = NULL;
    // Read the settings file
    if (bt_parse_settings_file(&user, &password, &dbname, &host) == true) {
        pthread_mutex_lock(&mysql_mutex);
        mysql = bt_connect_to_mysql_helper(user, password, dbname, host);
        pthread_mutex_unlock(&mysql_mutex);
    }
    // Release resources
    bt_free(user);
    bt_free(password);
    bt_free(dbname);
    bt_free(host);
    // Return  the result, might be `NULL`
    return mysql;
}

void
bt_database_initialize(void)
{
    mysql_thread_init();
    mysql_global = bt_connect_to_mysql();
}

void
bt_database_finalize(void)
{
    mysql_close(mysql_global);
    mysql_global = NULL;
    mysql_thread_end();
}

const char *
bt_get_category_name(bt_tennis_category category)
{
    // Simply return the category name
    if ((category & CategoryATP) == CategoryATP) {
        return "atp";
    } else if ((category & CategoryWTA) == CategoryWTA) {
        return "wta";
    }
    return NULL;
}

int
bt_get_tournament_id_from_players(bt_tennis_category catid,
                             int id1, int id2, int *tour, int *round, int *rank)
{
    char *query;
    MYSQL_STMT *stmt;
    const char *category;
    int result;

    // Initialize all the OUTPUT variables
    *tour = -1;
    *round = -1;
    *rank = -1;
    // Variable initialization
    result = -1; // Initialize the result flag
    category = bt_get_category_name(catid); // Initialize the category
    if (category == NULL)
        return -1;
    query = bt_load_query("tourid from ids", "%category%", category, NULL);
    if (query == NULL)
        return -1;
    // Create the statement (execute the query)
    stmt = bt_mysql_easy_query(query, "%d%d%d%d|%d%d%d",
                                     &id1, &id2, &id2, &id1, tour, round, rank);
    // Release resources
    bt_free(query);
    if (stmt == NULL)
        goto error;
    // Grab the result and store it in `tour`, `round`, `rank`
    result = mysql_stmt_fetch(stmt);
    if ((result != 0) && (result != 100)) {
        log("error: mysql(%d:%s)\n", result, mysql_stmt_error(stmt));
    }
    mysql_stmt_free_result(stmt);
    mysql_stmt_close(stmt);
error:

    return result;
}

int
bt_database_oncourt_local_version(void)
{
    MYSQL_STMT *stmt;
    int version;
    // Initialize the return value
    version = -1;
    // Execute this simple query
    stmt = bt_mysql_easy_query("SELECT `VER` FROM `version`", "|%d", &version);
    if (stmt == NULL)
        return -1;
    mysql_stmt_fetch(stmt);
    mysql_stmt_free_result(stmt);
    mysql_stmt_close(stmt);
    return version;
}


int
bt_mysql_execute_query(const char *const query)
{
    MYSQL_STMT *stmt;
    int result;
    // Ensure this is initialized
    result = -1;
    // Create a statement
    stmt = bt_database_new_stmt();
    if (stmt == NULL)
        return -1;
    // Prepare it
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0)
        goto error;
    // Execute it
    result = (mysql_stmt_execute(stmt) == 0) ? 0 : -1;
error:
    // Check for errors and display them in case they happened
    if ((result != 0) && ((mysql_stmt_errno(stmt) != 1062)))
        log("MySQL %s: %s\n", __FUNCTION__, mysql_stmt_error(stmt));
    // Release resources
    mysql_stmt_close(stmt);
    return result;
}

static bool
bt_database_mysql_server_ready(void)
{
    int result;
    // Check if the `mysql_global' is running and connected
    if ((mysql_global != NULL) && ((result = mysql_ping(mysql_global)) != 0)) {
        switch (result) {
        case CR_COMMANDS_OUT_OF_SYNC:
            // Problems here, what to do?
            mysql_global = NULL;
            log("WARNING: the mysql server was lost and I could not prevent it...\n");
            // BUG: perhaps close the previous connection to
            //      recover anyway and keep running
            break;
        default:
            // Reset the connection
            bt_database_finalize();
            bt_database_initialize();
            break;
        }
    } else  if (mysql_global == NULL) {
        // If the connection does not exist, create it
        mysql_global = bt_connect_to_mysql();
    }
    // If we reach here, and still `mysql_global` is NULL then, WTF?
    if (mysql_global == NULL)
        return NULL;
    // Return the connection object
    return mysql_global;
}

MYSQL_STMT *
bt_mysql_easy_query(const char *const query, const char *const format, ...)
{
    MYSQL_STMT *stmt;
    va_list args;
    // Check if the we are connected or can connect
    if (bt_database_mysql_server_ready() == false)
        return NULL;
    // Prepare the va_list to send it to the original function
    va_start(args, format);
    // Call the real funcion
    stmt = htisql_mysql_easy_vquery(mysql_global, query, format, args);
    // Always code correctly!
    va_end(args);
    // Return the resulting MYSQL_STMT, where the query has been executed
    return stmt;
}

MYSQL_STMT *
bt_database_new_stmt(void)
{
    // Check if the we are connected or can connect
    if (bt_database_mysql_server_ready() == false)
        return NULL;
    // Create a statment with the thread local `mysql_global`.
    return mysql_stmt_init(mysql_global);
}

bool
bt_next_doubles_for_player(const char *const category, int id)
{
    MYSQL_STMT *stmt;
    int count;
    char *query;

    // Initialize the return value
    count = 0;
    // Find the required query
    query = bt_load_query("will play doubles?", "%category%", category, NULL);
    if (query == NULL)
        return false;
    fprintf(stderr, "%s\n", query);
    // Execute the query
    stmt = bt_mysql_easy_query(query, "%d%d|%d", &id, &id, &count);
    if (stmt == NULL)
        goto error;
    mysql_stmt_fetch(stmt);
    mysql_stmt_free_result(stmt);
    mysql_stmt_close(stmt);
error:
    bt_free(query);
    return (count > 0);
}

bool
bt_mysql_is_null(MYSQL_BIND *result)
{
    // WTF?
    if (result == NULL)
        return 1;
    // Oh, again WTF?
    if (result->is_null == NULL)
        return 0;
    // Return whether the value is a MySQL NULL
    return (result->is_null[0] == 1);
}

int
bt_database_mto_query(const char *const sql, int match, int player)
{
    MYSQL_STMT *stmt;
    int result;

    // Intialize the return value
    result = 0;
    // Execute the query
    stmt = bt_mysql_easy_query(sql, "%d%d|%d", &match, &player, &result);
    if (stmt == NULL)
        return result;
    mysql_stmt_fetch(stmt);
    mysql_stmt_free_result(stmt);
    mysql_stmt_close(stmt);
    // Return the result
    return result;
}

int
bt_database_save_mto_message_id(int value, int match, const char *const channel)
{
    MYSQL_STMT *stmt;
    char *query;
    int result;

    // Initialize the return value
    result = -1;
    // Find the query
    query = bt_load_query("update mto", "%category%", "atp", NULL);
    if (query == NULL)
        return -1;
    // Execute it
    stmt = bt_mysql_easy_query(query, "%d%d%s", &match, &value, channel);
    if (stmt == NULL)
        goto error;
    // Cleanup resources
    mysql_stmt_close(stmt);
error:
    bt_free(query);
    return result;
}

int
bt_database_mto_message_id(int match, const char *const channel)
{
    char *query;
    MYSQL_STMT *stmt;
    int id;
    // With this default, the message is sent instead of edited
    id = -1;
    // Load the query
    query = bt_load_query("mto tgid", "", NULL);
    if (query == NULL)
        return -1;
    // Execute the query
    stmt = bt_mysql_easy_query(query, "%d%s|%d", &match, channel, &id);
    if (stmt != NULL) {
        mysql_stmt_fetch(stmt);
        mysql_stmt_free_result(stmt);
        mysql_stmt_close(stmt);
    }
    bt_free(query);
    return id;
}

int
bt_database_count_mto(int match, const char *const player)
{
    char *query;
    int count;
    MYSQL_STMT *stmt;
    count = 0;
    query = bt_load_query("mto count", "%category%", "atp", NULL);
    if (query == NULL)
        return count;
    stmt = bt_mysql_easy_query(query, "%d%s|%d", &match, player, &count);
    if (stmt != NULL) {
        mysql_stmt_fetch(stmt);
        mysql_stmt_free_result(stmt);
        mysql_stmt_close(stmt);
    }
    bt_free(query);
    return count;
}

void
bt_database_begin(void)
{
    if (bt_database_mysql_server_ready() == false)
        return;
    mysql_autocommit(mysql_global, 0);
}

void
bt_database_rollback(void)
{
    if (bt_database_mysql_server_ready() == false)
        return;
    mysql_rollback(mysql_global);
    mysql_autocommit(mysql_global, 1);
}

void
bt_database_commit(void)
{
    if (bt_database_mysql_server_ready() == false)
        return;
    mysql_commit(mysql_global);
    mysql_autocommit(mysql_global, 1);
}

bool
bt_database_get_tournament_name(bt_tennis_category category, int id, char **out_name, char **out_flag, char **out_court)
{
    char name[256];
    char court[256];
    char flag[256];
    MYSQL_STMT *stmt;
    const char *catname;
    char *query;

    *out_name = NULL;
    *out_court = NULL;
    *out_flag = NULL;

    if ((category & CategoryATP) == CategoryATP) {
        catname = "atp";
    } else if ((category & CategoryWTA) == CategoryWTA) {
        catname = "wta";
    } else {
        return false;
    }

    query = bt_load_query("tour data", "%category%", catname, NULL);
    if (query == NULL)
        return false;
    stmt = bt_mysql_easy_query(query, "%d|%256a%256a%256a", &id, name, flag, court);

    bt_free(query);
    if (stmt == NULL)
        return false;
    if (mysql_stmt_fetch(stmt) != 0) {
        mysql_stmt_free_result(stmt);
        mysql_stmt_close(stmt);

        return false;
    }
    mysql_stmt_free_result(stmt);
    mysql_stmt_close(stmt);

    *out_name = bt_strdup(name);
    if (*out_name == NULL)
        goto error;
    *out_court = bt_strdup(court);
    if (*out_court == NULL)
        goto error;
    *out_flag = bt_strdup(flag);
    if (*out_flag == NULL)
        goto error;
    return true;
error:
    bt_free(*out_name);
    bt_free(*out_court);
    bt_free(*out_flag);
    return false;
}

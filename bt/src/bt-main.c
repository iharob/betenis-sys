#include <bt-channel-settings.h>

#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>

#include <ctype.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>

#include <dirent.h>
#include <sys/wait.h>
#include <bt-util.h>

#include <http-connection.h>
#include <http-websockets.h>
#include <http-protocol.h>

#include <bt-william-hill-main.h>
#include <bt-mbet.h>
#include <bt-pinnacle.h>

#include <bt-daemon.h>
#include <bt-context.h>

#include <bt-oncourt-database.h>

#include <mysql.h>
#include <json.h>

#include <pthread.h>

#include <libxml/HTMLparser.h>

#include <errno.h>

#include <unistd.h>
#include <fcntl.h>

#include <bt-memory.h>
#include <bt-debug.h>
#include <bt-telegram-channel.h>
#include <bt-database.h>

static void bt_initialize() __attribute__((constructor));
static void bt_finalize(void) __attribute__((destructor));
typedef void *(*thread_start_function)(void *);

static bt_context *context;

#define CRASH_ALERT "\u26A0 Ha ocurrido una falta grave y el programa se ha cerrado. Intentaré reiniciarlo, avisaré si <b>NO</b> tuve éxito."
#define UNSTARTABLE_ALERT "\u26A0 Me ha sido imposible iniciar el programa, debe intervenir de inmediato."
#define EXIT_ALERT "\u26A0 El programa ha salido correctamente. Está cerrado ahora, espero que usted lo sepa."
#define EMERGENCY_CHANNEL "-1001069932343"

/*static void
signalhandler(int sig, siginfo_t *information, void *context)
{
    (void) sig;
    (void) context;

    switch (information->si_signo) {
    case SIGTERM:
        break;
    case SIGINT:
    case SIGKILL:
        BeginCriticalSection
        bt_context_stop(context);
        EndCriticalSection
        break;
    default:
        break;
    }
}*/

static int
bt_install_signal_handler(void)
{
    /*struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = signalhandler;
    sa.sa_flags = SA_SIGINFO;
    if (sigaction(SIGINT, &sa, NULL) == -1)
        return -1;
    if (sigaction(SIGKILL, &sa, NULL) == -1)
        return -1;*/
    return 0;
}

typedef struct bt_thread {
    pthread_t thread;
    thread_start_function start;
    const char *const name;
} bt_thread;

static bt_thread threads[] = {
    {-1, bt_william_hill_events_provider, "William hill event provider"},
    {-1, bt_william_hill_events_listener, "William hill event listener"},
    /*{-1, bt_watch_oncourt_database      , "OnCourt database updater"},*/
    /*{-1, bt_mbet_feed_prematch          , "MarathonBET pre match feed"},*/
    /*{-1, bt_mbet_feed_live              , "MarathonBET live match feed"},*/
    /*{-1, bt_pinnacle_main               , "Pinnacle Odds Feed"}*/
};

static int
bt_main(void)
{
    if (bt_channel_settings_count() == 0) {
        fprintf(stderr, "error: no hay canales configurados, abortando\n");
    } else {
        if (bt_is_daemon_running() != 0)
            return -1;
        context = bt_create_context();
        if (context == NULL)
            return -1;
        srand(time(NULL));
        for (size_t i = 0; i < countof(threads); ++i) {
            bt_thread *T;
            T = &threads[i];
            if (pthread_create(&T->thread, NULL, T->start, context) == 0)
                continue;
            goto failure;
        }
        if (bt_start_daemon(context) == -1)
            goto failure;
        for (ssize_t i = 0; i < countof(threads); ++i) {
            bt_thread *T;
            T = &threads[i];
            pthread_kill(T->thread, SIGTERM);
            pthread_join(T->thread, NULL);
        }
    }
failure:
    bt_context_free(context);
    return 0;
}

int
usage(const char *const program)
{
    fprintf(stderr, "Uso: %s {start|stop}\n", program);
    return -1;
}

static int
bt_compare_dirnames(const void *const lo, const void *const ro)
{
    return strcmp(*(const char **) lo, *(const char **) ro);
}

int
bt_scan_oncourt_dir(const char *const path)
{
    char **names;
    struct dirent *entry;
    size_t count;
    DIR *dir;
    size_t i;

    chdir(path);

    dir = opendir(".");
    if (dir == NULL)
        return -1;
    count = 0;
    while ((entry = readdir(dir)) != NULL)
        count += (strstr(entry->d_name, ".onc") == NULL) ? 0 : 1;
    rewinddir(dir);
    names = NULL;
    if (count == 0)
        goto error;
    names = bt_malloc(count * sizeof(*names));
    if (names == NULL)
        goto error;
    i = 0;
    while ((entry = readdir(dir)) != NULL) {
        if (strstr(entry->d_name, ".onc") == NULL)
            continue;
        names[i++] = bt_strdup(entry->d_name);
    }
    closedir(dir);

    dir = NULL;

    bt_database_initialize();
    bt_database_begin();

    qsort(names, count, sizeof(*names), bt_compare_dirnames);
    for (size_t i = 0; i < count; ++i) {
        char *data;
        size_t size;
        FILE *file;

        file = fopen(names[i], "r");
        if (file == NULL)
            continue;
        fseek(file, 0L, SEEK_END);
        size = ftell(file);
        fseek(file, 0L, SEEK_SET);

        data = bt_malloc(size + 1);
        if (data != NULL) {
            log("%s\n", names[i]);
            if (fread(data, 1, size, file) != size)
                goto error;
            // `null' terminate it
            data[size] = '\0';
            // Execute commands
            if (bt_oncourt_database_parse_update(data) != 0) {
                bt_free(data);
                bt_database_rollback();
                bt_database_finalize();
                goto error;
            }
            free(data);
        }
        fclose(file);
    }
    bt_database_commit();
    bt_database_finalize();

    for (size_t i = 0; i < count; ++i)
        bt_free(names[i]);
    bt_free(names);

    return 0;
error:
    if (names != NULL) {
        for (size_t i = 0; i < count; ++i)
            bt_free(names[i]);
        bt_free(names);
    }
    if (dir != NULL)
        closedir(dir);
    return -1;
}

static void
bt_initialize()
{
    char *argv[] = {"bt-daemon", NULL};
    // Ensure the `bt_load_query()' function will work
    bt_sort_queries();
    // Initialie libxml2
    xmlInitParser();
    // Initialize MySQL
    mysql_server_init(1, argv, NULL);
    // Start the main function
}

static void
bt_finalize(void)
{
    // Finalize MySQL
    mysql_server_end();
    // Finalize libxml2
    xmlCleanupParser();
}

int
start(int argc, char **argv)
{
    if (strcmp(argv[1], "loaddb") == 0) {
        if (argc < 3)
            return usage(argv[0]);
        bt_scan_oncourt_dir(argv[2]);
    } else if (strcmp(argv[1], "start") == 0) {
        int result;
        switch (0) { // fork())
        case -1:
            log("error: %s\n", strerror(errno));
            break;
        case 0:
            result = bt_main();
            return result;
        default:
            _exit(0);
            break;
        }
    } else if (strcmp(argv[1], "stop") == 0) {
        bt_stop_daemon();
    } else {
        return usage(argv[0]);
    }
    return 0;
}

int
main(int argc, char **argv)
{
    pid_t child;
    int wstatus;
    if (argc < 2)
        return usage(argv[0]);
    for (;;) {
        child = fork();
        switch (child) {
        case -1:
            bt_telegram_send_message(EMERGENCY_CHANNEL, UNSTARTABLE_ALERT);
            return -1;
        case 0:
            if (bt_install_signal_handler() == -1)
                return -1;
            start(argc, argv);
            _exit(0);
            break;
        default:
            if (bt_install_signal_handler() == -1)
                return -1;
            if (waitpid(child, &wstatus, 0) == -1)
                return -1;
            if (WIFSIGNALED(wstatus) == true) {
                bt_telegram_send_message(EMERGENCY_CHANNEL, CRASH_ALERT);
            } else if (WIFSIGNALED(wstatus) == false) {
                bt_telegram_send_message(EMERGENCY_CHANNEL, EXIT_ALERT);
                return 0;
            }
            break;
        }
    }
}


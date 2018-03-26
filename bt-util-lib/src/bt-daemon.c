#include <bt-context.h>
#include <bt-daemon.h>

#include <stdlib.h>

#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>

#include <unistd.h>
#include <fcntl.h>

#include <signal.h>
#include <stdio.h>

#include <errno.h>

#include <bt-private.h>
#include <bt-debug.h>

#define SOCKET_PATH "/tmp/betenisd.sock"

void
bt_daemon_start(int sock, bt_context *const context)
{
    int peer;
    // Wait for messages
    while ((peer = accept(sock, NULL, 0)) != -1) {
        char buffer[100];
        ssize_t count;
        // Read the message
        if ((count = read(peer, buffer, sizeof(buffer) - 1)) <= 0)
            continue;
        // Shutdown request
        shutdown(peer, SHUT_RDWR);
        // Close the peer socket
        close(peer);
        // Check if this is a stop message
        if ((count == 4) && (memcmp(buffer, "stop", 4) == 0)) {
            log("Stopping now!!!\n");
            // Stop the program now
            bt_context_stop(context);
            // Shut the listening socket
            shutdown(sock, SHUT_RDWR);
            // Close the listening socket
            close(sock);
            // Delete socket file
            unlink(SOCKET_PATH);
            return;
        }
    }
    // An error occurred
    shutdown(sock, SHUT_RDWR);
    close(sock);
}

static size_t
bt_daemon_initialize_socket_address(struct sockaddr_un *address)
{
    address->sun_family = AF_UNIX;
    for (size_t i = 0; i < sizeof(SOCKET_PATH); ++i)
        address->sun_path[i] = SOCKET_PATH[i];
    return sizeof(*address);
}

int
bt_daemon_connect(void)
{
    int sock;
    struct sockaddr_un address;
    socklen_t length;

    sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock == -1)
        return -1;
    length = bt_daemon_initialize_socket_address(&address);
    if (connect(sock, (struct sockaddr *) &address, length) == 0)
        return sock;
    shutdown(sock, SHUT_RDWR);
    close(sock);

    return -1;
}

void
bt_stop_daemon(void)
{
    int daemon;
    daemon = bt_daemon_connect();
    if (daemon == -1) {
        log("can't find the daemon, sorry\n");
        return;
    }
    if (write(daemon, "stop", 4) != 4) {
        log("can't tell the daemon to close!\n");
        close(daemon);
        return;
    }
    close(daemon);
}

int
bt_start_daemon(bt_context *const context)
{
    int sock;
    struct sockaddr_un address;
    socklen_t length;
    if (bt_is_daemon_running() == true)
        return -1;
    sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock == -1)
        return -1;
    length = bt_daemon_initialize_socket_address(&address);
    if ((bind(sock, (struct sockaddr *) &address, length) == 0) && (listen(sock, 100) != 0)) {
        goto failure;
    }
    bt_daemon_start(sock, context);
    return 0;
failure:
    shutdown(sock, SHUT_RDWR);
    close(sock);

    return -1;
}

int
bt_is_daemon_running(void)
{
    int sock;
    sock = bt_daemon_connect();
    if (sock != -1) {
        char running[7];
        if (write(sock, "status", 6) != 6)
            unlink(SOCKET_PATH);
        else if (read(sock, running, 7) != 7)
            unlink(SOCKET_PATH);
        else {
            if (memcmp(running, "running", 7) != 0)
                return 0;
            unlink(SOCKET_PATH);
        }
        shutdown(sock, SHUT_RDWR);
        close(sock);
    } else {
        unlink(SOCKET_PATH);
    }
    return 0;
}

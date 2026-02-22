#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <stdbool.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <syslog.h>
#include <sys/stat.h>

#define PORT 9000
#define BACKLOG 10
#define BUFFER_SIZE 1024
#define DATA_FILE "/var/tmp/aesdsocketdata"

static int server_fd = -1;
static volatile sig_atomic_t exit_requested = 0;

/* ---------------- SIGNAL HANDLER ---------------- */

void handle_signal(int signo)
{
    syslog(LOG_INFO, "Caught signal, exiting");
    exit_requested = 1;
}

/* ---------------- MAIN ---------------- */

int main(int argc, char *argv[])
{
    bool daemon_mode = false;

    if (argc == 2 && strcmp(argv[1], "-d") == 0)
        daemon_mode = true;

    openlog("aesdsocket", LOG_PID, LOG_USER);

    unlink(DATA_FILE);

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    /* -------- CREATE SOCKET -------- */
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        return -1;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    /* -------- BIND -------- */
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&server_addr,
             sizeof(server_addr)) < 0) {
        perror("bind");
        close(server_fd);
        return -1;
    }

    /* -------- DAEMON MODE -------- */
    if (daemon_mode) {
        pid_t pid = fork();
        if (pid < 0) {
            return -1;
        }

        if (pid > 0) {
            exit(EXIT_SUCCESS);  // parent exits
        }

        if (setsid() < 0) {
            return -1;
        }

        if (chdir("/") < 0) {
            return -1;
        }

        close(STDIN_FILENO);
        close(STDOUT_FILENO);
        close(STDERR_FILENO);
    }

    /* -------- LISTEN -------- */
    if (listen(server_fd, BACKLOG) < 0) {
        perror("listen");
        close(server_fd);
        return -1;
    }

    /* -------- MAIN LOOP -------- */
    while (!exit_requested) {

        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        int client_fd = accept(server_fd,
                               (struct sockaddr *)&client_addr,
                               &client_len);

        if (client_fd < 0) {
            if (errno == EINTR)
                continue;
            break;
        }

        syslog(LOG_INFO, "Accepted connection from %s",
               inet_ntoa(client_addr.sin_addr));

        char buffer[BUFFER_SIZE];
        char *packet = NULL;
        size_t packet_size = 0;

        while (!exit_requested) {

            ssize_t bytes = recv(client_fd, buffer, BUFFER_SIZE, 0);
            if (bytes <= 0)
                break;

            for (ssize_t i = 0; i < bytes; i++) {

                char *tmp = realloc(packet, packet_size + 1);
                if (!tmp) {
                    free(packet);
                    close(client_fd);
                    return -1;
                }

                packet = tmp;
                packet[packet_size++] = buffer[i];

                if (buffer[i] == '\n') {

                    /* Append to file */
                    int fd = open(DATA_FILE,
                                  O_CREAT | O_WRONLY | O_APPEND,
                                  0644);
                    if (fd < 0) {
                        free(packet);
                        close(client_fd);
                        return -1;
                    }

                    write(fd, packet, packet_size);
                    close(fd);

                    free(packet);
                    packet = NULL;
                    packet_size = 0;

                    /* Send entire file back */
                    fd = open(DATA_FILE, O_RDONLY);
                    if (fd < 0) {
                        close(client_fd);
                        return -1;
                    }

                    ssize_t read_bytes;
                    while ((read_bytes = read(fd, buffer,
                                              BUFFER_SIZE)) > 0) {
                        send(client_fd, buffer,
                             read_bytes, 0);
                    }

                    close(fd);
                }
            }
        }

        free(packet);
        close(client_fd);

        syslog(LOG_INFO, "Closed connection from %s",
               inet_ntoa(client_addr.sin_addr));
    }

    /* -------- CLEANUP -------- */
    close(server_fd);
    unlink(DATA_FILE);
    closelog();

    return 0;
}
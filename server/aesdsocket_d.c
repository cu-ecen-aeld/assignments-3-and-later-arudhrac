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
#define DATA_FILE "/var/tmp/aesdsocketdata"
#define BACKLOG 10
#define BUFFER_SIZE 1024

static int server_fd = -1;
static volatile sig_atomic_t exit_flag = 0;

void signal_handler(int signo)
{
    syslog(LOG_INFO, "Caught signal, exiting");
    exit_flag = 1;
}

int main()
{
    int client_fd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);
    char buffer[BUFFER_SIZE];
    char *packet = NULL;
    size_t packet_size = 0;

    openlog("aesdsocket", LOG_PID, LOG_USER);

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    /* Create socket */
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        syslog(LOG_ERR, "Socket creation failed");
        return -1;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    /* Bind */
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        syslog(LOG_ERR, "Bind failed");
        close(server_fd);
        return -1;
    }

    /* Listen */
    if (listen(server_fd, BACKLOG) < 0) {
        syslog(LOG_ERR, "Listen failed");
        close(server_fd);
        return -1;
    }

    while (!exit_flag) {

        client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd < 0) {
            if (errno == EINTR)
                continue;
            syslog(LOG_ERR, "Accept failed");
            break;
        }

        syslog(LOG_INFO, "Accepted connection from %s",
               inet_ntoa(client_addr.sin_addr));

        packet = NULL;
        packet_size = 0;

        while (!exit_flag) {
            ssize_t bytes = recv(client_fd, buffer, BUFFER_SIZE, 0);
            if (bytes <= 0)
                break;

            char *newline;
            size_t offset = 0;

            while ((newline = memchr(buffer + offset, '\n', bytes - offset))) {

                size_t chunk = newline - (buffer + offset) + 1;

                char *tmp = realloc(packet, packet_size + chunk);
                if (!tmp) {
                    free(packet);
                    syslog(LOG_ERR, "Memory allocation failed");
                    close(client_fd);
                    exit(EXIT_FAILURE);
                }

                packet = tmp;
                memcpy(packet + packet_size, buffer + offset, chunk);
                packet_size += chunk;

                /* Append to file */
                int fd = open(DATA_FILE, O_CREAT | O_WRONLY | O_APPEND, 0644);
                if (fd >= 0) {
                    write(fd, packet, packet_size);
                    close(fd);
                }

                free(packet);
                packet = NULL;
                packet_size = 0;

                /* Send full file back */
                fd = open(DATA_FILE, O_RDONLY);
                if (fd >= 0) {
                    ssize_t read_bytes;
                    while ((read_bytes = read(fd, buffer, BUFFER_SIZE)) > 0) {
                        send(client_fd, buffer, read_bytes, 0);
                    }
                    close(fd);
                }

                offset += chunk;
            }

            /* Remaining partial packet */
            if (offset < bytes) {
                size_t chunk = bytes - offset;
                char *tmp = realloc(packet, packet_size + chunk);
                if (!tmp) {
                    free(packet);
                    syslog(LOG_ERR, "Memory allocation failed");
                    close(client_fd);
                    exit(EXIT_FAILURE);
                }
                packet = tmp;
                memcpy(packet + packet_size, buffer + offset, chunk);
                packet_size += chunk;
            }
        }

        free(packet);
        close(client_fd);

        syslog(LOG_INFO, "Closed connection from %s",
               inet_ntoa(client_addr.sin_addr));
    }

    close(server_fd);
    unlink(DATA_FILE);
    closelog();

    return 0;
}

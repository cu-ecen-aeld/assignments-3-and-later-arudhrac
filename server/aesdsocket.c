#define _GNU_SOURCE

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <syslog.h>
#include <unistd.h>

#define PORT "9000"
#define DATAFILE "/var/tmp/aesdsocketdata"
#define BACKLOG 10

static volatile sig_atomic_t exit_requested = 0;
static int serverfd_global = -1;

/* ================= SIGNAL HANDLER ================= */

static void signal_handler(int signo)
{
    (void)signo;
    syslog(LOG_INFO, "Caught signal, exiting");
    exit_requested = 1;

    if (serverfd_global != -1) {
        close(serverfd_global);  // unblock accept()
        serverfd_global = -1;
    }
}

/* ================= DAEMONIZE ================= */

static int daemonize(void)
{
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid > 0) exit(EXIT_SUCCESS);

    if (setsid() < 0) return -1;

    if (chdir("/") != 0)
        return -1;

    int fd = open("/dev/null", O_RDWR);
    if (fd < 0) return -1;

    dup2(fd, STDIN_FILENO);
    dup2(fd, STDOUT_FILENO);
    dup2(fd, STDERR_FILENO);

    if (fd > 2)
        close(fd);

    return 0;
}

/* ================= APPEND TO FILE ================= */

static int append_to_file(const char *buf, size_t len)
{
    int fd = open(DATAFILE, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd < 0) return -1;

    size_t written = 0;
    while (written < len) {
        ssize_t rc = write(fd, buf + written, len - written);
        if (rc < 0) {
            if (errno == EINTR) continue;
            close(fd);
            return -1;
        }
        written += rc;
    }

    close(fd);
    return 0;
}

/* ================= SEND FULL FILE ================= */

static int send_full_file(int clientfd)
{
    int fd = open(DATAFILE, O_RDONLY);
    if (fd < 0) return -1;

    char buffer[4096];
    ssize_t r;

    while ((r = read(fd, buffer, sizeof(buffer))) > 0) {
        ssize_t sent = 0;
        while (sent < r) {
            ssize_t s = send(clientfd, buffer + sent, r - sent, 0);
            if (s < 0) {
                if (errno == EINTR) continue;
                close(fd);
                return -1;
            }
            sent += s;
        }
    }

    close(fd);
    return 0;
}

/* ================= CREATE SOCKET ================= */

static int create_server_socket(void)
{
    struct addrinfo hints, *res, *p;
    int sockfd = -1;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    if (getaddrinfo(NULL, PORT, &hints, &res) != 0)
        return -1;

    for (p = res; p != NULL; p = p->ai_next) {

        sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (sockfd < 0) continue;

        int opt = 1;
        setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        if (bind(sockfd, p->ai_addr, p->ai_addrlen) == 0)
            break;

        close(sockfd);
        sockfd = -1;
    }

    freeaddrinfo(res);

    if (sockfd < 0)
        return -1;

    if (listen(sockfd, BACKLOG) != 0) {
        close(sockfd);
        return -1;
    }

    return sockfd;
}

/* ================= MAIN ================= */

int main(int argc, char *argv[])
{
    bool daemon_mode = false;

    openlog("aesdsocket", LOG_PID, LOG_USER);

    if (argc == 2 && strcmp(argv[1], "-d") == 0)
        daemon_mode = true;

    unlink(DATAFILE);

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    int serverfd = create_server_socket();
    if (serverfd < 0) {
        syslog(LOG_ERR, "Socket setup failed");
        return -1;
    }

    serverfd_global = serverfd;

    if (daemon_mode) {
        if (daemonize() != 0) {
            syslog(LOG_ERR, "Daemonize failed");
            return -1;
        }
    }

    while (!exit_requested) {

        struct sockaddr_in client_addr;
        socklen_t addrlen = sizeof(client_addr);

        int clientfd = accept(serverfd,
                              (struct sockaddr *)&client_addr,
                              &addrlen);

        if (clientfd < 0) {
            if (exit_requested) break;
            if (errno == EINTR) continue;
            break;
        }

        syslog(LOG_INFO, "Accepted connection from %s",
               inet_ntoa(client_addr.sin_addr));

        char *packet = NULL;
        size_t packet_size = 0;

        while (!exit_requested) {

            char buf[1024];
            ssize_t r = recv(clientfd, buf, sizeof(buf), 0);
            if (r <= 0) break;

            char *tmp = realloc(packet, packet_size + r);
            if (!tmp) break;

            packet = tmp;
            memcpy(packet + packet_size, buf, r);
            packet_size += r;

            char *newline;
            while ((newline = memchr(packet, '\n', packet_size)) != NULL) {

                size_t pkt_len = (newline - packet) + 1;

                append_to_file(packet, pkt_len);
                send_full_file(clientfd);

                size_t remaining = packet_size - pkt_len;
                memmove(packet, packet + pkt_len, remaining);
                packet_size = remaining;
            }
        }

        free(packet);
        shutdown(clientfd, SHUT_RDWR);
        close(clientfd);

        syslog(LOG_INFO, "Closed connection from %s",
               inet_ntoa(client_addr.sin_addr));
    }

    if (serverfd != -1)
        close(serverfd);

    unlink(DATAFILE);
    closelog();

    return 0;
}
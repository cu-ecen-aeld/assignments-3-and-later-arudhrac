#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <syslog.h>
#include <signal.h>
#include <fcntl.h>
#include <arpa/inet.h>

#define PORT "9000"
#define BUFFER_SIZE 1024
#define DATA_FILE "/var/tmp/aesdsocketdata"

volatile sig_atomic_t exit_flag = 0;
int listen_sock = -1;
int data_fd = -1;

void sig_handler(int sig) {
    syslog(LOG_INFO, "Caught signal, exiting");
    exit_flag = 1;
}

void *get_in_addr(struct sockaddr *sa) {
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }
    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

int main(int argc, char *argv[]) {
    int daemon_mode = 0;
    int opt;
    while ((opt = getopt(argc, argv, "d")) != -1) {
        if (opt == 'd') daemon_mode = 1;
    }

    openlog("aesdsocket", 0, LOG_USER);

    if (daemon_mode) {
        pid_t pid = fork();
        if (pid < 0) {
            syslog(LOG_ERR, "fork failed");
            exit(1);
        }
        if (pid > 0) exit(0);
        umask(0);
        setsid();
        chdir("/");
        close(STDIN_FILENO);
        close(STDOUT_FILENO);
        close(STDERR_FILENO);
        int fd = open("/dev/null", O_RDWR);
        dup2(fd, STDIN_FILENO);
        dup2(fd, STDOUT_FILENO);
        dup2(fd, STDERR_FILENO);
        close(fd);
    }

    struct addrinfo hints, *servinfo, *p;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    if (getaddrinfo(NULL, PORT, &hints, &servinfo) != 0) {
        syslog(LOG_ERR, "getaddrinfo failed");
        closelog();
        return -1;
    }

    listen_sock = socket(servinfo->ai_family, servinfo->ai_socktype, servinfo->ai_protocol);
    if (listen_sock < 0) {
        syslog(LOG_ERR, "socket failed");
        freeaddrinfo(servinfo);
        closelog();
        return -1;
    }

    int yes = 1;
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int));

    if (bind(listen_sock, servinfo->ai_addr, servinfo->ai_addrlen) < 0) {
        syslog(LOG_ERR, "bind failed");
        close(listen_sock);
        freeaddrinfo(servinfo);
        closelog();
        return -1;
    }

    freeaddrinfo(servinfo);

    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    if (listen(listen_sock, 5) < 0) {
        syslog(LOG_ERR, "listen failed");
        close(listen_sock);
        closelog();
        return -1;
    }

    char client_ip[INET6_ADDRSTRLEN];
    while (!exit_flag) {
        struct sockaddr_storage client_addr;
        socklen_t addr_len = sizeof(client_addr);
        int client_sock = accept(listen_sock, (struct sockaddr*)&client_addr, &addr_len);
        if (client_sock < 0) {
            if (exit_flag) break;
            continue;
        }

        inet_ntop(client_addr.ss_family, get_in_addr((struct sockaddr*)&client_addr), client_ip, sizeof(client_ip));
        syslog(LOG_INFO, "Accepted connection from %s", client_ip);

        data_fd = open(DATA_FILE, O_RDWR | O_CREAT | O_TRUNC, 0666);
        if (data_fd < 0) {
            syslog(LOG_ERR, "open data file failed");
            close(client_sock);
            continue;
        }

        // Fixed packet handling: accumulate until \n
        char recv_buf[BUFFER_SIZE];
        char *buf_ptr = recv_buf;
        size_t buf_len = 0;
        ssize_t bytes_recvd;

        while (!exit_flag) {
            bytes_recvd = recv(client_sock, buf_ptr, BUFFER_SIZE - buf_len, 0);
            if (bytes_recvd <= 0) {
                break;  // EOF or error
            }
            buf_ptr += bytes_recvd;
            buf_len += bytes_recvd;

            char *newline_pos;
            while ((newline_pos = (char *)memchr(recv_buf, '\n', buf_len)) != NULL) {
                size_t packet_len = (newline_pos - recv_buf) + 1;
                ssize_t written = write(data_fd, recv_buf, packet_len);
                if (written != (ssize_t)packet_len) {
                    syslog(LOG_ERR, "write failed");
                }
                size_t remaining = buf_len - packet_len;
                memmove(recv_buf, newline_pos + 1, remaining);
                buf_len = remaining;
            }
            if (buf_len >= BUFFER_SIZE - 1) {
                syslog(LOG_WARNING, "overlength packet, discarding");
                buf_len = 0;
            }
        }

        // Send full file back
        lseek(data_fd, 0, SEEK_SET);
        char send_buf[BUFFER_SIZE];
        ssize_t bytes_read;
        while ((bytes_read = read(data_fd, send_buf, sizeof(send_buf))) > 0) {
            send(client_sock, send_buf, bytes_read, 0);
        }

        close(data_fd);
        data_fd = -1;
        close(client_sock);

        syslog(LOG_INFO, "Closed connection from %s", client_ip);
    }

    if (data_fd >= 0) {
        close(data_fd);
        unlink(DATA_FILE);
    }
    if (listen_sock >= 0) {
        close(listen_sock);
    }
    closelog();
    return 0;
}

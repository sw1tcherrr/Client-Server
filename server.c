#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <memory.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <unistd.h>

#include "common.h"
#include "server_conf.h"

int setup_listener(char const* port) {
    int listener_fd;
    int yes = 1;
    struct addrinfo hint;
    struct addrinfo* server_info;
    int exit_code;

    memset(&hint, 0, sizeof(hint));
    hint.ai_family = AF_INET;
    hint.ai_socktype = SOCK_STREAM;
    hint.ai_flags = AI_PASSIVE;

    ERR_WRAPPER((exit_code = getaddrinfo(NULL, port, &hint, &server_info)) != 0, gai_strerror(exit_code), error)

    for(struct addrinfo* p = server_info; p != NULL; p = p->ai_next) {
        listener_fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (listener_fd < 0) {
            continue;
        }

        setsockopt(listener_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)); // for no error when restarting

        if (bind(listener_fd, p->ai_addr, p->ai_addrlen) < 0) {
            close(listener_fd);
            continue;
        }

        if (listen(listener_fd, BACKLOG) == -1) {
            close(listener_fd);
            continue;
        }

        freeaddrinfo(server_info);
        return listener_fd;
    }

error:
    freeaddrinfo(server_info);
    return -1;
}

void dump_client(struct sockaddr_in client_addr, int n) {
    char addr[INET_ADDRSTRLEN];
    inet_ntop(client_addr.sin_family, &client_addr.sin_addr, addr, sizeof addr);
    printf("+ Thread %d: Got connection from %s\n", n, addr);
}

void send_response(int socket, int res) {
    if (send(socket, &res, sizeof(res), 0) == -1) {
        perror("- Couldn't send response");
        close(socket);
    }
}

void signal_error_and_close(int socket, char const* msg) {
    perror(msg);
    send_response(socket, -1);
    close(socket);
}

#define SIGCLS_WRAPPER(predicate, sock, msg, label) \
	if (predicate) {                                \
		signal_error_and_close(sock, msg);          \
        goto label;                                 \
	}

ssize_t recv_size(int socket) {
    uint32_t size;
    ssize_t read = recv(socket, &size, sizeof(uint32_t), 0);
    SIGCLS_WRAPPER(read == -1, socket, "- Couldn't receive (size)", error)
    if (read == 0) { // client is already dead
        close(socket);
        return -1;
    }
    size = ntohl(size);

    return size;
error:
    return -1;
}

int recv_all(int socket, char* buf, size_t len) {
    size_t total = 0;
    size_t left = len;
    size_t n;

    while(total < len) {
        n = recv(socket, buf + total, left, 0);
        SIGCLS_WRAPPER(n == -1, socket, "- Couldn't receive (content)", error)
        total += n;
        left -= n;
    }

    return 0;
error:
    return -1;
}

_Noreturn void* server(void* data_) {
    struct thread_data* data = data_;
    int thread_num = data->num;
    int listener_fd = data->listener_fd;
    int epoll_fd = data->epoll_fd;
    char const* path = data->path;
    free(data);

    struct epoll_event event;
    struct epoll_event events[MAX_EVENTS];
    int client_fd;
    struct sockaddr_in client_addr;
    socklen_t len = sizeof(client_addr);

    uint32_t seed = time(0) + thread_num; // for thread-safe random

    for (;;) {
        int event_cnt = epoll_wait(epoll_fd, events, MAX_EVENTS, TIMEOUT);

        for (int i = 0; i < event_cnt; ++i) {
            if (events[i].data.fd == listener_fd) {
                client_fd = accept(listener_fd, (struct sockaddr*)&client_addr, &len);
                ERRNO_WRAPPER(client_fd == -1, "- Couldn't accept connection (accept)", continue_)

                event.events = EPOLLIN | EPOLLONESHOT | EPOLLET;
                event.data.fd = client_fd;
                if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &event) == -1) {
                    perror("- Couldn't accept connection (epoll_ctl)");
                    close(client_fd);
                    continue;
                }

                dump_client(client_addr, thread_num);
            } else {
                client_fd = events[i].data.fd;

                ssize_t filename_size = recv_size(client_fd);
                if (filename_size == -1) { continue; }

                char* filename = malloc(filename_size + 1);
                SIGCLS_WRAPPER(filename == NULL, client_fd, "- Couldn't allocate memory (for user's filename)", continue_)

                ERR_WRAPPER_NOMSG(recv_all(client_fd, filename, filename_size) == -1, cleanup_filename)
                filename[filename_size] = '\0';

                ssize_t size = recv_size(client_fd);
                ERR_WRAPPER_NOMSG(size == -1, cleanup_filename)

                // len(path) + len('/') + len(max int) + len('_') + len(filename) + len('\0')
                char* file = malloc(strlen(path) + 1 + 10 + 1 + strlen(filename) + 1);
                SIGCLS_WRAPPER(file == NULL, client_fd, "- Couldn't allocate memory (for full filename)", cleanup_filename)
                sprintf(file, "%s/%d_%s", path, rand_r(&seed), filename);

                int fd = open(file, O_RDWR | O_CREAT, S_IRWXU);
                SIGCLS_WRAPPER(fd == -1, client_fd, "- Couldn't create file (open)", cleanup_file)

                SIGCLS_WRAPPER(ftruncate(fd, size) == -1, client_fd, "- Couldn't create file (truncate)", cleanup_file)

                char* buf = mmap(NULL, size, PROT_WRITE, MAP_SHARED, fd, 0);
                SIGCLS_WRAPPER(buf == MAP_FAILED, client_fd, "- Couldn't mmap file", cleanup_file)

                ERR_WRAPPER_NOMSG(recv_all(client_fd, buf, size) == -1, cleanup_mmap)

                printf("+ Thread %d: Got file %s\n", thread_num, file);

                send_response(client_fd, 0);

                event.events = EPOLLIN | EPOLLONESHOT | EPOLLET;
                event.data.fd = client_fd;
                if (epoll_ctl(epoll_fd, EPOLL_CTL_MOD, client_fd, &event) == -1) {
                    perror("- Couldn't rearm connection");
                    close(client_fd);
                }

                cleanup_mmap:
                    munmap(buf, size);
                cleanup_file:
                    free(file);
                cleanup_filename:
                    free(filename);
            }
            continue_: ;
        }
    }
}

int main(int argc, char** argv) {
    char const* port = DEFAULT_PORT;
    char const* path = DEFAULT_PATH;

    if (argc >= 2) {
        port = argv[1];
        if (argc == 3) {
            path = argv[2];
        }
    }

    int listener_fd = setup_listener(port);
    ERR_WRAPPER(listener_fd == -1, "- Couldn't initialize server (socket)", error);

    int epoll_fd = epoll_create1(0);
    ERRNO_WRAPPER(epoll_fd == -1, "- Couldn't initialize server (epoll)", cleanup_listener)

    pthread_t thread_pool[NUM_THREADS];
    for (int i = 0; i < NUM_THREADS; ++i) {
        struct thread_data* data = malloc(sizeof(struct thread_data)); // thread is responsible for freeing
        ERR_WRAPPER(data == NULL, "- Couldn't initialize server (threads)", cleanup_epoll)
        data->num = i;
        data->listener_fd = listener_fd;
        data->epoll_fd = epoll_fd;
        data->path = path;

        ERRNO_WRAPPER(pthread_create(&thread_pool[i], NULL, server, data) != 0,
                      "- Couldn't initialize server (threads)", cleanup_epoll) // created threads will die when main() returns
    }

    struct thread_data* data = malloc(sizeof(struct thread_data));
    ERR_WRAPPER(data == NULL, "- Couldn't initialize server (threads)", cleanup_epoll)
    data->num = NUM_THREADS;
    data->listener_fd = listener_fd;
    data->epoll_fd = epoll_fd;
    data->path = path;

    struct epoll_event event;
    event.events = EPOLLIN | EPOLLEXCLUSIVE;
    event.data.fd = listener_fd;
    ERRNO_WRAPPER(epoll_ctl(epoll_fd, EPOLL_CTL_ADD, listener_fd, &event) == -1,
                  "- Couldn't initialize server (epoll_ctl)", cleanup_epoll)

    printf("+ Started on port %s\n", port);

    server(data); // no return

cleanup_epoll:
    close(epoll_fd);
cleanup_listener:
    close(listener_fd);
error:
    return ERROR;
}

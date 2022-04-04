#include <fcntl.h>
#include <netdb.h>
#include <memory.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <unistd.h>

#include "common.h"

int connect_to_server(char const* address, char const* port) {
    int server_fd;
    struct addrinfo hint;
    struct addrinfo* server_info;
    int exit_code;

    memset(&hint, 0, sizeof(hint));
    hint.ai_family = AF_INET;
    hint.ai_socktype = SOCK_STREAM;

    ERR_WRAPPER((exit_code = getaddrinfo(address, port, &hint, &server_info)) != 0, gai_strerror(exit_code), error)

    for(struct addrinfo* p = server_info; p != NULL; p = p->ai_next) {
        server_fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (server_fd == -1) {
            continue;
        }

        if (connect(server_fd, p->ai_addr, p->ai_addrlen) == -1) {
            close(server_fd);
            continue;
        }

        freeaddrinfo(server_info);
        return server_fd;
    }

error:
    freeaddrinfo(server_info);
    return -1;
}

int send_all(int socket, char const* buf, size_t len) {
    size_t total = 0;
    size_t left = len;
    ssize_t n;

    while(total < len) {
        n = send(socket, buf + total, left, 0);
        if (n == -1) {
            break;
        }
        total += n;
        left -= n;
    }

    return (n == -1) ? -1 : 0;
}

int main(int argc, char** argv) {
    ERR_WRAPPER(argc < 3 || argc > 4 || strchr(argv[1], ':') == NULL,
                "Usage: client <address:port> <path_to_file> [name_on_server]", exit)
    char* sep = strchr(argv[1], ':');
    *sep = '\0';
    char const* address = argv[1];
    char const* port = sep + 1;
    char const* file = argv[2];
    char const* name_on_server = (argc == 4) ? argv[3] : file;

    int server_fd = connect_to_server(address, port);
    ERR_WRAPPER(server_fd == -1, "- Couldn't connect", exit)

    int fd = open(file, O_RDONLY);
    ERRNO_WRAPPER(fd == -1, "- Couldn't open file", cleanup_server)

    struct stat stat;
    ERRNO_WRAPPER(fstat(fd, &stat) == -1, "- Couldn't get filesize", cleanup_file)

    char* buf = mmap(NULL, stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    ERRNO_WRAPPER(buf == MAP_FAILED, "- Couldn't mmap file", cleanup_file)

    uint32_t file_size = htonl(stat.st_size);
    size_t name_size_host = strlen(name_on_server);
    uint32_t name_size = htonl(name_size_host);

    ERRNO_WRAPPER(send(server_fd, &name_size, sizeof(name_size), 0) == -1, "- Couldn't send filename", cleanup_mmap)
    ERRNO_WRAPPER(send_all(server_fd, name_on_server, name_size_host) == -1, "- Couldn't send filename", cleanup_mmap)

    ERRNO_WRAPPER(send(server_fd, &file_size, sizeof(file_size), 0) == -1, "- Couldn't send file size", cleanup_mmap)
    ERRNO_WRAPPER(send_all(server_fd, buf, stat.st_size) == -1, "- Couldn't send full file", cleanup_mmap)
    printf("+ Successfully sent full file\n");

    int status = -1;
    ssize_t bytes = recv(server_fd, &status, sizeof(status), 0);
    ERRNO_WRAPPER(bytes == -1, "- Couldn't ask server status", cleanup_mmap)
    ERR_WRAPPER(bytes == 0, "- Lost connection when asking server status", cleanup_mmap)
    if (status != 0) {
        fprintf(stderr, "- Server responded with error\n");
    } else {
        printf("+ Server responded with ok\n");
    }

cleanup_mmap:
    munmap(buf, stat.st_size);
cleanup_file:
    close(fd);
cleanup_server:
    close(server_fd);
exit:
    return ERROR;
}

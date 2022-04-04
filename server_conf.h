#pragma once

#define DEFAULT_PORT "1026"
#define DEFAULT_PATH "/tmp"

#define BACKLOG 150
#define MAX_EVENTS 150
#define TIMEOUT 30000
#define NUM_THREADS 3

struct thread_data {
    int num;
    int listener_fd;
    int epoll_fd;
    char const* path;
};

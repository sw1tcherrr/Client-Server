# Client-Server

_VK internship assignment_

Simple server for receiving files from multiple clients and client for sending files

#### Features

+ pure C11
+ multithreading with `pthreads` for receiving multiple files in parallel
+ the fastest way of IO multiplexing - using `epoll`
+ memory-mapped file IO to omit allocation of buffers
+ comprehensible error messages

#### Usage

Server:
```shell
Server [port] [saving_dir]
```

Default port and directory for files are `1026` and `/tmp` (can be reconfigured) 

Client:

```shell
Client <address:port> <path_to_file> [name_on_server]
```

Default name_on_server is same as the original filename

#### Configuration

The following constants can be set in `server_conf.h` before compilation:

+ `BACKLOG` - queue size for accepting connections, other ones likely to be rejected
+ `MAX_EVENTS` - maximum number of file descriptors returned by `epoll_wait`
+ `TIMEOUT` - `epoll_wait` timeout
+ `NUM_THREADS` - number of additional threads besides main
+ 'DEFAULT_PORT'
+ 'DEFAULT_PATH'

#### Compilation

Via `cmake`

Tested with modern `gcc`, language standard is `C11`

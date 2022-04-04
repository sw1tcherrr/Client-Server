cmake_minimum_required(VERSION 3.7.2) # determined by https://github.com/nlohmann/cmake_min_version/ for compatibility with old ubuntu
project(Client_Server C)

set(CMAKE_C_STANDARD 11)

set(CMAKE_C_FLAGS "-pthread -O2")

add_executable(Client client.c)
add_executable(Server server.c)

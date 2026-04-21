cmake_minimum_required(VERSION 3.16)
project(deadlock_monitor LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Threads REQUIRED)

add_executable(monitor
    src/main.cpp
    src/monitor.cpp
    src/simulation.cpp
)

target_link_libraries(monitor PRIVATE Threads::Threads)
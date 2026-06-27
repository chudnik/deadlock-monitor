cmake_minimum_required(VERSION 3.16)
project(deadlock_monitor LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(modules/monitor)
add_subdirectory(modules/simulation)
add_subdirectory(modules/logger)
add_subdirectory(utils)

add_executable(deadlock_monitor src/main.cpp)

target_link_libraries(deadlock_monitor PRIVATE simulation)
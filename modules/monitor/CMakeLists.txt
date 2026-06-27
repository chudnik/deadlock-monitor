add_library(monitor STATIC
        src/monitor.cpp
)

target_include_directories(monitor PUBLIC include)
target_link_libraries(monitor PUBLIC utils)
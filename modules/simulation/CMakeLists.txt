add_library(simulation STATIC
        src/simulation.cpp
)

target_include_directories(simulation PUBLIC include)

target_link_libraries(simulation PUBLIC monitor)
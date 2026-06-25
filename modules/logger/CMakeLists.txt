add_library(logger STATIC
        src/logger.cpp
        src/formatter.cpp
)

target_include_directories(logger PUBLIC include)
target_link_libraries(logger PUBLIC monitor)
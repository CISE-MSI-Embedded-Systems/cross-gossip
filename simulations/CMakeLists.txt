cmake_minimum_required(VERSION 3.16)

project(simulation C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

add_executable(simulation
    main_linux.c
    ../libraries/gossip/distributed.c
    ../libraries/gossip/gossip.c
)

target_include_directories(simulation
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../libraries
)

target_link_libraries(simulation
    PRIVATE
        rt
)

target_compile_options(simulation
    PRIVATE
        -Wall
        -Wextra
        -Wpedantic
)

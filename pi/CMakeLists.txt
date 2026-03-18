#cmake_minimum_required(VERSION 3.16)
#
#project(pi C)
#
#set(CMAKE_C_STANDARD 11)
#set(CMAKE_C_STANDARD_REQUIRED ON)
#set(CMAKE_C_EXTENSIONS OFF)
#
#add_executable(pi
#    main_pi.c
#    ../libraries/gossip/distributed.c
#    ../libraries/gossip/gossip.c
#)
#
#target_include_directories(pi
#     ${CMAKE_CURRENT_SOURCE_DIR}/../libraries
# )

# target_compile_options(simulation
#     PRIVATE
#         -Wall
#         -Wextra
#         -Wpedantic
# )

cmake_minimum_required(VERSION 3.16)

# Set the compiling mode flags
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -DDEBUG")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG -s")

# Set the project name
project(pi C)

# Include the current directory for header files
include_directories(../libraries/gossip
../libraries/sqlite)

# Add the executable
add_executable(pi
main_pi.c
../libraries/gossip/distributed.c
../libraries/gossip/gossip.c
../libraries/sqlite/sqlite3.c)

# Add any libraries
# target_link_libraries(cosine_series m)

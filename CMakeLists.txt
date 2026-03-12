cmake_minimum_required(VERSION 3.20)

project(cross_gossip C)

set(CMAKE_C_STANDARD 11)

add_subdirectory(libraries)
add_subdirectory(simulations)

enable_testing()

add_subdirectory(tests)

add_library(gossip
    gossip/gossip.c
    gossip/distributed.c
)

target_include_directories(gossip
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

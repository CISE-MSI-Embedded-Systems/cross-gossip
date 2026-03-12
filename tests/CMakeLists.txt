add_executable(cluster_alive_test
    cluster_alive_test.c
)

add_test(
    NAME cluster_alive
    COMMAND cluster_alive_test
)

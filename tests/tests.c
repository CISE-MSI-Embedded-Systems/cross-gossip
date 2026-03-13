#include "gossip/gossip.h"
#include "gossip/platform.h"

#include <assert.h>
#include <mqueue.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>


#define RESET "\e[0m"
#define BLACK "\e[30m"
#define RED "\e[31m"
#define YELLOW "\e[33m"
#define GREEN "\e[34m"
#define MAGENTA "\e[35m"
#define CYAN "\e[36m"

#define MQ_NAME "/" __FILE_NAME__

void test_printf(const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);

  printf("[TEST] ");
  vprintf(fmt, args);
  printf("\n");

  va_end(args);
}

#define NODES 3
pid_t nodes_pid[NODES];

void nodes_kill_and_wait(void) {
  for (int i = 0; i < NODES; i += 1) {
    kill(nodes_pid[i], SIGTERM);
  }

  for (int i = 0; i < NODES; i += 1) {
    waitpid(nodes_pid[i], NULL, 0);
  }

  test_printf("killed %d nodes", NODES);
}

void sigint_handler(int sig) {
  (void)sig;
  nodes_kill_and_wait();
  exit(1);
}

void nodes_spawn(void) {
  char node_id_buf[16] = {0};
  for (int i = 0; i < NODES; i++) {
    pid_t pid = fork();
    if (pid == -1) {
      perror("fork failed");
      exit(1);
    }
    if (pid == 0) {
      // child process
      char node_id_buf[16];
      snprintf(node_id_buf, sizeof(node_id_buf), "%d", i + 1);
      char *argv[] = {"linux_node", node_id_buf, MQ_NAME, NULL};
      execvp("./simulations/linux_node", argv);
      perror("execvp failed");
      exit(1);
    } else {
      nodes_pid[i] = pid;
    }
  }

  struct sigaction sa = {0};
  sa.sa_handler = sigint_handler;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;
  sigaction(SIGINT, &sa, NULL);

  test_printf("spawned %d nodes", NODES);
}

void test_nearby_heartbeats(mqd_t queue) {
  uint8_t nodes_alive_bitset_mask = 0;
  for (int i = 0; i < NODES; i += 1) {
    nodes_alive_bitset_mask |= (1 << i);
  }

  uint8_t nodes_done_bitset = {0};
  uint8_t nodes_alive_bitset[NODES] = {0};
  static_assert(NODES <= sizeof(nodes_alive_bitset[0]) * 8, "change bit set size");
  uint8_t nodes_that_found_all = 0;

  for (int i = 0; i < NODES; i++) {
    nodes_alive_bitset[i] |= (1 << i);
  }

  TracedGossipMsg traced = {0};

  while (nodes_that_found_all < NODES) {
    int recv_len = mq_receive(queue, (char *)&traced, sizeof(traced), NULL);
    assert(recv_len == sizeof(traced));

    GossipMsg msg = traced.msg;
    uint32_t rx_id = traced.node_id;
    uint32_t tx_id = msg.id.node_id;

    uint32_t rx_idx = traced.node_id - 1;
    assert(rx_idx < NODES);

    uint32_t tx_idx = msg.id.node_id - 1;
    assert(tx_idx < NODES);

    if (msg.type == GOSSIP_TYPE_HEARTBEAT) {
      if (rx_id == tx_id) {
        continue;
      }

      if (nodes_done_bitset & (1 << rx_idx)) {
        continue;
      }

      test_printf("%d got heartbeat from %d", rx_id, tx_id);

      nodes_alive_bitset[rx_idx] |= (1 << tx_idx);
      if (nodes_alive_bitset[rx_idx] == nodes_alive_bitset_mask) {
        test_printf("%d found all nearby nodes", rx_id, tx_id);
        nodes_done_bitset |= (1 << rx_idx);
        nodes_that_found_all += 1;
      }
    }
  }
}


int main() {
  struct mq_attr attr;
  attr.mq_flags = 0;
  attr.mq_maxmsg = 10;
  attr.mq_msgsize = sizeof(TracedGossipMsg);
  attr.mq_curmsgs = 0;

  mq_unlink(MQ_NAME);
  mqd_t queue = mq_open(MQ_NAME, O_CREAT | O_RDONLY, 0664, &attr);
  if (queue == -1) {
    perror("mq_open");
    exit(1);
  }

  uint32_t test_idx = 0;

  nodes_spawn();
  test_nearby_heartbeats(queue);
  nodes_kill_and_wait();
  test_printf(YELLOW "#%d - nearby heartbeats" RESET, ++test_idx);

  mq_close(queue);
  return 0;
}

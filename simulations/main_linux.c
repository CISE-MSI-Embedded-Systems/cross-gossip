#define _GNU_SOURCE
#define _DEFAULT_SOURCE

#include "gossip/distributed.h"
#include "gossip/gossip.h"
#include "gossip/platform.h"

#include <arpa/inet.h>
#include <assert.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <mqueue.h>
#include <fcntl.h>

#include <sys/select.h>
#include <sys/signal.h>
#include <sys/socket.h>
#include <sys/types.h>
//
#define MCAST_PORT 5000
#define MCAST_ADDR "239.192.0.1"

// TODO: impl
StorageRecord *platform_storage_find(GossipMsgId msg_id) {
  (void)msg_id;
  return NULL;
}
// TODO: impl
void platform_storage_store(const StorageRecord *record) { (void)record; }

#define PLATFORM_MAX_TIMERS 10
timer_t timers[PLATFORM_MAX_TIMERS];
bool timers_used[PLATFORM_MAX_TIMERS];
TimerCallback callbacks[PLATFORM_MAX_TIMERS];

static void linux_timer_trampoline(union sigval sv) {
  int idx = sv.sival_int;

  if (idx >= 0 && idx < PLATFORM_MAX_TIMERS && timers_used[idx]) {
    callbacks[idx]();
  }
}

static void fill_timespec_ms(struct itimerspec *its, uint32_t ms) {
  its->it_value.tv_sec = ms / 1000;
  its->it_value.tv_nsec = (ms % 1000) * 1000000;

  its->it_interval = its->it_value; // periodic
}

int platform_timer_start(uint32_t interval_ms, TimerCallback cb) {
  int idx = -1;

  for (int i = 0; i < PLATFORM_MAX_TIMERS; i++) {
    if (!timers_used[i]) {
      timers_used[i] = true;
      callbacks[i] = cb;
      idx = i;
      break;
    }
  }

  if (idx == -1)
    return -1;

  struct sigevent sev = {0};
  struct itimerspec its = {0};

  sev.sigev_notify = SIGEV_THREAD;
  sev.sigev_notify_function = linux_timer_trampoline;
  sev.sigev_value.sival_int = idx;

  if (timer_create(CLOCK_MONOTONIC, &sev, &timers[idx]) != 0) {
    timers_used[idx] = false;
    return -1;
  }

  fill_timespec_ms(&its, interval_ms);

  if (timer_settime(timers[idx], 0, &its, NULL) != 0) {
    timer_delete(timers[idx]);
    timers_used[idx] = false;
    return -1;
  }

  return idx;
}

void platform_timer_stop(int idx) {
  if (idx < 0 || idx >= PLATFORM_MAX_TIMERS)
    return;

  if (!timers_used[idx])
    return;

  struct itimerspec its = {0};

  // Disarm timer (not strictly required before delete, but clean)
  timer_settime(timers[idx], 0, &its, NULL);

  timer_delete(timers[idx]);

  timers_used[idx] = false;
  callbacks[idx] = NULL;
}

// TODO: better data faking
static SensorData fake_sensor_data = {
    .current_amps = 0,
    .voltage_volts = 0,
};

void platform_sensor_read(SensorData *sensor_data) {
  *sensor_data = fake_sensor_data;

  sensor_data->current_amps += 1;
  sensor_data->voltage_volts += 1;
}

static int tx_fd = -1;
void platform_radio_tx(const uint8_t *data, uint8_t len) {
  assert(tx_fd >= 0);
  struct sockaddr_in addr = {0};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(MCAST_PORT);
  addr.sin_addr.s_addr = inet_addr(MCAST_ADDR);
  bool sent_all = sendto(tx_fd, data, len, 0, (struct sockaddr *)&addr,
                         sizeof(addr)) == (ssize_t)len;
  assert(sent_all);
  return;
}

static uint32_t node_id;
uint32_t platform_node_id(void) { return node_id; }

void platform_log(const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);

  printf("[NODE %d] ", node_id);
  vprintf(fmt, args);
  printf("\n");

  va_end(args);
}

static char *trace_msg_mq_name;
void platform_trace_msg(const GossipMsg *msg) {
  TracedGossipMsg traced = {*msg, node_id};
  static mqd_t trace_msg_mq = -1;
  if (trace_msg_mq == -1) {
    trace_msg_mq = mq_open(trace_msg_mq_name, O_WRONLY);
  }

  int mq_res = mq_send(trace_msg_mq, (char *)&traced, sizeof(TracedGossipMsg), 0);
  if (mq_res == -1) {
    perror("mq_res");
    exit(1);
  }
}

int main(int argc, char *argv[]) {
  if (argc < 3) {
    fprintf(stderr, "usage: %s <node_id> <mq_name>\n", argv[0]);
    return 1;
  }

  node_id = (uint32_t)atoi(argv[1]);
  trace_msg_mq_name = argv[2];

  // RX socket
  int rx_fd = socket(AF_INET, SOCK_DGRAM, 0);
  if (rx_fd < 0)
    return 1;

  int reuse = 1;
  setsockopt(rx_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
#ifdef SO_REUSEPORT
  setsockopt(rx_fd, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse));
#endif

  struct sockaddr_in addr = {0};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(MCAST_PORT);
  addr.sin_addr.s_addr = htonl(INADDR_ANY);

  if (bind(rx_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    close(rx_fd);
    rx_fd = -1;
    return 1;
  }

  struct ip_mreq mreq = {0};
  mreq.imr_multiaddr.s_addr = inet_addr(MCAST_ADDR);
  mreq.imr_interface.s_addr = htonl(INADDR_ANY);
  setsockopt(rx_fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq));

  // TX socket
  tx_fd = socket(AF_INET, SOCK_DGRAM, 0);
  if (tx_fd < 1) {
    close(rx_fd);
    rx_fd = -1;
    return 1;
  }

  unsigned char ttl = 1;
  setsockopt(tx_fd, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl));
  unsigned char loop = 1;
  setsockopt(tx_fd, IPPROTO_IP, IP_MULTICAST_LOOP, &loop, sizeof(loop));

  distributed_on_init();

  fd_set readfds;
  while (1) {
    FD_ZERO(&readfds);
    if (rx_fd >= 0)
      FD_SET(rx_fd, &readfds);
    int ret = select(rx_fd + 1, &readfds, NULL, NULL, NULL);
    if (ret > 0 && FD_ISSET(rx_fd, &readfds)) {
      uint8_t buf[1024];
      ssize_t n = read(rx_fd, buf, sizeof(buf));
      if (n > 0)
        gossip_on_radio_rx(buf, n);
    }
  }

  if (rx_fd >= 0)
    close(rx_fd);
  if (tx_fd >= 0)
    close(tx_fd);
  rx_fd = tx_fd = -1;
}

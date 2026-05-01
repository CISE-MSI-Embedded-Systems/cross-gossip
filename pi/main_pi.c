#define _GNU_SOURCE
#define _DEFAULT_SOURCE

#include "../libraries/gossip/distributed.h"
#include "../libraries/gossip/gossip.h"
#include "../libraries/gossip/platform.h"
#include "../libraries/sqlite/sqlite3.h"

// LIBMODBUS Include
#include <modbus.h>
#include <errno.h>

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
#include <time.h>

#include <sys/select.h>
#include <sys/signal.h>
#include <sys/socket.h>
#include <sys/types.h>
//
#define MCAST_PORT 5000
#define MCAST_ADDR "239.192.0.1"
sqlite3 *db;
int status;
// Utilize sqlite for saving and loading storage records on this platform
// ... https://sqlite.org/cintro.html

// NOTE: If duplicate node_id's exist, current implementation shows the oldest entry.
// SCHEMA: node_id (int) | seq (int) | voltage (float) | current (float) | Timestamp (TIMESTAMP)
StorageRecord *platform_storage_find(GossipMsgId msg_id) {

  //   typedef struct {
  //   GossipMsgId msg_id;
  //   GossipMsgData sensor_data;
  // } StorageRecord;
  
  uint32_t id = msg_id.node_id;
  const char *find = "SELECT * FROM records WHERE node_id = ?;";
  sqlite3_stmt *stmt;
  status = sqlite3_prepare(db, find, -1, &stmt, NULL);
  if(status!=0){
    printf("Failed to prepare statement: %s\n", sqlite3_errmsg(db));
  }

  sqlite3_bind_int(stmt, 1, id);
  status = sqlite3_step(stmt);

  uint32_t seq;
  uint32_t voltage;
  uint32_t current;
  if(status == SQLITE_ROW){
    seq = sqlite3_column_int(stmt, 1);
    voltage = sqlite3_column_int(stmt, 2);
    current = sqlite3_column_int(stmt, 3);
  }
  else if(status == SQLITE_DONE){
    printf("No matching record found.\n");
  }
  else{
    printf("Failed to execute statement: %s\n", sqlite3_errmsg(db));
  }
  
  sqlite3_finalize(stmt);
  printf("Node ID: %d\n", id);
  printf("Seq Num: %d\n", seq);
  printf("Node Voltage (V): %d\n", voltage);
  printf("Node Current (A): %d\n", current);

  // Return StorageRecord with gathered data, not NULL
  GossipMsgData sensor_data;
  sensor_data.voltage_volts = voltage;
  sensor_data.current_amps = current;

  StorageRecord *record = malloc(sizeof(StorageRecord));
  record->msg_id = msg_id;
  record->sensor_data = sensor_data;
  return record;
}

void platform_storage_store(const StorageRecord *record) {
  StorageRecord rec = *record;
  uint32_t msg_id = rec.msg_id.node_id;
  uint32_t seq = rec.msg_id.seq;
  GossipMsgData sensor_data = rec.sensor_data;
  uint32_t voltage_volts = sensor_data.voltage_volts;
  uint32_t current_amps = sensor_data.current_amps;

  const char *insert = "INSERT INTO records (node_id, seq, voltage, current) VALUES (?, ?, ?, ?)";
  sqlite3_stmt *stmt;
  status = sqlite3_prepare(db, insert, -1, &stmt, NULL);
  if(status!=0){
    printf("Failed to prepare statement: %s\n", sqlite3_errmsg(db));
  }

  sqlite3_bind_int(stmt, 1, msg_id);
  sqlite3_bind_int(stmt, 2, seq);
  sqlite3_bind_double(stmt, 3, voltage_volts);
  sqlite3_bind_double(stmt, 4, current_amps);
  
  status = sqlite3_step(stmt);
  if(status!=SQLITE_DONE){
    printf("Failed to execute statement: %s\n", sqlite3_errmsg(db));
  }
  sqlite3_finalize(stmt);
}

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

static SensorData fake_sensor_data = {
  .current_amps = 0,
  .voltage_volts = 0,
};

// Gets register data from smart meter simulator
// NOTE: Simulator holds randomized data for now
static SensorData platform_sensor_read_simulator()
{
  const char MODBUS_TCP_ADDR[] = "100.112.125.111"; // Addres of machine hosting simulator server
  const int PORT = 1502; // Match with port used in simulator server
   
  modbus_t *mb;
  uint16_t tab_reg[64];
  mb = modbus_new_tcp(MODBUS_TCP_ADDR, PORT);
  if(modbus_connect(mb) == -1)
  {
     fprintf(stderr, "Connection failed: %s\n", modbus_strerror(errno));
     modbus_free(mb);
     return fake_sensor_data;
  }

  int register_data = modbus_read_registers(mb, 0, 60, tab_reg);
  printf("Registers read: %d\n", register_data);

  
  //TODO: Take data out from the register into SensorData var
  uint16_t avg_voltage_1;
  uint16_t avg_voltage_2;
  uint16_t avg_current_1;
  uint16_t avg_current_2;
  for(int i = 0; i < register_data; i++)
  {
    // Register 15 and 16 (index 14 and 15) in simulator holds avg voltage
    // Register 17 and 18 (index 16 and 17) in simulator holds avg current
    switch(i)
    {
      case 14:
        avg_voltage_1 = tab_reg[i];
        printf("1st data segment of avg voltage = %d\n", tab_reg[i]);
        break;
      case 15:
        avg_voltage_2 = tab_reg[i];
        printf("2nd data segment of avg goltage = %d\n", tab_reg[i]);
        break;
       case 16:
       	avg_current_1 = tab_reg[i];
        printf("1st data segment of avg current = %d\n", tab_reg[i]);
        break;
       case 17:
       	avg_current_2 = tab_reg[i];
       	printf("2nd data segment of avg current = %d\n", tab_reg[i]);
       	break;
    }
    // printf("reg[%d] = %d (0x%X)\n", i, tab_reg[i], tab_reg[i]);
  }
  printf("Read all register data!\n\n");

  modbus_close(mb);
  modbus_free(mb);
  
  // Smart Meter and previous implementations use ABCD format
  // ABCD = Register Order -> (1,2) -> Normal Byte Order
  float f1 = modbus_get_float_abcd(&tab_reg[14]);
  float f2 = modbus_get_float_abcd(&tab_reg[16]);
  
  SensorData sim_sensor_data = {
    .current_amps = f2,
    .voltage_volts = f1,
  };

  return sim_sensor_data;
}


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

  /* TESTING STORAGE METHODS */
  
  /* srand(time(NULL));
  const char *filename = "storage.db"; 
  status = sqlite3_open(filename, &db);
  if(status != 0){
    printf("Failed DB init: %s\n", sqlite3_errmsg(db));
  }

  const char *create_table = "CREATE TABLE IF NOT EXISTS records (node_id INTEGER, seq INTEGER, voltage REAL, current REAL, timestamp DATETIME DEFAULT CURRENT_TIMESTAMP);";
  status = sqlite3_exec(db, create_table, 0, 0, 0);
  if(status!=0){
    printf("Table creation failed: %s\n", sqlite3_errmsg(db));
  }

  node_id = (uint32_t)atoi(argv[1]);
  trace_msg_mq_name = argv[2];

  printf("Beginning insertion test...\n");
  // Dummy storage record for testing
  GossipMsgId dummy_id;
  dummy_id.node_id = node_id;
  dummy_id.seq = 1; // What exactly is seq?
  
  StorageRecord dummy_data;
  dummy_data.msg_id = dummy_id;
  dummy_data.sensor_data.voltage_volts = rand()%10;
  dummy_data.sensor_data.current_amps = rand()%10;

  const StorageRecord *dummy_rec = &dummy_data;
  platform_storage_store(dummy_rec);

  printf("Beginning selection test...\n");
  platform_storage_find(dummy_data.msg_id);
  sqlite3_close(db); */
  
  /* TESTING STORAGE METHODS */

  SensorData sim_sensor_data = platform_sensor_read_simulator();
  printf("Voltage (V) = %f\n", sim_sensor_data.voltage_volts);
  printf("Current (A) = %f\n", sim_sensor_data.current_amps);

  /* // RX socket
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
  */
}


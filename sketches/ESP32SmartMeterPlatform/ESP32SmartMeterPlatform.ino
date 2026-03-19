extern "C" {
#include "gossip.h"
#include "distributed.h"
#include "platform.h"
}

#include <SPI.h>
#include <RH_RF95.h>
#include <WiFi.h>
#include <Ticker.h>

// CHANGE ME FOR EVERY FLASH
#define NODE_ID 1


#define RFM95_CS   5
#define RFM95_RST  14
#define RFM95_INT  26
#define RF95_FREQ  915.0

RH_RF95 rf95(RFM95_CS, RFM95_INT);

/* ---- platform api ---- */
StorageRecord *platform_storage_find(GossipMsgId msg_id) {
  // TODO:
  return NULL;
}

void platform_storage_store(const StorageRecord *sensor_data) {
  // TODO:
  return;
}

#define PLATFORM_MAX_TIMERS 10

Ticker timers[PLATFORM_MAX_TIMERS];
bool timers_used[PLATFORM_MAX_TIMERS];
TimerCallback callbacks[PLATFORM_MAX_TIMERS];
volatile bool timer_flags[PLATFORM_MAX_TIMERS];

static void ticker_trampoline(int idx) {
  if (idx >= 0 && idx < PLATFORM_MAX_TIMERS && timers_used[idx]) {
    timer_flags[idx] = true;   // ONLY set flag
  }
}

int platform_timer_start(uint32_t interval_ms, TimerCallback cb) {
  int idx = -1;

  for (int i = 0; i < PLATFORM_MAX_TIMERS; i++) {
    if (!timers_used[i]) {
      timers_used[i] = true;
      callbacks[i] = cb;
      timer_flags[i] = false;
      idx = i;
      break;
    }
  }

  if (idx == -1)
    return -1;

  timers[idx].attach_ms(
    interval_ms,
    ticker_trampoline,
    idx   // argument passed to trampoline
  );

  return idx;
}

void platform_timer_stop(int idx) {
  if (idx < 0 || idx >= PLATFORM_MAX_TIMERS)
    return;

  if (!timers_used[idx])
    return;

  timers[idx].detach();

  timers_used[idx] = false;
  callbacks[idx] = NULL;
}

void platform_sensor_read(SensorData *sensor_data) {
  // don't do this
  *sensor_data = (SensorData){0};
}

void platform_radio_tx(const uint8_t *data, uint8_t len) {
  rf95.send(data, (uint8_t)len);
  rf95.waitPacketSent();
}

void platform_log(const char *fmt, ...) {
  char buf[128]; // adjust size as needed

  va_list args;
  va_start(args, fmt);
  vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);

  Serial.println(buf);
}

void platform_trace_msg(const GossipMsg *msg) {
  // TODO: tracing with multiple embedded devices isn't as easy
}

uint32_t platform_node_id(void) {
  // we have to have something better than this
  return NODE_ID;
}

void setup() {
  Serial.begin(115200);
  delay(2000);
  WiFi.begin("CLARO-9726", "AdsPMLxh3D");
  while (WiFi.status() != WL_CONNECTED) delay(200);

  pinMode(RFM95_RST, OUTPUT);
  digitalWrite(RFM95_RST, HIGH);
  delay(5);
  digitalWrite(RFM95_RST, LOW);
  delay(5);
  digitalWrite(RFM95_RST, HIGH);
  delay(5);

  if (!rf95.init()) {
    Serial.println("RF95 init failed — check wiring");
    while (1);
  }
  rf95.setFrequency(RF95_FREQ);
  rf95.setTxPower(20, false);

  Serial.println("going to init");
  distributed_on_init();
}

void loop() {
  for (int i = 0; i < PLATFORM_MAX_TIMERS; i++) {
    if (timers_used[i] && timer_flags[i]) {
      timer_flags[i] = false;

      if (callbacks[i]) {
        callbacks[i]();
      }
    }
  }

  if (rf95.available()) {
    uint8_t buf[sizeof(GossipMsg)];
    uint8_t len = sizeof(buf);
    
    if (rf95.recv(buf, &len)) {
      if (len != sizeof(GossipMsg)) {
        Serial.print("bad len: ");
        Serial.println(len);
      }
      gossip_on_radio_rx(buf, len);
    }
  }
}
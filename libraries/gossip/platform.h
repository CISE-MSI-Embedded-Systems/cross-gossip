#pragma once
#include "gossip.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
  float current_amps, voltage_volts;
} SensorData;

typedef struct {
  GossipMsgId msg_id;
  GossipMsgData sensor_data;
} StorageRecord;

typedef void (*TimerCallback)(void);

/* ---- platform api ---- */
StorageRecord *platform_storage_find(GossipMsgId msg_id);

void platform_storage_store(const StorageRecord *sensor_data);

int platform_timer_start(uint32_t interval_ms, TimerCallback cb);

void platform_timer_stop(int idx);

void platform_sensor_read(SensorData *sensor_data);

void platform_radio_tx(const uint8_t *data, uint8_t len);

void platform_log(const char *fmt, ...);

uint32_t platform_node_id(void);

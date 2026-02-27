#pragma once
#include <stddef.h>
#include <stdint.h>

/* ---- Gossip message ---- */
typedef enum {
  GOSSIP_TYPE_DEFAULT = 0,
  GOSSIP_TYPE_HEARTBEAT = 1,
  GOSSIP_TYPE_DATA = 2,
} GossipMsgType;

typedef struct {
  uint32_t voltage_volts;
  uint32_t current_amps;
} GossipMsgData;

typedef struct {
  uint32_t node_id, seq;
} GossipMsgId;

typedef struct {
  GossipMsgId id;
  union {
    GossipMsgData data;
  } as;
  GossipMsgType type;
} GossipMsg;

/* ---- gossip callbacks ---- */
void gossip_on_radio_rx(const uint8_t *data, uint8_t len);

/* ---- gossip api ---- */
void gossip_msg_tx(const GossipMsg *msg);

#include "distributed.h"
#include "gossip.h"
#include "platform.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

static uint32_t seq = 0;
static uint32_t node_id = 0;

static GossipMsgId next_msg_id() {
  return (GossipMsgId){
    .node_id = node_id,
    .seq = seq++,
  };
}

/* ---- distributed callbacks ---- */
void distributed_on_msg_rx(const GossipMsg *msg) {
  platform_log("distributed_on_msg_rx");
  (void)msg;
  if (node_id == 0) {
    platform_log("WARN: ignoring node_id == 0");
  }

  if (msg->id.node_id == node_id) {
    platform_log("WARN: same node_id");
    return;
  }

  switch (msg->type) {
  case GOSSIP_TYPE_HEARTBEAT: {
    platform_log("recieved: heartbeat");
    break;
  }
  case GOSSIP_TYPE_DATA: {
    GossipMsgData data = msg->as.data;
    platform_log("recieved: data");
    platform_log("node_id = %d, seq = %d", msg->id.node_id, msg->id.seq);
    if (platform_storage_find(msg->id) == NULL) {
      StorageRecord record = {
        .msg_id = msg->id,
        .sensor_data = data,
      };
      platform_storage_store(&record);
    }
    break;
  }
  case GOSSIP_TYPE_DEFAULT: {
    platform_log("WARN: recieved default type");
    break;
  }
  default: {
    platform_log("WARN: recieved unexpected type");
    break;
  }
  }
}

static void broadcast_sensor_data(void) {
  platform_log("broadcast_sensor_data");
  SensorData sensor_data;
  platform_sensor_read(&sensor_data);
  GossipMsg msg = {0};
  msg.id = next_msg_id();
  msg.type = GOSSIP_TYPE_DATA;
  msg.as.data = (GossipMsgData){
      .current_amps = sensor_data.current_amps,
      .voltage_volts = sensor_data.voltage_volts,
  };
  gossip_msg_tx(&msg);
}

static void heartbeat(void) {
  platform_log("heartbeat");
  GossipMsg msg = {0};
  msg.id = next_msg_id();
  msg.type = GOSSIP_TYPE_HEARTBEAT;
  gossip_msg_tx(&msg);
}

void distributed_on_init(void) {
  node_id = platform_node_id();
  assert(node_id != 0 && "can't have node_id == 0");
  platform_log("after assert");
  platform_timer_start(1000, broadcast_sensor_data);
  platform_timer_start(5000, heartbeat);
  platform_log("after timer setup");
}

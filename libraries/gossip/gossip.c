#include "gossip.h"
#include "distributed.h"
#include "platform.h"

#include <stdint.h>

/* ---- gossip callbacks ---- */
void gossip_on_radio_rx(const uint8_t *data, uint8_t len) {
  if (len != sizeof(GossipMsg)) {
    return;
  }

  const GossipMsg *msg = (const GossipMsg *)data;
  platform_trace_msg(msg);

  distributed_on_msg_rx((GossipMsg *)msg);
}

void gossip_msg_tx(const GossipMsg *msg) {
  platform_radio_tx((uint8_t*)msg, (uint8_t)sizeof(GossipMsg));
}

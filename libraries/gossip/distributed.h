#pragma once
#include <stddef.h>
#include <stdint.h>
#include "gossip.h"

/* ---- distributed callbacks ---- */
void distributed_on_msg_rx(const GossipMsg *msg);
void distributed_on_init(void);

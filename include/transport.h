#pragma once
#include <stdbool.h>
#include <stdint.h>
#define CLOCK_TIMEOUT_US 2000000u
typedef struct {bool running; unsigned source,phase,division,count; uint64_t last_clock,next_tick; bool clock_seen;} transport_t;
/* Returns accepted realtime status, zero for ignored. Source 1=DIN, 2=USB. */
uint8_t transport_receive(transport_t *t,unsigned source,uint8_t byte,uint64_t now);
bool transport_timeout(transport_t *t,uint64_t now,unsigned period);
bool transport_step_due(transport_t *t);

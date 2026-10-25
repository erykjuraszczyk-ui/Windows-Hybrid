#ifndef HYBRID_INTERRUPTS_H
#define HYBRID_INTERRUPTS_H
#include <stdbool.h>
#include <stdint.h>
bool interrupts_init(void);
bool interrupts_self_test(void);
void interrupts_enable(void);
void interrupts_disable(void);
uint64_t interrupts_ticks(void);
void interrupts_wait_for_ticks(uint64_t target);
#endif

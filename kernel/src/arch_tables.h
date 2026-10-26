#ifndef HYBRID_ARCH_TABLES_H
#define HYBRID_ARCH_TABLES_H
#include <stdbool.h>
#include <stdint.h>

#define HYBRID_KERNEL_CODE_SELECTOR UINT16_C(0x08)
#define HYBRID_KERNEL_DATA_SELECTOR UINT16_C(0x10)
#define HYBRID_TSS_SELECTOR         UINT16_C(0x18)

bool arch_tables_init(void);
bool arch_tables_self_test(void);

#endif

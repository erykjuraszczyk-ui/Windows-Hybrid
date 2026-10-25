#ifndef HYBRID_CONSOLE_H
#define HYBRID_CONSOLE_H
#include <stdint.h>
void console_init(void);
void serial_write(const char *text);
void serial_write_hex64(uint64_t value);
#endif

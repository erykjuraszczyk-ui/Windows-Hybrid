#include "console.h"
#include <stdint.h>
static inline void io_out8(uint16_t port, uint8_t value) {
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}
static inline uint8_t io_in8(uint16_t port) {
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}
void console_init(void) {
    const uint16_t base = 0x3F8;
    io_out8((uint16_t)(base + 1), 0x00);
    io_out8((uint16_t)(base + 3), 0x80);
    io_out8((uint16_t)(base + 0), 0x03);
    io_out8((uint16_t)(base + 1), 0x00);
    io_out8((uint16_t)(base + 3), 0x03);
    io_out8((uint16_t)(base + 2), 0xC7);
    io_out8((uint16_t)(base + 4), 0x0B);
}
static void serial_putc(char value) {
    for (uint32_t spin = 0; spin < 1000000U; ++spin) {
        if ((io_in8(0x3FDU) & 0x20U) != 0U) {
            io_out8(0x3F8U, (uint8_t)value);
            return;
        }
    }
}
void serial_write(const char *text) {
    if (text == 0) return;
    while (*text != '\0') {
        if (*text == '\n') serial_putc('\r');
        serial_putc(*text++);
    }
}
void serial_write_hex64(uint64_t value) {
    static const char digits[] = "0123456789ABCDEF";
    serial_write("0x");
    for (int shift = 60; shift >= 0; shift -= 4)
        serial_putc(digits[(value >> (unsigned)shift) & 0x0FU]);
}

#include "interrupts.h"
#include "console.h"
#include <stddef.h>
#include <stdint.h>
#define IDT_ENTRIES 256U
#define TIMER_VECTOR 32U
#define PIT_FREQUENCY 100U
#define PIT_INPUT_HZ 1193182U
struct idt_gate {
    uint16_t offset_low; uint16_t selector; uint8_t ist; uint8_t attributes;
    uint16_t offset_middle; uint32_t offset_high; uint32_t reserved;
} __attribute__((packed));
struct idt_register { uint16_t limit; uint64_t base; } __attribute__((packed));
struct interrupt_frame {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t vector, error_code, rip, cs, rflags;
};
extern void (*isr_stub_table[])(void);
extern void isr_default_stub(void);
static struct idt_gate idt[IDT_ENTRIES] __attribute__((aligned(16)));
static volatile uint64_t timer_tick_count;
static volatile bool timer_first_tick_reported;
static volatile bool breakpoint_test_armed;
static volatile bool breakpoint_test_passed;
static inline void io_out8(uint16_t port, uint8_t value) {
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port) : "memory");
}
static inline void io_wait(void) {
    /* Legacy PIC/PIT devices may require an I/O-cycle delay between writes. */
    __asm__ volatile ("outb %%al, $0x80" : : "a"(0U) : "memory");
}
static void pic_write(uint16_t port, uint8_t value) {
    io_out8(port, value);
    io_wait();
}
static uint16_t current_code_selector(void) {
    uint16_t selector;
    __asm__ volatile ("mov %%cs, %0" : "=r"(selector));
    return selector;
}
static void idt_set_gate(uint8_t vector, uintptr_t handler, uint16_t selector) {
    idt[vector].offset_low = (uint16_t)(handler & 0xFFFFU);
    idt[vector].selector = selector; idt[vector].ist = 0U;
    idt[vector].attributes = 0x8EU;
    idt[vector].offset_middle = (uint16_t)((handler >> 16U) & 0xFFFFU);
    idt[vector].offset_high = (uint32_t)((handler >> 32U) & 0xFFFFFFFFU);
    idt[vector].reserved = 0U;
}
static void pic_remap_and_mask(void) {
    pic_write(0x20U, 0x11U); pic_write(0xA0U, 0x11U);
    pic_write(0x21U, 0x20U); pic_write(0xA1U, 0x28U);
    pic_write(0x21U, 0x04U); pic_write(0xA1U, 0x02U);
    pic_write(0x21U, 0x01U); pic_write(0xA1U, 0x01U);
    /* Unmask only master IRQ0; keep every other legacy IRQ masked. */
    pic_write(0x21U, 0xFEU); pic_write(0xA1U, 0xFFU);
}
static void pit_init(void) {
    const uint16_t divisor = (uint16_t)(PIT_INPUT_HZ / PIT_FREQUENCY);
    io_out8(0x43U, 0x36U);
    io_wait();
    io_out8(0x40U, (uint8_t)(divisor & 0xFFU));
    io_wait();
    io_out8(0x40U, (uint8_t)((divisor >> 8U) & 0xFFU));
    io_wait();
}
bool interrupts_init(void) {
    __asm__ volatile ("cli");
    const uint16_t selector = current_code_selector();
    const uintptr_t default_handler = (uintptr_t)isr_default_stub;
    for (uint32_t vector = 0; vector < IDT_ENTRIES; ++vector)
        idt_set_gate((uint8_t)vector, default_handler, selector);
    for (uint32_t vector = 0; vector < 32U; ++vector)
        idt_set_gate((uint8_t)vector, (uintptr_t)isr_stub_table[vector], selector);
    idt_set_gate((uint8_t)TIMER_VECTOR, (uintptr_t)isr_stub_table[TIMER_VECTOR], selector);
    const struct idt_register idtr = {
        .limit = (uint16_t)(sizeof(idt) - 1U),
        .base = (uint64_t)(uintptr_t)&idt[0]
    };
    __asm__ volatile ("lidt %0" : : "m"(idtr));
    struct idt_register loaded;
    __asm__ volatile ("sidt %0" : "=m"(loaded));
    if (loaded.base != idtr.base || loaded.limit != idtr.limit) return false;
    timer_tick_count = 0U; timer_first_tick_reported = false;
    breakpoint_test_armed = false; breakpoint_test_passed = false;
    pic_remap_and_mask(); pit_init();
    return true;
}
bool interrupts_self_test(void) {
    breakpoint_test_passed = false; breakpoint_test_armed = true;
    __asm__ volatile ("int3");
    return breakpoint_test_passed;
}
void interrupts_enable(void) { __asm__ volatile ("sti" : : : "memory"); }
void interrupts_disable(void) { __asm__ volatile ("cli" : : : "memory"); }
uint64_t interrupts_ticks(void) { return timer_tick_count; }
void interrupts_wait_for_ticks(uint64_t target) {
    while (timer_tick_count < target) __asm__ volatile ("hlt");
}
void interrupt_dispatch(struct interrupt_frame *frame) {
    if (frame == NULL) {
        serial_write("HYBRID: FATAL_NULL_INTERRUPT_FRAME\n");
        interrupts_disable();
        for (;;) __asm__ volatile ("hlt");
    }
    if (frame->vector == TIMER_VECTOR) {
        ++timer_tick_count;
        if (!timer_first_tick_reported) {
            timer_first_tick_reported = true;
            serial_write("HYBRID: IRQ0_FIRST\n");
        }
        io_out8(0x20U, 0x20U);
        return;
    }
    if (frame->vector == 3U && breakpoint_test_armed) {
        breakpoint_test_armed = false; breakpoint_test_passed = true;
        serial_write("HYBRID: EXCEPTION_SELFTEST_OK\n");
        return;
    }
    serial_write("HYBRID: FATAL_EXCEPTION vector=");
    serial_write_hex64(frame->vector);
    serial_write(" error="); serial_write_hex64(frame->error_code);
    serial_write(" rip="); serial_write_hex64(frame->rip); serial_write("\n");
    interrupts_disable();
    for (;;) __asm__ volatile ("hlt");
}

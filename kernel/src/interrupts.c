#include "interrupts.h"
#include "scheduler.h"
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
    uint64_t vector, error_code, rip, cs, rflags, rsp, ss;
};
_Static_assert(sizeof(struct interrupt_frame) == 176U,
               "64-bit hardware interrupt frame includes RSP and SS");
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
static inline uint8_t io_in8(uint16_t port) {
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port) : "memory");
    return value;
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
static void idt_set_gate(uint8_t vector, uintptr_t handler, uint16_t selector, uint8_t ist) {
    idt[vector].offset_low = (uint16_t)(handler & 0xFFFFU);
    idt[vector].selector = selector; idt[vector].ist = (uint8_t)(ist & 0x07U);
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
    for (uint32_t vector = 0U; vector < IDT_ENTRIES; ++vector)
        idt_set_gate((uint8_t)vector, default_handler, selector, 0U);
    for (uint32_t vector = 0U; vector < 32U; ++vector) {
        uint8_t ist = 0U;
        if (vector == 2U) ist = 4U;       /* NMI */
        else if (vector == 8U) ist = 1U;  /* double fault */
        else if (vector == 13U) ist = 2U; /* general protection */
        else if (vector == 14U) ist = 3U; /* page fault */
        idt_set_gate((uint8_t)vector, (uintptr_t)isr_stub_table[vector], selector, ist);
    }
    idt_set_gate((uint8_t)TIMER_VECTOR, (uintptr_t)isr_stub_table[TIMER_VECTOR], selector, 0U);
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
bool interrupts_ist_self_test(void) {
    struct idt_register loaded;
    __asm__ volatile ("sidt %0" : "=m"(loaded));
    return loaded.base == (uint64_t)(uintptr_t)&idt[0]
        && loaded.limit == (uint16_t)(sizeof(idt) - 1U)
        && idt[2].ist == 4U
        && idt[8].ist == 1U
        && idt[13].ist == 2U
        && idt[14].ist == 3U
        && idt[32].ist == 0U;
}
bool interrupts_self_test(void) {
    breakpoint_test_passed = false; breakpoint_test_armed = true;
    __asm__ volatile ("int3");
    return breakpoint_test_passed;
}
void interrupts_enable(void) { __asm__ volatile ("sti" : : : "memory"); }
void interrupts_disable(void) { __asm__ volatile ("cli" : : : "memory"); }
uint64_t interrupts_ticks(void) { return timer_tick_count; }
bool interrupts_wait_for_ticks(uint64_t target) {
    /*
     * Use a bounded diagnostic wait rather than an unbounded HLT loop.
     * If IRQ0 is not delivered, report CPU IF and legacy PIC state so CI
     * fails with actionable evidence instead of timing out silently.
     */
    for (uint64_t spin = 0U; timer_tick_count < target && spin < UINT64_C(100000000); ++spin) {
        __asm__ volatile ("pause");
    }
    if (timer_tick_count < target) {
        uint64_t rflags;
        __asm__ volatile ("pushfq; popq %0" : "=r"(rflags));
        /* Select the PIC's IRR for diagnostic readback. */
        io_out8(0x20U, 0x0AU);
        const uint8_t master_irr = io_in8(0x20U);
        const uint8_t master_imr = io_in8(0x21U);
        serial_write("HYBRID: TIMER_DIAG ticks=");
        serial_write_hex64(timer_tick_count);
        serial_write(" rflags=");
        serial_write_hex64(rflags);
        serial_write(" pic_irr=");
        serial_write_hex64(master_irr);
        serial_write(" pic_imr=");
        serial_write_hex64(master_imr);
        serial_write("\n");
        return false;
    }
    return true;
}
struct interrupt_frame *interrupt_dispatch(struct interrupt_frame *frame) {
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
        return (struct interrupt_frame *)scheduler_on_timer_interrupt(frame);
    }
    if (frame->vector == 3U && breakpoint_test_armed) {
        breakpoint_test_armed = false; breakpoint_test_passed = true;
        serial_write("HYBRID: EXCEPTION_SELFTEST_OK\n");
        return frame;
    }
    serial_write("HYBRID: FATAL_EXCEPTION vector=");
    serial_write_hex64(frame->vector);
    serial_write(" error="); serial_write_hex64(frame->error_code);
    serial_write(" rip="); serial_write_hex64(frame->rip); serial_write("\n");
    interrupts_disable();
    for (;;) __asm__ volatile ("hlt");
}

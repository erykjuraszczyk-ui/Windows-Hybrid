#include "arch_tables.h"
#include "memory.h"

#include <stddef.h>
#include <stdint.h>

#define ARCH_TSS_STACK_COUNT 5U
#define ARCH_TSS_IST_COUNT 4U
#define ARCH_TSS_IST_DOUBLE_FAULT 0U
#define ARCH_TSS_IST_GENERAL_PROTECTION 1U
#define ARCH_TSS_IST_PAGE_FAULT 2U
#define ARCH_TSS_IST_NMI 3U

struct descriptor_register {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

struct x86_64_tss {
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist1;
    uint64_t ist2;
    uint64_t ist3;
    uint64_t ist4;
    uint64_t ist5;
    uint64_t ist6;
    uint64_t ist7;
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iomap_base;
} __attribute__((packed));

_Static_assert(sizeof(struct x86_64_tss) == 104U, "x86-64 TSS must be 104 bytes");

static uint64_t gdt[5] __attribute__((aligned(16)));
static struct x86_64_tss tss __attribute__((aligned(16)));
static uint64_t tss_stack_pages[ARCH_TSS_STACK_COUNT];
static bool tables_ready;

extern void arch_load_gdt_and_tss(const struct descriptor_register *gdtr);

static uint64_t read_rflags(void) {
    uint64_t flags;
    __asm__ volatile ("pushfq; popq %0" : "=r"(flags));
    return flags;
}
static void irq_restore(uint64_t flags) {
    if ((flags & (UINT64_C(1) << 9U)) != 0U)
        __asm__ volatile ("sti" : : : "memory");
}
static uint64_t make_tss_descriptor_low(uintptr_t base, uint32_t limit) {
    return ((uint64_t)limit & UINT64_C(0xFFFF))
         | (((uint64_t)base & UINT64_C(0xFFFFFF)) << 16U)
         | (UINT64_C(0x89) << 40U)
         | ((((uint64_t)limit >> 16U) & UINT64_C(0xF)) << 48U)
         | ((((uint64_t)base >> 24U) & UINT64_C(0xFF)) << 56U);
}
static void release_tss_stacks(uint32_t count) {
    while (count > 0U) {
        --count;
        if (tss_stack_pages[count] != 0U) {
            (void)memory_free_page(tss_stack_pages[count]);
            tss_stack_pages[count] = 0U;
        }
    }
}
static bool allocate_stack_top(uint32_t index, uint64_t *top) {
    uint64_t physical = 0U;
    if (top == NULL || !memory_alloc_page(&physical)) return false;
    void *virtual_base = memory_phys_to_virt(physical);
    if (virtual_base == NULL) {
        (void)memory_free_page(physical);
        return false;
    }
    tss_stack_pages[index] = physical;
    uintptr_t stack_top = (uintptr_t)virtual_base + (uintptr_t)HYBRID_PAGE_SIZE;
    stack_top &= ~(uintptr_t)0xFU;
    *top = (uint64_t)stack_top;
    return true;
}

bool arch_tables_init(void) {
    if (tables_ready) return arch_tables_self_test();

    const uint64_t saved_flags = read_rflags();
    __asm__ volatile ("cli" : : : "memory");
    for (uint32_t i = 0U; i < ARCH_TSS_STACK_COUNT; ++i) tss_stack_pages[i] = 0U;

    uint32_t allocated = 0U;
    uint64_t stack_top[ARCH_TSS_STACK_COUNT] = {0U, 0U, 0U, 0U, 0U};
    for (; allocated < ARCH_TSS_STACK_COUNT; ++allocated) {
        if (!allocate_stack_top(allocated, &stack_top[allocated])) {
            release_tss_stacks(allocated);
            irq_restore(saved_flags);
            return false;
        }
    }

    tss = (struct x86_64_tss){0};
    tss.rsp0 = stack_top[0];
    tss.ist1 = stack_top[1];
    tss.ist2 = stack_top[2];
    tss.ist3 = stack_top[3];
    tss.ist4 = stack_top[4];
    tss.iomap_base = (uint16_t)sizeof(tss);

    gdt[0] = 0U;
    gdt[1] = UINT64_C(0x00AF9A000000FFFF); /* 64-bit ring-0 code */
    gdt[2] = UINT64_C(0x00CF92000000FFFF); /* ring-0 data */
    const uintptr_t tss_base = (uintptr_t)&tss;
    const uint32_t tss_limit = (uint32_t)sizeof(tss) - 1U;
    gdt[3] = make_tss_descriptor_low(tss_base, tss_limit);
    gdt[4] = (uint64_t)(tss_base >> 32U);

    const struct descriptor_register gdtr = {
        .limit = (uint16_t)(sizeof(gdt) - 1U),
        .base = (uint64_t)(uintptr_t)&gdt[0]
    };
    arch_load_gdt_and_tss(&gdtr);
    tables_ready = true;
    const bool valid = arch_tables_self_test();
    if (!valid) tables_ready = false;
    irq_restore(saved_flags);
    if (!valid) {
        release_tss_stacks(ARCH_TSS_STACK_COUNT);
        return false;
    }
    return true;
}

bool arch_tables_self_test(void) {
    if (!tables_ready && tss.rsp0 == 0U) return false;

    struct descriptor_register loaded;
    uint16_t task_register;
    __asm__ volatile ("sgdt %0" : "=m"(loaded));
    __asm__ volatile ("str %0" : "=r"(task_register));

    const uint8_t tss_type = (uint8_t)((gdt[3] >> 40U) & UINT64_C(0xFF));
    return loaded.base == (uint64_t)(uintptr_t)&gdt[0]
        && loaded.limit == (uint16_t)(sizeof(gdt) - 1U)
        && task_register == HYBRID_TSS_SELECTOR
        && tss_type == 0x8BU /* LTR marks the descriptor busy */
        && tss.rsp0 != 0U
        && tss.ist1 != 0U && tss.ist2 != 0U
        && tss.ist3 != 0U && tss.ist4 != 0U;
}

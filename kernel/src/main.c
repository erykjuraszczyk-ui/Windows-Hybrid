#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <limine.h>
#include "console.h"
#include "arch_tables.h"
#include "interrupts.h"
#include "memory.h"
#include "scheduler.h"

__attribute__((used, section(".limine_requests")))
static volatile uint64_t base_revision[] = LIMINE_BASE_REVISION(6);

__attribute__((used, section(".limine_requests")))
static volatile struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST_ID,
    .revision = 0
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_hhdm_request hhdm_request = {
    .id = LIMINE_HHDM_REQUEST_ID,
    .revision = 0
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_memmap_request memmap_request = {
    .id = LIMINE_MEMMAP_REQUEST_ID,
    .revision = 0
};

__attribute__((used, section(".limine_requests_start")))
static volatile uint64_t requests_start[] = LIMINE_REQUESTS_START_MARKER;

__attribute__((used, section(".limine_requests_end")))
static volatile uint64_t requests_end[] = LIMINE_REQUESTS_END_MARKER;

static inline void cpu_halt(void) {
    for (;;) {
        __asm__ volatile ("cli; hlt");
    }
}

static const uint8_t font5x7[128][7] = {
    [' '] = {0, 0, 0, 0, 0, 0, 0},
    ['.'] = {0, 0, 0, 0, 0x0C, 0x0C, 0},
    ['-'] = {0, 0, 0, 0x1F, 0, 0, 0},
    ['/'] = {0x01, 0x02, 0x02, 0x04, 0x08, 0x08, 0x10},
    ['0'] = {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E},
    ['1'] = {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E},
    ['2'] = {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F},
    ['3'] = {0x1E, 0x01, 0x01, 0x0E, 0x01, 0x01, 0x1E},
    ['4'] = {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02},
    ['5'] = {0x1F, 0x10, 0x10, 0x1E, 0x01, 0x01, 0x1E},
    ['6'] = {0x0E, 0x10, 0x10, 0x1E, 0x11, 0x11, 0x0E},
    ['7'] = {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08},
    ['8'] = {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E},
    ['9'] = {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x01, 0x0E},
    ['A'] = {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11},
    ['B'] = {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E},
    ['C'] = {0x0F, 0x10, 0x10, 0x10, 0x10, 0x10, 0x0F},
    ['D'] = {0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E},
    ['E'] = {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F},
    ['F'] = {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10},
    ['G'] = {0x0F, 0x10, 0x10, 0x17, 0x11, 0x11, 0x0F},
    ['H'] = {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11},
    ['I'] = {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x1F},
    ['J'] = {0x07, 0x02, 0x02, 0x02, 0x12, 0x12, 0x0C},
    ['K'] = {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11},
    ['L'] = {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F},
    ['M'] = {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11},
    ['N'] = {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11},
    ['O'] = {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E},
    ['P'] = {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10},
    ['Q'] = {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D},
    ['R'] = {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11},
    ['S'] = {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E},
    ['T'] = {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04},
    ['U'] = {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E},
    ['V'] = {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04},
    ['W'] = {0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A},
    ['X'] = {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11},
    ['Y'] = {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04},
    ['Z'] = {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F}
};

static uint32_t color_channel(uint8_t value, uint8_t bits, uint8_t shift) {
    if (bits == 0U || bits > 8U || shift >= 32U) {
        return 0U;
    }
    const uint64_t maximum = ((uint64_t)1U << bits) - 1U;
    return (uint32_t)(((uint64_t)value * maximum / 255U) << shift);
}

static uint32_t pack_rgb(const struct limine_framebuffer *fb,
                         uint8_t red, uint8_t green, uint8_t blue) {
    return color_channel(red, fb->red_mask_size, fb->red_mask_shift)
         | color_channel(green, fb->green_mask_size, fb->green_mask_shift)
         | color_channel(blue, fb->blue_mask_size, fb->blue_mask_shift);
}

static void pixel(struct limine_framebuffer *fb, uint64_t x, uint64_t y,
                  uint32_t color) {
    if (x >= fb->width || y >= fb->height) {
        return;
    }
    volatile uint32_t *buffer = (volatile uint32_t *)fb->address;
    const uint64_t stride = fb->pitch / sizeof(uint32_t);
    buffer[y * stride + x] = color;
}

static void rectangle(struct limine_framebuffer *fb, uint64_t x, uint64_t y,
                      uint64_t width, uint64_t height, uint32_t color) {
    for (uint64_t row = 0; row < height && y + row >= y; ++row) {
        if (y + row >= fb->height) {
            break;
        }
        for (uint64_t col = 0; col < width && x + col >= x; ++col) {
            if (x + col >= fb->width) {
                break;
            }
            pixel(fb, x + col, y + row, color);
        }
    }
}

static size_t text_length(const char *text) {
    size_t length = 0;
    while (text[length] != '\0') {
        ++length;
    }
    return length;
}

static uint8_t font_character(char character, size_t row) {
    unsigned char code = (unsigned char)character;
    if (code >= (unsigned char)'a' && code <= (unsigned char)'z') {
        code = (unsigned char)(code - (unsigned char)'a' + (unsigned char)'A');
    }
    if (code >= 128U || row >= 7U) {
        return 0U;
    }
    return font5x7[code][row];
}

static void draw_text(struct limine_framebuffer *fb, uint64_t x, uint64_t y,
                      const char *text, uint64_t scale, uint32_t color) {
    if (scale == 0U) {
        return;
    }
    for (size_t index = 0; text[index] != '\0'; ++index) {
        const uint64_t origin_x = x + (uint64_t)index * 6U * scale;
        for (size_t row = 0; row < 7U; ++row) {
            const uint8_t bits = font_character(text[index], row);
            for (size_t col = 0; col < 5U; ++col) {
                if ((bits & (uint8_t)(1U << (4U - col))) != 0U) {
                    rectangle(fb, origin_x + (uint64_t)col * scale,
                              y + (uint64_t)row * scale,
                              scale, scale, color);
                }
            }
        }
    }
}

static void draw_centered(struct limine_framebuffer *fb, uint64_t y,
                          const char *text, uint64_t scale, uint32_t color) {
    const uint64_t width = (uint64_t)text_length(text) * 6U * scale;
    const uint64_t x = width < fb->width ? (fb->width - width) / 2U : 0U;
    draw_text(fb, x, y, text, scale, color);
}

static bool draw_splash(struct limine_framebuffer *fb) {
    if (fb == NULL || fb->address == NULL || fb->bpp != 32U ||
        fb->memory_model != LIMINE_FRAMEBUFFER_RGB ||
        fb->width == 0U || fb->height == 0U ||
        fb->pitch < fb->width * sizeof(uint32_t)) {
        return false;
    }

    for (uint64_t y = 0; y < fb->height; ++y) {
        const uint8_t green = (uint8_t)(7U + (y * 18U / fb->height));
        for (uint64_t x = 0; x < fb->width; ++x) {
            const uint8_t blue = (uint8_t)(24U + (x * 38U / fb->width));
            pixel(fb, x, y, pack_rgb(fb, 3U, green, blue));
        }
    }

    const uint32_t cyan = pack_rgb(fb, 58U, 231U, 255U);
    const uint32_t white = pack_rgb(fb, 235U, 248U, 255U);
    const uint32_t muted = pack_rgb(fb, 130U, 183U, 210U);
    const uint64_t unit = fb->width >= 1000U ? 4U : (fb->width >= 640U ? 3U : 2U);
    const uint64_t center_x = fb->width / 2U;
    const uint64_t icon_y = fb->height / 5U;
    const uint64_t icon_size = unit * 12U;

    rectangle(fb, center_x - icon_size / 2U, icon_y, icon_size, unit, cyan);
    rectangle(fb, center_x - icon_size / 2U, icon_y + icon_size - unit,
              icon_size, unit, cyan);
    rectangle(fb, center_x - icon_size / 2U, icon_y, unit, icon_size, cyan);
    rectangle(fb, center_x + icon_size / 2U - unit, icon_y, unit, icon_size, cyan);
    rectangle(fb, center_x - unit / 2U, icon_y + unit * 2U,
              unit, icon_size - unit * 4U, white);

    draw_centered(fb, fb->height / 2U, "HYBRID OS", unit, white);
    draw_centered(fb, fb->height / 2U + unit * 10U, "V0.5.0", unit, cyan);
    draw_centered(fb, fb->height / 2U + unit * 22U, "BOOT OK", 2U, white);
    draw_centered(fb, fb->height - 24U, "X86-64 / UEFI", 1U, muted);
    rectangle(fb, 0U, 0U, fb->width, 4U, cyan);
    return true;
}

void kmain(void) {
    __asm__ volatile ("cli" : : : "memory");
    console_init();
    serial_write("\nHYBRID OS v0.7.0\n");
    serial_write("HYBRID: KERNEL_START\n");
    if (!LIMINE_BASE_REVISION_SUPPORTED(base_revision)) {
        serial_write("HYBRID: FAIL_BOOT_PROTOCOL_REVISION\n");
        cpu_halt();
    }
    if (hhdm_request.response == NULL || memmap_request.response == NULL) {
        serial_write("HYBRID: FAIL_MEMORY_REQUESTS\n");
        cpu_halt();
    }
    if (!memory_init(memmap_request.response, hhdm_request.response->offset)) {
        serial_write("HYBRID: FAIL_MEMORY_INIT\n");
        cpu_halt();
    }
    serial_write("HYBRID: MEMORY_MAP_OK free_pages=");
    serial_write_hex64(memory_free_pages());
    serial_write("\n");
    if (!memory_self_test()) {
        serial_write("HYBRID: FAIL_MEMORY_SELFTEST\n");
        cpu_halt();
    }
    serial_write("HYBRID: MEMORY_SELFTEST_OK\n");

    bool framebuffer_ready = false;
    if (framebuffer_request.response != NULL &&
        framebuffer_request.response->framebuffer_count > 0U) {
        framebuffer_ready = draw_splash(
            framebuffer_request.response->framebuffers[0]);
    }
    if (framebuffer_ready) serial_write("HYBRID: FRAMEBUFFER_OK\n");
    else serial_write("HYBRID: FRAMEBUFFER_UNAVAILABLE\n");

    if (!arch_tables_init()) {
        serial_write("HYBRID: FAIL_TSS_INIT\n");
        cpu_halt();
    }
    serial_write("HYBRID: TSS_OK\n");
    if (!arch_tables_self_test()) {
        serial_write("HYBRID: FAIL_TSS_SELFTEST\n");
        cpu_halt();
    }
    if (!interrupts_init()) {
        serial_write("HYBRID: FAIL_IDT_INIT\n");
        cpu_halt();
    }
    serial_write("HYBRID: IDT_OK\n");
    if (!interrupts_ist_self_test()) {
        serial_write("HYBRID: FAIL_IST_SELFTEST\n");
        cpu_halt();
    }
    serial_write("HYBRID: IST_OK\n");
    if (!interrupts_self_test()) {
        serial_write("HYBRID: FAIL_EXCEPTION_SELFTEST\n");
        cpu_halt();
    }

    interrupts_enable();
    const bool timer_ok = interrupts_wait_for_ticks(5U);
    interrupts_disable();
    if (!timer_ok || interrupts_ticks() < 5U) {
        serial_write("HYBRID: FAIL_TIMER ticks=");
        serial_write_hex64(interrupts_ticks());
        serial_write("\n");
        cpu_halt();
    }
    serial_write("HYBRID: TIMER_OK ticks=");
    serial_write_hex64(interrupts_ticks());
    serial_write("\n");
    if (!scheduler_self_test()) {
        serial_write("HYBRID: FAIL_SCHEDULER_SELFTEST\n");
        cpu_halt();
    }
    serial_write("HYBRID: SCHEDULER_SELFTEST_OK dispatches=");
    serial_write_hex64(scheduler_dispatch_count());
    serial_write("\n");
    if (!scheduler_task_lifecycle_self_test()) {
        serial_write("HYBRID: FAIL_TASK_LIFECYCLE\n");
        cpu_halt();
    }
    serial_write("HYBRID: TASK_LIFECYCLE_SELFTEST_OK\n");
    if (!scheduler_preemptive_self_test()) {
        serial_write("HYBRID: FAIL_PREEMPTIVE_SCHEDULER\n");
        cpu_halt();
    }
    serial_write("HYBRID: PREEMPTIVE_SELFTEST_OK switches=");
    serial_write_hex64(scheduler_preemptive_switch_count());
    serial_write(" task_a=");
    serial_write_hex64(scheduler_preemptive_counter_a());
    serial_write(" task_b=");
    serial_write_hex64(scheduler_preemptive_counter_b());
    serial_write("\n");
    if (!scheduler_blocking_sync_self_test()) {
        serial_write("HYBRID: FAIL_BLOCKING_SYNC\n");
        cpu_halt();
    }
    serial_write("HYBRID: BLOCKING_SYNC_SELFTEST_OK\n");
    if (!scheduler_timer_timeout_cancel_self_test()) {
        serial_write("HYBRID: FAIL_TIMER_TIMEOUT_CANCEL\n");
        cpu_halt();
    }
    serial_write("HYBRID: TIMER_TIMEOUT_CANCEL_SELFTEST_OK expirations=");
    serial_write_hex64(scheduler_timer_expiration_count());
    serial_write("\n");
    if (!scheduler_semaphore_ipc_self_test()) {
        serial_write("HYBRID: FAIL_SEMAPHORE_IPC\n");
        cpu_halt();
    }
    serial_write("HYBRID: SEMAPHORE_IPC_SELFTEST_OK\n");
    if (!scheduler_ipc_service_self_test()) {
        serial_write("HYBRID: FAIL_IPC_SERVICES\n");
        cpu_halt();
    }
    serial_write("HYBRID: IPC_SERVICES_SELFTEST_OK\n");
    if (!scheduler_ipc_request_response_discovery_self_test()) {
        serial_write("HYBRID: FAIL_IPC_REQUEST_RESPONSE\n");
        cpu_halt();
    }
    serial_write("HYBRID: IPC_REQUEST_RESPONSE_SELFTEST_OK\n");
    if (!scheduler_ipc_runtime_self_test()) {
        serial_write("HYBRID: FAIL_IPC_RUNTIME\n");
        cpu_halt();
    }
    serial_write("HYBRID: IPC_RUNTIME_SELFTEST_OK pending=");
    serial_write_hex64(scheduler_ipc_pending_call_count());
    serial_write("\n");
    if (!scheduler_ipc_lifecycle_self_test()) {
        serial_write("HYBRID: FAIL_IPC_LIFECYCLE\n");
        cpu_halt();
    }
    serial_write("HYBRID: IPC_LIFECYCLE_SELFTEST_OK\n");
    if (!scheduler_ipc_backpressure_self_test()) {
        serial_write("HYBRID: FAIL_IPC_BACKPRESSURE\n");
        cpu_halt();
    }
    serial_write("HYBRID: IPC_BACKPRESSURE_SELFTEST_OK\n");
    if (!scheduler_ipc_receipt_retry_self_test()) {
        serial_write("HYBRID: FAIL_IPC_RECEIPTS_RETRIES\n");
        cpu_halt();
    }
    serial_write("HYBRID: IPC_RECEIPTS_RETRIES_SELFTEST_OK\n");
    if (!scheduler_ipc_timer_retry_dead_letter_self_test()) {
        serial_write("HYBRID: FAIL_IPC_TIMER_RETRY_DEAD_LETTER\n");
        return;
    }
    serial_write("HYBRID: IPC_TIMER_RETRY_DEAD_LETTER_SELFTEST_OK\n");
    if (!scheduler_ipc_circuit_breaker_self_test()) {
        serial_write("HYBRID: FAIL_IPC_CIRCUIT_BREAKER\n");
        return;
    }
    serial_write("HYBRID: IPC_CIRCUIT_BREAKER_SELFTEST_OK\n");
    scheduler_ipc_diagnostics_t ipc_diagnostics = {0};
    scheduler_ipc_get_diagnostics(&ipc_diagnostics);
    if (ipc_diagnostics.pending_calls != 0U ||
        ipc_diagnostics.pending_deliveries != 0U ||
        ipc_diagnostics.active_services != 0U) {
        serial_write("HYBRID: FAIL_IPC_DIAGNOSTICS\n");
        cpu_halt();
    }
    serial_write("HYBRID: IPC_DIAGNOSTICS_OK pending=");
    serial_write_hex64(ipc_diagnostics.pending_calls);
    serial_write(" deliveries=");
    serial_write_hex64(ipc_diagnostics.pending_deliveries);
    serial_write(" services=");
    serial_write_hex64(ipc_diagnostics.active_services);
    serial_write("\n");
    serial_write("HYBRID: BOOT_OK\n");
    cpu_halt();
}

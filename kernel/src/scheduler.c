#include "scheduler.h"
#include "console.h"
#include "interrupts.h"
#include "memory.h"
#include <stddef.h>
#include <stdint.h>

typedef struct {
    scheduler_task_id_t id;
    volatile scheduler_task_state_t state;
    scheduler_step_fn step;
    void *context;
    scheduler_task_entry_fn entry;
    void * volatile saved_frame;
    uint64_t stack_physical;
} scheduler_task_t;

static scheduler_task_t task_table[SCHEDULER_MAX_TASKS];
static uint32_t scheduler_cursor;
static uint32_t next_task_id;
static bool dispatch_in_progress;
static uint64_t dispatch_count;
static uint64_t completed_count;
static uint64_t failed_count;
#define SCHEDULER_NO_TASK UINT32_MAX
#define SCHEDULER_PREEMPTIVE_TEST_TICKS 12U
struct scheduler_interrupt_frame {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t vector, error_code, rip, cs, rflags;
};
_Static_assert(sizeof(struct scheduler_interrupt_frame) == 160U, "ISR frame layout mismatch");
static volatile bool preemptive_active;
static void * volatile bootstrap_frame;
static volatile uint32_t current_task_index;
static volatile uint64_t preemptive_ticks, preemptive_switches;
static volatile uint64_t test_counter_a, test_counter_b;

static uint32_t find_unused_slot(void) {
    for (uint32_t i = 0U; i < SCHEDULER_MAX_TASKS; ++i)
        if (task_table[i].state == SCHEDULER_TASK_UNUSED) return i;
    return SCHEDULER_NO_TASK;
}
static uint32_t find_task_index(scheduler_task_id_t id) {
    if (id == 0U) return SCHEDULER_NO_TASK;
    for (uint32_t i = 0U; i < SCHEDULER_MAX_TASKS; ++i)
        if (task_table[i].id == id) return i;
    return SCHEDULER_NO_TASK;
}
static uint16_t scheduler_current_cs(void) {
    uint16_t selector;
    __asm__ volatile ("mov %%cs, %0" : "=r"(selector));
    return selector;
}
static uint32_t find_next_preemptive_task(uint32_t after_index) {
    const uint32_t start = after_index == SCHEDULER_NO_TASK ? SCHEDULER_MAX_TASKS - 1U : after_index;
    for (uint32_t offset = 1U; offset <= SCHEDULER_MAX_TASKS; ++offset) {
        const uint32_t index = (start + offset) % SCHEDULER_MAX_TASKS;
        if (task_table[index].state == SCHEDULER_TASK_READY &&
            task_table[index].entry != NULL && task_table[index].saved_frame != NULL)
            return index;
    }
    return SCHEDULER_NO_TASK;
}
static void scheduler_task_trampoline(void) __attribute__((noreturn));
static void scheduler_task_trampoline(void) {
    const uint32_t index = current_task_index;
    interrupts_disable();
    serial_write("HYBRID: PREEMPTIVE_TASK_ENTRY\n");
    interrupts_enable();
    if (index < SCHEDULER_MAX_TASKS && task_table[index].entry != NULL)
        task_table[index].entry();
    __asm__ volatile ("cli" : : : "memory");
    if (index < SCHEDULER_MAX_TASKS && task_table[index].state == SCHEDULER_TASK_RUNNING) {
        task_table[index].state = SCHEDULER_TASK_EXITED;
        ++completed_count;
    }
    for (;;) __asm__ volatile ("sti; hlt" : : : "memory");
}

void scheduler_init(void) {
    for (uint32_t i = 0U; i < SCHEDULER_MAX_TASKS; ++i) {
        task_table[i].id = 0U;
        task_table[i].state = SCHEDULER_TASK_UNUSED;
        task_table[i].step = NULL;
        task_table[i].context = NULL;
        task_table[i].entry = NULL;
        task_table[i].saved_frame = NULL;
        task_table[i].stack_physical = 0U;
    }
    scheduler_cursor = 0U;
    next_task_id = 1U;
    dispatch_in_progress = false;
    dispatch_count = 0U;
    completed_count = 0U;
    failed_count = 0U;
    preemptive_active = false;
    bootstrap_frame = NULL;
    current_task_index = SCHEDULER_NO_TASK;
    preemptive_ticks = 0U;
    preemptive_switches = 0U;
    test_counter_a = 0U;
    test_counter_b = 0U;
}

bool scheduler_create_task(scheduler_step_fn step, void *context, scheduler_task_id_t *task_id) {
    if (step == NULL || task_id == NULL || dispatch_in_progress || preemptive_active || next_task_id == 0U) {
        return false;
    }
    for (uint32_t offset = 0U; offset < SCHEDULER_MAX_TASKS; ++offset) {
        const uint32_t index = (scheduler_cursor + offset) % SCHEDULER_MAX_TASKS;
        scheduler_task_t *task = &task_table[index];
        if (task->state != SCHEDULER_TASK_UNUSED) continue;
        task->id = next_task_id++;
        task->state = SCHEDULER_TASK_READY;
        task->step = step;
        task->context = context;
        task->entry = NULL;
        task->saved_frame = NULL;
        task->stack_physical = 0U;
        *task_id = task->id;
        return true;
    }
    return false;
}

bool scheduler_run_once(void) {
    if (dispatch_in_progress || preemptive_active) return false;
    scheduler_task_t *selected = NULL;
    uint32_t selected_index = 0U;
    for (uint32_t offset = 0U; offset < SCHEDULER_MAX_TASKS; ++offset) {
        const uint32_t index = (scheduler_cursor + offset) % SCHEDULER_MAX_TASKS;
        if (task_table[index].state == SCHEDULER_TASK_READY && task_table[index].step != NULL) {
            selected = &task_table[index];
            selected_index = index;
            break;
        }
    }
    if (selected == NULL) return false;

    selected->state = SCHEDULER_TASK_RUNNING;
    dispatch_in_progress = true;
    const scheduler_step_result_t result = selected->step(selected->context);
    dispatch_in_progress = false;
    ++dispatch_count;
    scheduler_cursor = (selected_index + 1U) % SCHEDULER_MAX_TASKS;

    if (result == SCHEDULER_STEP_YIELD) {
        selected->state = SCHEDULER_TASK_READY;
    } else if (result == SCHEDULER_STEP_COMPLETE) {
        selected->state = SCHEDULER_TASK_EXITED;
        ++completed_count;
    } else {
        selected->state = SCHEDULER_TASK_FAILED;
        ++failed_count;
    }
    return true;
}

scheduler_task_state_t scheduler_task_state(scheduler_task_id_t task_id) {
    if (task_id == 0U) return SCHEDULER_TASK_UNUSED;
    for (uint32_t i = 0U; i < SCHEDULER_MAX_TASKS; ++i) {
        if (task_table[i].id == task_id) return task_table[i].state;
    }
    return SCHEDULER_TASK_UNUSED;
}
uint32_t scheduler_ready_count(void) {
    uint32_t ready = 0U;
    for (uint32_t i = 0U; i < SCHEDULER_MAX_TASKS; ++i)
        if (task_table[i].state == SCHEDULER_TASK_READY) ++ready;
    return ready;
}
uint64_t scheduler_dispatch_count(void) { return dispatch_count; }
uint64_t scheduler_completed_count(void) { return completed_count; }
uint64_t scheduler_failed_count(void) { return failed_count; }

bool scheduler_create_preemptive_task(scheduler_task_entry_fn entry, scheduler_task_id_t *task_id) {
    if (entry == NULL || task_id == NULL || dispatch_in_progress || preemptive_active ||
        next_task_id == 0U) return false;
    const uint32_t index = find_unused_slot();
    if (index == SCHEDULER_NO_TASK) return false;

    uint64_t physical = 0U;
    if (!memory_alloc_page(&physical)) return false;
    void *virtual_base = memory_phys_to_virt(physical);
    if (virtual_base == NULL) { (void)memory_free_page(physical); return false; }

    /* iretq enters the task with SysV RSP % 16 == 8. */
    const uintptr_t top = (uintptr_t)virtual_base + (uintptr_t)HYBRID_PAGE_SIZE - 8U;
    struct scheduler_interrupt_frame *frame =
        (struct scheduler_interrupt_frame *)(top - sizeof(*frame));
    uint64_t *words = (uint64_t *)frame;
    for (uint32_t i = 0U; i < 20U; ++i) words[i] = 0U;
    frame->vector = 32U;
    frame->error_code = 0U;
    frame->rip = (uint64_t)(uintptr_t)scheduler_task_trampoline;
    frame->cs = (uint64_t)scheduler_current_cs();
    frame->rflags = UINT64_C(0x202);
    serial_write("HYBRID: TASK_FRAME rip=");
    serial_write_hex64(frame->rip);
    serial_write(" cs=");
    serial_write_hex64(frame->cs);
    serial_write(" flags=");
    serial_write_hex64(frame->rflags);
    serial_write(" stack=");
    serial_write_hex64(stack_top);
    serial_write("\n");

    scheduler_task_t *task = &task_table[index];
    task->id = next_task_id++;
    task->state = SCHEDULER_TASK_READY;
    task->step = NULL;
    task->context = NULL;
    task->entry = entry;
    task->saved_frame = frame;
    task->stack_physical = physical;
    *task_id = task->id;
    return true;
}

void *scheduler_on_timer_interrupt(void *interrupt_frame) {
    if (!preemptive_active || interrupt_frame == NULL) return interrupt_frame;
    uint32_t previous = current_task_index;
    if (bootstrap_frame == NULL) {
        bootstrap_frame = interrupt_frame;
        previous = SCHEDULER_NO_TASK;
    } else if (previous < SCHEDULER_MAX_TASKS) {
        scheduler_task_t *task = &task_table[previous];
        task->saved_frame = interrupt_frame;
        if (task->state == SCHEDULER_TASK_RUNNING) task->state = SCHEDULER_TASK_READY;
    }

    ++preemptive_ticks;
    if (preemptive_ticks == 1U) serial_write("HYBRID: PREEMPTIVE_TICK1\n");
    if (preemptive_ticks == 2U) serial_write("HYBRID: PREEMPTIVE_TICK2\n");
    if (preemptive_ticks >= SCHEDULER_PREEMPTIVE_TEST_TICKS) {
        serial_write("HYBRID: PREEMPTIVE_RETURN_BOOT\n");
        preemptive_active = false;
        current_task_index = SCHEDULER_NO_TASK;
        void *resume = (void *)bootstrap_frame;
        bootstrap_frame = NULL;
        return resume != NULL ? resume : interrupt_frame;
    }

    const uint32_t next = find_next_preemptive_task(previous);
    if (next == SCHEDULER_NO_TASK) {
        preemptive_active = false;
        current_task_index = SCHEDULER_NO_TASK;
        void *resume = (void *)bootstrap_frame;
        bootstrap_frame = NULL;
        return resume != NULL ? resume : interrupt_frame;
    }
    if (previous < SCHEDULER_MAX_TASKS && previous != next) ++preemptive_switches;
    task_table[next].state = SCHEDULER_TASK_RUNNING;
    current_task_index = next;
    serial_write("HYBRID: PREEMPTIVE_DISPATCH frame=");
    serial_write_hex64((uint64_t)(uintptr_t)task_table[next].saved_frame);
    serial_write("\n");
    return (void *)task_table[next].saved_frame;
}

static void preemptive_test_a(void) {
    for (;;) { ++test_counter_a; __asm__ volatile ("pause"); }
}
static void preemptive_test_b(void) {
    for (;;) { ++test_counter_b; __asm__ volatile ("pause"); }
}

bool scheduler_preemptive_self_test(void) {
    scheduler_init();
    scheduler_task_id_t a = 0U, b = 0U;
    if (!scheduler_create_preemptive_task(preemptive_test_a, &a)) return false;
    if (!scheduler_create_preemptive_task(preemptive_test_b, &b)) {
        const uint32_t ix = find_task_index(a);
        if (ix < SCHEDULER_MAX_TASKS && task_table[ix].stack_physical != 0U) {
            (void)memory_free_page(task_table[ix].stack_physical);
            task_table[ix].stack_physical = 0U;
            task_table[ix].saved_frame = NULL;
            task_table[ix].entry = NULL;
            task_table[ix].state = SCHEDULER_TASK_EXITED;
        }
        return false;
    }

    preemptive_active = true;
    bootstrap_frame = NULL;
    current_task_index = SCHEDULER_NO_TASK;
    preemptive_ticks = 0U;
    preemptive_switches = 0U;
    const uint64_t target = interrupts_ticks() + SCHEDULER_PREEMPTIVE_TEST_TICKS;
    serial_write("HYBRID: PREEMPTIVE_START\n");
    interrupts_enable();
    const bool waited = interrupts_wait_for_ticks(target);
    interrupts_disable();

    const uint32_t ia = find_task_index(a), ib = find_task_index(b);
    const bool ran = waited && !preemptive_active && bootstrap_frame == NULL &&
        ia < SCHEDULER_MAX_TASKS && ib < SCHEDULER_MAX_TASKS &&
        test_counter_a > 0U && test_counter_b > 0U &&
        preemptive_switches >= (SCHEDULER_PREEMPTIVE_TEST_TICKS - 2U);

    bool cleanup_ok = true;
    for (uint32_t i = 0U; i < SCHEDULER_MAX_TASKS; ++i) {
        if (task_table[i].stack_physical != 0U) {
            if (!memory_free_page(task_table[i].stack_physical)) cleanup_ok = false;
            task_table[i].stack_physical = 0U;
            task_table[i].saved_frame = NULL;
            task_table[i].entry = NULL;
            if (task_table[i].state != SCHEDULER_TASK_FAILED)
                task_table[i].state = SCHEDULER_TASK_EXITED;
        }
    }
    return ran && cleanup_ok;
}
uint64_t scheduler_preemptive_switch_count(void) { return preemptive_switches; }
uint64_t scheduler_preemptive_counter_a(void) { return test_counter_a; }
uint64_t scheduler_preemptive_counter_b(void) { return test_counter_b; }


typedef struct {
    uint32_t tag;
    uint32_t remaining_steps;
    uint32_t runs;
    uint32_t *sequence;
    uint32_t *sequence_length;
    bool *trace_valid;
} scheduler_test_probe_t;

static scheduler_step_result_t scheduler_test_probe_step(void *context) {
    scheduler_test_probe_t *probe = (scheduler_test_probe_t *)context;
    if (probe == NULL || probe->remaining_steps == 0U) return SCHEDULER_STEP_COMPLETE;
    if (*probe->sequence_length >= 6U) {
        *probe->trace_valid = false;
    } else {
        probe->sequence[*probe->sequence_length] = probe->tag;
        ++(*probe->sequence_length);
    }
    ++probe->runs;
    --probe->remaining_steps;
    return probe->remaining_steps == 0U ? SCHEDULER_STEP_COMPLETE : SCHEDULER_STEP_YIELD;
}
static scheduler_step_result_t scheduler_capacity_probe_step(void *context) {
    (void)context;
    return SCHEDULER_STEP_COMPLETE;
}

bool scheduler_self_test(void) {
    scheduler_init();
    uint32_t sequence[6] = {0U, 0U, 0U, 0U, 0U, 0U};
    uint32_t sequence_length = 0U;
    bool trace_valid = true;
    scheduler_test_probe_t probes[3] = {
        {.tag=1U, .remaining_steps=2U, .runs=0U, .sequence=sequence,
         .sequence_length=&sequence_length, .trace_valid=&trace_valid},
        {.tag=2U, .remaining_steps=3U, .runs=0U, .sequence=sequence,
         .sequence_length=&sequence_length, .trace_valid=&trace_valid},
        {.tag=3U, .remaining_steps=1U, .runs=0U, .sequence=sequence,
         .sequence_length=&sequence_length, .trace_valid=&trace_valid}
    };
    scheduler_task_id_t ids[3] = {0U, 0U, 0U};
    if (!scheduler_create_task(scheduler_test_probe_step, &probes[0], &ids[0]) ||
        !scheduler_create_task(scheduler_test_probe_step, &probes[1], &ids[1]) ||
        !scheduler_create_task(scheduler_test_probe_step, &probes[2], &ids[2]) ||
        scheduler_create_task(NULL, NULL, &ids[0]) ||
        scheduler_create_task(scheduler_test_probe_step, NULL, NULL)) return false;

    uint32_t guard = 0U;
    while (scheduler_run_once()) if (++guard > 16U) return false;

    static const uint32_t expected[6] = {1U, 2U, 3U, 1U, 2U, 2U};
    if (!trace_valid || sequence_length != 6U || scheduler_dispatch_count() != 6U ||
        scheduler_completed_count() != 3U || scheduler_failed_count() != 0U ||
        scheduler_ready_count() != 0U ||
        scheduler_task_state(ids[0]) != SCHEDULER_TASK_EXITED ||
        scheduler_task_state(ids[1]) != SCHEDULER_TASK_EXITED ||
        scheduler_task_state(ids[2]) != SCHEDULER_TASK_EXITED ||
        probes[0].runs != 2U || probes[1].runs != 3U || probes[2].runs != 1U)
        return false;
    for (uint32_t i = 0U; i < 6U; ++i) if (sequence[i] != expected[i]) return false;

    scheduler_task_id_t capacity_ids[SCHEDULER_MAX_TASKS - 3U];
    for (uint32_t i = 0U; i < SCHEDULER_MAX_TASKS - 3U; ++i) {
        if (!scheduler_create_task(scheduler_capacity_probe_step, NULL, &capacity_ids[i]))
            return false;
    }
    scheduler_task_id_t rejected_id = 0U;
    if (scheduler_create_task(scheduler_capacity_probe_step, NULL, &rejected_id)) return false;

    guard = 0U;
    while (scheduler_run_once()) if (++guard > SCHEDULER_MAX_TASKS) return false;
    return scheduler_ready_count() == 0U &&
           scheduler_dispatch_count() == 11U &&
           scheduler_completed_count() == SCHEDULER_MAX_TASKS &&
           scheduler_failed_count() == 0U &&
           scheduler_task_state(0U) == SCHEDULER_TASK_UNUSED &&
           !scheduler_run_once();
}

#include "scheduler.h"
#include "interrupts.h"
#include "memory.h"
#include "console.h"
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
    scheduler_wait_queue_t *waiting_on;
    uint64_t wait_deadline;
    bool has_deadline;
    scheduler_wait_result_t wait_result;
} scheduler_task_t;

static scheduler_task_t task_table[SCHEDULER_MAX_TASKS];
static uint32_t scheduler_cursor;
static uint32_t next_task_id;
static bool dispatch_in_progress;
static bool scheduler_ever_initialized;
static uint64_t dispatch_count;
static uint64_t completed_count;
static uint64_t failed_count;
#define SCHEDULER_NO_TASK UINT32_MAX
#define SCHEDULER_PREEMPTIVE_TEST_TICKS 12U
#define SCHEDULER_BLOCKING_TEST_TICKS 40U
#define SCHEDULER_SEMAPHORE_IPC_TEST_TICKS 48U
struct scheduler_interrupt_frame {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t vector, error_code, rip, cs, rflags, rsp, ss;
};
_Static_assert(sizeof(struct scheduler_interrupt_frame) == 176U,
               "64-bit interrupt frame must include RSP and SS");
static volatile bool preemptive_active;
static void * volatile bootstrap_frame;
static volatile uint32_t current_task_index;
static volatile uint64_t preemptive_ticks, preemptive_switches;
static volatile uint64_t preemptive_tick_limit;
static volatile uint64_t timer_expiration_count;
static volatile uint64_t test_counter_a, test_counter_b;
#define SCHEDULER_TASK_TEST_ITERATIONS UINT64_C(50000)

static uint64_t scheduler_irq_save_disable(void) {
    uint64_t flags;
    __asm__ volatile ("pushfq; popq %0; cli" : "=r"(flags) : : "memory");
    return flags;
}
static void scheduler_irq_restore(uint64_t flags) {
    if ((flags & (UINT64_C(1) << 9U)) != 0U)
        __asm__ volatile ("sti" : : : "memory");
}

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
static uint16_t scheduler_current_ss(void) {
    uint16_t selector;
    __asm__ volatile ("mov %%ss, %0" : "=r"(selector));
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
    /* The synthetic iret frame keeps IF clear until current_task_index is captured. */
    const uint32_t index = current_task_index;
    __asm__ volatile ("sti" : : : "memory");
    if (index < SCHEDULER_MAX_TASKS && task_table[index].entry != NULL)
        task_table[index].entry();
    const uint64_t flags = scheduler_irq_save_disable();
    if (index < SCHEDULER_MAX_TASKS && task_table[index].state == SCHEDULER_TASK_RUNNING) {
        task_table[index].state = SCHEDULER_TASK_EXITED;
        ++completed_count;
    }
    scheduler_irq_restore(flags);
    /* Defer stack reclamation until the interrupt path has switched away. */
    for (;;) __asm__ volatile ("sti; hlt" : : : "memory");
}

void scheduler_init(void) {
    if (preemptive_active) return;
    for (uint32_t i = 0U; i < SCHEDULER_MAX_TASKS; ++i) {
        if (task_table[i].stack_physical != 0U) {
            (void)memory_free_page(task_table[i].stack_physical);
            task_table[i].stack_physical = 0U;
        }
    }
    for (uint32_t i = 0U; i < SCHEDULER_MAX_TASKS; ++i) {
        task_table[i].id = 0U;
        task_table[i].state = SCHEDULER_TASK_UNUSED;
        task_table[i].step = NULL;
        task_table[i].context = NULL;
        task_table[i].entry = NULL;
        task_table[i].saved_frame = NULL;
        task_table[i].stack_physical = 0U;
        task_table[i].waiting_on = NULL;
        task_table[i].wait_deadline = 0U;
        task_table[i].has_deadline = false;
        task_table[i].wait_result = SCHEDULER_WAIT_NONE;
    }
    scheduler_cursor = 0U;
    if (!scheduler_ever_initialized) {
        next_task_id = 1U;
        scheduler_ever_initialized = true;
    }
    dispatch_in_progress = false;
    dispatch_count = 0U;
    completed_count = 0U;
    failed_count = 0U;
    preemptive_active = false;
    bootstrap_frame = NULL;
    current_task_index = SCHEDULER_NO_TASK;
    preemptive_ticks = 0U;
    preemptive_switches = 0U;
    preemptive_tick_limit = SCHEDULER_PREEMPTIVE_TEST_TICKS;
    timer_expiration_count = 0U;
    test_counter_a = 0U;
    test_counter_b = 0U;
}

bool scheduler_create_task(scheduler_step_fn step, void *context, scheduler_task_id_t *task_id) {
    const uint64_t flags = scheduler_irq_save_disable();
    if (step == NULL || task_id == NULL || dispatch_in_progress ||
        preemptive_active || next_task_id == 0U) {
        scheduler_irq_restore(flags);
        return false;
    }
    const uint32_t index = find_unused_slot();
    if (index == SCHEDULER_NO_TASK) {
        scheduler_irq_restore(flags);
        return false;
    }
    scheduler_task_t *task = &task_table[index];
    task->id = next_task_id++;
    task->state = SCHEDULER_TASK_READY;
    task->step = step;
    task->context = context;
    task->entry = NULL;
    task->saved_frame = NULL;
    task->stack_physical = 0U;
    task->waiting_on = NULL;
    task->wait_deadline = 0U;
    task->has_deadline = false;
    task->wait_result = SCHEDULER_WAIT_NONE;
    *task_id = task->id;
    scheduler_irq_restore(flags);
    return true;
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

bool scheduler_task_create(scheduler_task_entry_fn entry, scheduler_task_id_t *task_id) {
    const uint64_t flags = scheduler_irq_save_disable();
    if (entry == NULL || task_id == NULL || dispatch_in_progress ||
        preemptive_active || next_task_id == 0U) {
        scheduler_irq_restore(flags);
        return false;
    }
    const uint32_t index = find_unused_slot();
    if (index == SCHEDULER_NO_TASK) {
        scheduler_irq_restore(flags);
        return false;
    }
    uint64_t physical = 0U;
    if (!memory_alloc_page(&physical)) {
        scheduler_irq_restore(flags);
        return false;
    }
    void *virtual_base = memory_phys_to_virt(physical);
    if (virtual_base == NULL) {
        (void)memory_free_page(physical);
        scheduler_irq_restore(flags);
        return false;
    }
    uintptr_t top = (uintptr_t)virtual_base + (uintptr_t)HYBRID_PAGE_SIZE - 8U;
    top &= ~(uintptr_t)0x7U;
    struct scheduler_interrupt_frame *frame =
        (struct scheduler_interrupt_frame *)(top - sizeof(*frame));
    uint64_t *words = (uint64_t *)frame;
    for (uint32_t i = 0U; i < 22U; ++i) words[i] = 0U;
    frame->vector = 32U;
    frame->error_code = 0U;
    frame->rip = (uint64_t)(uintptr_t)scheduler_task_trampoline;
    frame->cs = (uint64_t)scheduler_current_cs();
    frame->rflags = UINT64_C(0x2);
    frame->rsp = (uint64_t)top;
    frame->ss = (uint64_t)scheduler_current_ss();
    scheduler_task_t *task = &task_table[index];
    task->id = next_task_id++;
    task->state = SCHEDULER_TASK_READY;
    task->step = NULL;
    task->context = NULL;
    task->entry = entry;
    task->saved_frame = frame;
    task->stack_physical = physical;
    task->waiting_on = NULL;
    task->wait_deadline = 0U;
    task->has_deadline = false;
    task->wait_result = SCHEDULER_WAIT_NONE;
    *task_id = task->id;
    scheduler_irq_restore(flags);
    return true;
}
bool scheduler_create_preemptive_task(scheduler_task_entry_fn entry, scheduler_task_id_t *task_id) {
    return scheduler_task_create(entry, task_id);
}
bool scheduler_task_reap(scheduler_task_id_t task_id) {
    const uint64_t flags = scheduler_irq_save_disable();
    const uint32_t index = find_task_index(task_id);
    if (index == SCHEDULER_NO_TASK || preemptive_active ||
        current_task_index == index || task_table[index].waiting_on != NULL ||
        (task_table[index].state != SCHEDULER_TASK_EXITED &&
         task_table[index].state != SCHEDULER_TASK_FAILED &&
         task_table[index].state != SCHEDULER_TASK_CANCELLED)) {
        scheduler_irq_restore(flags);
        return false;
    }
    if (task_table[index].stack_physical != 0U &&
        !memory_free_page(task_table[index].stack_physical)) {
        scheduler_irq_restore(flags);
        return false;
    }
    task_table[index].id = 0U;
    task_table[index].state = SCHEDULER_TASK_UNUSED;
    task_table[index].step = NULL;
    task_table[index].context = NULL;
    task_table[index].entry = NULL;
    task_table[index].saved_frame = NULL;
    task_table[index].stack_physical = 0U;
    task_table[index].waiting_on = NULL;
    task_table[index].wait_deadline = 0U;
    task_table[index].has_deadline = false;
    task_table[index].wait_result = SCHEDULER_WAIT_NONE;
    scheduler_irq_restore(flags);
    return true;
}

bool scheduler_task_block(scheduler_task_id_t task_id) {
    const uint64_t flags = scheduler_irq_save_disable();
    const uint32_t index = find_task_index(task_id);
    if (index == SCHEDULER_NO_TASK || dispatch_in_progress || preemptive_active ||
        task_table[index].state != SCHEDULER_TASK_READY) {
        scheduler_irq_restore(flags);
        return false;
    }
    task_table[index].state = SCHEDULER_TASK_BLOCKED;
    task_table[index].has_deadline = false;
    task_table[index].wait_result = SCHEDULER_WAITING;
    scheduler_irq_restore(flags);
    return true;
}

bool scheduler_task_wake(scheduler_task_id_t task_id) {
    const uint64_t flags = scheduler_irq_save_disable();
    const uint32_t index = find_task_index(task_id);
    if (index == SCHEDULER_NO_TASK ||
        task_table[index].state != SCHEDULER_TASK_BLOCKED ||
        task_table[index].waiting_on != NULL) {
        scheduler_irq_restore(flags);
        return false;
    }
    task_table[index].state = SCHEDULER_TASK_READY;
    task_table[index].has_deadline = false;
    task_table[index].wait_result = SCHEDULER_WAIT_SIGNALED;
    scheduler_irq_restore(flags);
    return true;
}

static void scheduler_remove_waiter_locked(uint32_t index) {
    if (index >= SCHEDULER_MAX_TASKS) return;
    scheduler_task_t *task = &task_table[index];
    if (task->waiting_on != NULL) {
        task->waiting_on->waiters &= ~(UINT32_C(1) << index);
        task->waiting_on = NULL;
    }
}

static bool scheduler_deadline_reached(uint64_t now, uint64_t deadline) {
    return (int64_t)(now - deadline) >= 0;
}

static bool scheduler_has_blocked_tasks(void) {
    for (uint32_t i = 0U; i < SCHEDULER_MAX_TASKS; ++i)
        if (task_table[i].state == SCHEDULER_TASK_BLOCKED) return true;
    return false;
}

static void scheduler_process_timeouts(uint64_t now) {
    for (uint32_t i = 0U; i < SCHEDULER_MAX_TASKS; ++i) {
        scheduler_task_t *task = &task_table[i];
        if (task->state != SCHEDULER_TASK_BLOCKED || !task->has_deadline ||
            !scheduler_deadline_reached(now, task->wait_deadline)) continue;
        scheduler_remove_waiter_locked(i);
        task->has_deadline = false;
        task->wait_deadline = 0U;
        task->wait_result = (task->wait_result == SCHEDULER_WAIT_SLEEPING)
            ? SCHEDULER_WAIT_SLEEP_COMPLETE : SCHEDULER_WAIT_TIMED_OUT;
        task->state = SCHEDULER_TASK_READY;
        ++timer_expiration_count;
    }
}

void scheduler_wait_queue_init(scheduler_wait_queue_t *queue) {
    if (queue == NULL) return;
    const uint64_t flags = scheduler_irq_save_disable();
    queue->waiters = 0U;
    scheduler_irq_restore(flags);
}

static bool wait_queue_wake_one_locked(scheduler_wait_queue_t *queue) {
    if (queue == NULL) return false;
    for (uint32_t i = 0U; i < SCHEDULER_MAX_TASKS; ++i) {
        const uint32_t bit = UINT32_C(1) << i;
        if ((queue->waiters & bit) == 0U) continue;
        queue->waiters &= ~bit;
        if (task_table[i].waiting_on == queue &&
            task_table[i].state == SCHEDULER_TASK_BLOCKED) {
            task_table[i].waiting_on = NULL;
            task_table[i].has_deadline = false;
            task_table[i].wait_deadline = 0U;
            task_table[i].wait_result = SCHEDULER_WAIT_SIGNALED;
            task_table[i].state = SCHEDULER_TASK_READY;
            return true;
        }
    }
    return false;
}

static bool scheduler_wait_queue_wait_internal(scheduler_wait_queue_t *queue,
                                                uint64_t timeout_ticks,
                                                bool timed) {
    if (queue == NULL || (timed && (timeout_ticks == 0U || timeout_ticks > (uint64_t)INT64_MAX)))
        return false;
    const uint64_t flags = scheduler_irq_save_disable();
    const uint32_t index = current_task_index;
    if (!preemptive_active || index >= SCHEDULER_MAX_TASKS ||
        task_table[index].state != SCHEDULER_TASK_RUNNING ||
        task_table[index].waiting_on != NULL) {
        scheduler_irq_restore(flags);
        return false;
    }
    scheduler_task_t *task = &task_table[index];
    task->wait_result = SCHEDULER_WAITING;
    task->has_deadline = timed;
    task->wait_deadline = timed ? interrupts_ticks() + timeout_ticks : 0U;
    queue->waiters |= UINT32_C(1) << index;
    task->waiting_on = queue;
    task->state = SCHEDULER_TASK_BLOCKED;
    scheduler_irq_restore(flags);
    while (task_table[index].state == SCHEDULER_TASK_BLOCKED)
        __asm__ volatile ("sti; hlt; cli" : : : "memory");
    scheduler_irq_restore(flags);
    return task_table[index].state == SCHEDULER_TASK_RUNNING &&
           task_table[index].wait_result == SCHEDULER_WAIT_SIGNALED;
}

bool scheduler_wait_queue_wait(scheduler_wait_queue_t *queue) {
    return scheduler_wait_queue_wait_internal(queue, 0U, false);
}

bool scheduler_wait_queue_wait_timeout(scheduler_wait_queue_t *queue, uint64_t timeout_ticks) {
    return scheduler_wait_queue_wait_internal(queue, timeout_ticks, true);
}

bool scheduler_sleep_ticks(uint64_t ticks) {
    if (ticks == 0U) return true;
    if (ticks > (uint64_t)INT64_MAX) return false;
    const uint64_t flags = scheduler_irq_save_disable();
    const uint32_t index = current_task_index;
    if (!preemptive_active || index >= SCHEDULER_MAX_TASKS ||
        task_table[index].state != SCHEDULER_TASK_RUNNING ||
        task_table[index].waiting_on != NULL) {
        scheduler_irq_restore(flags);
        return false;
    }
    scheduler_task_t *task = &task_table[index];
    task->wait_deadline = interrupts_ticks() + ticks;
    task->has_deadline = true;
    task->wait_result = SCHEDULER_WAIT_SLEEPING;
    task->state = SCHEDULER_TASK_BLOCKED;
    scheduler_irq_restore(flags);
    while (task_table[index].state == SCHEDULER_TASK_BLOCKED)
        __asm__ volatile ("sti; hlt; cli" : : : "memory");
    scheduler_irq_restore(flags);
    return task_table[index].state == SCHEDULER_TASK_RUNNING &&
           task_table[index].wait_result == SCHEDULER_WAIT_SLEEP_COMPLETE;
}

bool scheduler_task_cancel(scheduler_task_id_t task_id) {
    const uint64_t flags = scheduler_irq_save_disable();
    const uint32_t index = find_task_index(task_id);
    if (index == SCHEDULER_NO_TASK || current_task_index == index ||
        (task_table[index].state != SCHEDULER_TASK_READY &&
         task_table[index].state != SCHEDULER_TASK_BLOCKED)) {
        scheduler_irq_restore(flags);
        return false;
    }
    scheduler_remove_waiter_locked(index);
    task_table[index].has_deadline = false;
    task_table[index].wait_deadline = 0U;
    task_table[index].wait_result = SCHEDULER_WAIT_CANCELLED;
    task_table[index].state = SCHEDULER_TASK_CANCELLED;
    scheduler_irq_restore(flags);
    return true;
}

uint64_t scheduler_timer_expiration_count(void) { return timer_expiration_count; }

bool scheduler_wait_queue_wake_one(scheduler_wait_queue_t *queue) {
    const uint64_t flags = scheduler_irq_save_disable();
    const bool woke = wait_queue_wake_one_locked(queue);
    scheduler_irq_restore(flags);
    return woke;
}

uint32_t scheduler_wait_queue_wake_all(scheduler_wait_queue_t *queue) {
    const uint64_t flags = scheduler_irq_save_disable();
    uint32_t count = 0U;
    while (wait_queue_wake_one_locked(queue)) ++count;
    scheduler_irq_restore(flags);
    return count;
}

void scheduler_mutex_init(scheduler_mutex_t *mutex) {
    if (mutex == NULL) return;
    const uint64_t flags = scheduler_irq_save_disable();
    mutex->locked = 0U;
    mutex->owner = 0U;
    mutex->waiters.waiters = 0U;
    scheduler_irq_restore(flags);
}

bool scheduler_mutex_lock(scheduler_mutex_t *mutex) {
    if (mutex == NULL) return false;
    for (;;) {
        const uint64_t flags = scheduler_irq_save_disable();
        const uint32_t index = current_task_index;
        if (!preemptive_active || index >= SCHEDULER_MAX_TASKS ||
            task_table[index].state != SCHEDULER_TASK_RUNNING) {
            scheduler_irq_restore(flags);
            return false;
        }
        if (mutex->locked == 0U) {
            mutex->locked = 1U;
            mutex->owner = task_table[index].id;
            scheduler_irq_restore(flags);
            return true;
        }
        if (mutex->owner == task_table[index].id) {
            scheduler_irq_restore(flags);
            return false; /* non-recursive mutex */
        }
        if (task_table[index].waiting_on != NULL) {
            scheduler_irq_restore(flags);
            return false;
        }
        mutex->waiters.waiters |= UINT32_C(1) << index;
        task_table[index].waiting_on = &mutex->waiters;
        task_table[index].state = SCHEDULER_TASK_BLOCKED;
        scheduler_irq_restore(flags);
        while (task_table[index].state == SCHEDULER_TASK_BLOCKED)
            __asm__ volatile ("sti; hlt; cli" : : : "memory");
        scheduler_irq_restore(flags);
        if (task_table[index].state != SCHEDULER_TASK_RUNNING) return false;
    }
}

bool scheduler_mutex_unlock(scheduler_mutex_t *mutex) {
    if (mutex == NULL) return false;
    const uint64_t flags = scheduler_irq_save_disable();
    const uint32_t index = current_task_index;
    if (!preemptive_active || index >= SCHEDULER_MAX_TASKS ||
        task_table[index].state != SCHEDULER_TASK_RUNNING ||
        mutex->locked == 0U || mutex->owner != task_table[index].id) {
        scheduler_irq_restore(flags);
        return false;
    }
    mutex->locked = 0U;
    mutex->owner = 0U;
    (void)wait_queue_wake_one_locked(&mutex->waiters);
    scheduler_irq_restore(flags);
    return true;
}

static bool scheduler_block_current_locked(scheduler_wait_queue_t *queue, uint32_t *index_out) {
    if (queue == NULL || index_out == NULL || !preemptive_active) return false;
    const uint32_t index = current_task_index;
    if (index >= SCHEDULER_MAX_TASKS ||
        task_table[index].state != SCHEDULER_TASK_RUNNING ||
        task_table[index].waiting_on != NULL) return false;
    scheduler_task_t *task = &task_table[index];
    queue->waiters |= UINT32_C(1) << index;
    task->waiting_on = queue;
    task->wait_deadline = 0U;
    task->has_deadline = false;
    task->wait_result = SCHEDULER_WAITING;
    task->state = SCHEDULER_TASK_BLOCKED;
    *index_out = index;
    return true;
}

static bool scheduler_wait_for_blocked_task(uint32_t index) {
    while (index < SCHEDULER_MAX_TASKS && task_table[index].state == SCHEDULER_TASK_BLOCKED)
        __asm__ volatile ("sti; hlt; cli" : : : "memory");
    return index < SCHEDULER_MAX_TASKS &&
           task_table[index].state == SCHEDULER_TASK_RUNNING;
}

void scheduler_semaphore_init(scheduler_semaphore_t *semaphore,
                              uint32_t initial_count, uint32_t max_count) {
    if (semaphore == NULL || max_count == 0U || initial_count > max_count) return;
    const uint64_t flags = scheduler_irq_save_disable();
    semaphore->count = initial_count;
    semaphore->max_count = max_count;
    semaphore->waiters.waiters = 0U;
    scheduler_irq_restore(flags);
}

bool scheduler_semaphore_wait(scheduler_semaphore_t *semaphore) {
    if (semaphore == NULL) return false;
    for (;;) {
        const uint64_t flags = scheduler_irq_save_disable();
        if (semaphore->max_count == 0U || semaphore->count > semaphore->max_count) {
            scheduler_irq_restore(flags);
            return false;
        }
        if (semaphore->count > 0U) {
            --semaphore->count;
            scheduler_irq_restore(flags);
            return true;
        }
        uint32_t index = SCHEDULER_NO_TASK;
        if (!scheduler_block_current_locked(&semaphore->waiters, &index)) {
            scheduler_irq_restore(flags);
            return false;
        }
        scheduler_irq_restore(flags);
        const bool resumed = scheduler_wait_for_blocked_task(index);
        const bool handed_token = resumed &&
            task_table[index].wait_result == SCHEDULER_WAIT_SIGNALED;
        scheduler_irq_restore(flags);
        return handed_token;
    }
}

bool scheduler_semaphore_post(scheduler_semaphore_t *semaphore) {
    if (semaphore == NULL) return false;
    const uint64_t flags = scheduler_irq_save_disable();
    if (semaphore->max_count == 0U || semaphore->count > semaphore->max_count) {
        scheduler_irq_restore(flags);
        return false;
    }
    if (semaphore->waiters.waiters != 0U &&
        wait_queue_wake_one_locked(&semaphore->waiters)) {
        scheduler_irq_restore(flags);
        return true;
    }
    if (semaphore->count == semaphore->max_count) {
        scheduler_irq_restore(flags);
        return false;
    }
    ++semaphore->count;
    scheduler_irq_restore(flags);
    return true;
}

void scheduler_ipc_channel_init(scheduler_ipc_channel_t *channel) {
    if (channel == NULL) return;
    const uint64_t flags = scheduler_irq_save_disable();
    channel->head = 0U;
    channel->tail = 0U;
    channel->count = 0U;
    channel->readers.waiters = 0U;
    channel->writers.waiters = 0U;
    for (uint32_t i = 0U; i < SCHEDULER_IPC_CAPACITY; ++i) {
        channel->messages[i].type = 0U;
        channel->messages[i].length = 0U;
        for (uint32_t j = 0U; j < SCHEDULER_IPC_PAYLOAD_SIZE; ++j)
            channel->messages[i].payload[j] = 0U;
    }
    scheduler_irq_restore(flags);
}

static bool scheduler_ipc_message_valid(const scheduler_ipc_message_t *message) {
    return message != NULL && message->length <= SCHEDULER_IPC_PAYLOAD_SIZE;
}

static void scheduler_ipc_message_copy(scheduler_ipc_message_t *destination,
                                       const scheduler_ipc_message_t *source) {
    destination->type = source->type;
    destination->length = source->length;
    for (uint32_t i = 0U; i < SCHEDULER_IPC_PAYLOAD_SIZE; ++i)
        destination->payload[i] = source->payload[i];
}

bool scheduler_ipc_send(scheduler_ipc_channel_t *channel,
                        const scheduler_ipc_message_t *message) {
    if (channel == NULL || !scheduler_ipc_message_valid(message)) return false;
    for (;;) {
        const uint64_t flags = scheduler_irq_save_disable();
        if (channel->count > SCHEDULER_IPC_CAPACITY ||
            channel->head >= SCHEDULER_IPC_CAPACITY ||
            channel->tail >= SCHEDULER_IPC_CAPACITY) {
            scheduler_irq_restore(flags);
            return false;
        }
        if (channel->count < SCHEDULER_IPC_CAPACITY) {
            scheduler_ipc_message_copy(&channel->messages[channel->tail], message);
            channel->tail = (channel->tail + 1U) % SCHEDULER_IPC_CAPACITY;
            ++channel->count;
            if (channel->readers.waiters != 0U)
                (void)wait_queue_wake_one_locked(&channel->readers);
            scheduler_irq_restore(flags);
            return true;
        }
        uint32_t index = SCHEDULER_NO_TASK;
        if (!scheduler_block_current_locked(&channel->writers, &index)) {
            scheduler_irq_restore(flags);
            return false;
        }
        scheduler_irq_restore(flags);
        if (!scheduler_wait_for_blocked_task(index)) {
            scheduler_irq_restore(flags);
            return false;
        }
        scheduler_irq_restore(flags);
    }
}

bool scheduler_ipc_receive(scheduler_ipc_channel_t *channel,
                           scheduler_ipc_message_t *message) {
    if (channel == NULL || message == NULL) return false;
    for (;;) {
        const uint64_t flags = scheduler_irq_save_disable();
        if (channel->count > SCHEDULER_IPC_CAPACITY ||
            channel->head >= SCHEDULER_IPC_CAPACITY ||
            channel->tail >= SCHEDULER_IPC_CAPACITY) {
            scheduler_irq_restore(flags);
            return false;
        }
        if (channel->count > 0U) {
            scheduler_ipc_message_copy(message, &channel->messages[channel->head]);
            channel->head = (channel->head + 1U) % SCHEDULER_IPC_CAPACITY;
            --channel->count;
            if (channel->writers.waiters != 0U)
                (void)wait_queue_wake_one_locked(&channel->writers);
            scheduler_irq_restore(flags);
            return true;
        }
        uint32_t index = SCHEDULER_NO_TASK;
        if (!scheduler_block_current_locked(&channel->readers, &index)) {
            scheduler_irq_restore(flags);
            return false;
        }
        scheduler_irq_restore(flags);
        if (!scheduler_wait_for_blocked_task(index)) {
            scheduler_irq_restore(flags);
            return false;
        }
        scheduler_irq_restore(flags);
    }
}

static scheduler_semaphore_t semaphore_ipc_test_sem;
static scheduler_ipc_channel_t semaphore_ipc_test_channel;
static volatile uint32_t semaphore_ipc_test_sem_acquired;
static volatile uint32_t semaphore_ipc_test_sem_posted;
static volatile uint32_t semaphore_ipc_test_sent;
static volatile uint32_t semaphore_ipc_test_received;
static volatile uint32_t semaphore_ipc_test_errors;

static void semaphore_ipc_test_waiter(void) {
    if (scheduler_semaphore_wait(&semaphore_ipc_test_sem))
        ++semaphore_ipc_test_sem_acquired;
    else
        ++semaphore_ipc_test_errors;
}

static void semaphore_ipc_test_poster(void) {
    if (scheduler_semaphore_post(&semaphore_ipc_test_sem))
        ++semaphore_ipc_test_sem_posted;
    else
        ++semaphore_ipc_test_errors;
}

static void semaphore_ipc_test_sender(void) {
    for (uint32_t i = 0U; i < SCHEDULER_IPC_CAPACITY + 1U; ++i) {
        scheduler_ipc_message_t message = {0};
        message.type = UINT32_C(0x48594252);
        message.length = 2U;
        message.payload[0] = (uint8_t)i;
        message.payload[1] = (uint8_t)(i ^ UINT32_C(0xA5));
        if (!scheduler_ipc_send(&semaphore_ipc_test_channel, &message)) {
            ++semaphore_ipc_test_errors;
            return;
        }
        ++semaphore_ipc_test_sent;
    }
}

static void semaphore_ipc_test_receiver(void) {
    for (uint32_t i = 0U; i < SCHEDULER_IPC_CAPACITY + 1U; ++i) {
        scheduler_ipc_message_t message = {0};
        if (!scheduler_ipc_receive(&semaphore_ipc_test_channel, &message)) {
            ++semaphore_ipc_test_errors;
            return;
        }
        if (message.type != UINT32_C(0x48594252) || message.length != 2U ||
            message.payload[0] != (uint8_t)i ||
            message.payload[1] != (uint8_t)(i ^ UINT32_C(0xA5)))
            ++semaphore_ipc_test_errors;
        ++semaphore_ipc_test_received;
    }
}

bool scheduler_semaphore_ipc_self_test(void) {
    scheduler_init();
    scheduler_semaphore_init(&semaphore_ipc_test_sem, 0U, 1U);
    scheduler_ipc_channel_init(&semaphore_ipc_test_channel);
    semaphore_ipc_test_sem_acquired = 0U;
    semaphore_ipc_test_sem_posted = 0U;
    semaphore_ipc_test_sent = 0U;
    semaphore_ipc_test_received = 0U;
    semaphore_ipc_test_errors = 0U;

    if (semaphore_ipc_test_sem.count != 0U ||
        semaphore_ipc_test_sem.max_count != 1U ||
        scheduler_semaphore_wait(NULL) ||
        scheduler_semaphore_post(NULL) ||
        scheduler_ipc_send(NULL, NULL) ||
        scheduler_ipc_receive(NULL, NULL)) return false;
    scheduler_ipc_message_t invalid = {0};
    invalid.length = SCHEDULER_IPC_PAYLOAD_SIZE + 1U;
    if (scheduler_ipc_send(&semaphore_ipc_test_channel, &invalid)) return false;

    scheduler_task_id_t waiter = 0U, poster = 0U, sender = 0U, receiver = 0U;
    if (!scheduler_task_create(semaphore_ipc_test_waiter, &waiter) ||
        !scheduler_task_create(semaphore_ipc_test_poster, &poster) ||
        !scheduler_task_create(semaphore_ipc_test_sender, &sender) ||
        !scheduler_task_create(semaphore_ipc_test_receiver, &receiver)) return false;

    preemptive_active = true;
    bootstrap_frame = NULL;
    current_task_index = SCHEDULER_NO_TASK;
    preemptive_ticks = 0U;
    preemptive_switches = 0U;
    preemptive_tick_limit = SCHEDULER_SEMAPHORE_IPC_TEST_TICKS;
    const uint64_t target = interrupts_ticks() + SCHEDULER_SEMAPHORE_IPC_TEST_TICKS;
    interrupts_enable();
    const bool waited = interrupts_wait_for_ticks(target);
    interrupts_disable();

    const bool passed = waited && !preemptive_active && bootstrap_frame == NULL &&
        scheduler_task_state(waiter) == SCHEDULER_TASK_EXITED &&
        scheduler_task_state(poster) == SCHEDULER_TASK_EXITED &&
        scheduler_task_state(sender) == SCHEDULER_TASK_EXITED &&
        scheduler_task_state(receiver) == SCHEDULER_TASK_EXITED &&
        semaphore_ipc_test_sem_acquired == 1U && semaphore_ipc_test_sem_posted == 1U &&
        semaphore_ipc_test_sem.count == 0U &&
        semaphore_ipc_test_sem.waiters.waiters == 0U &&
        semaphore_ipc_test_sent == SCHEDULER_IPC_CAPACITY + 1U &&
        semaphore_ipc_test_received == SCHEDULER_IPC_CAPACITY + 1U &&
        semaphore_ipc_test_channel.count == 0U &&
        semaphore_ipc_test_channel.readers.waiters == 0U &&
        semaphore_ipc_test_channel.writers.waiters == 0U &&
        semaphore_ipc_test_errors == 0U && scheduler_failed_count() == 0U;
    const bool reaped = passed && scheduler_task_reap(waiter) &&
        scheduler_task_reap(poster) && scheduler_task_reap(sender) &&
        scheduler_task_reap(receiver);
    if (!passed || !reaped) {
        serial_write("HYBRID: SEMAPHORE_IPC_DIAG states=");
        serial_write_hex64(scheduler_task_state(waiter)); serial_write(",");
        serial_write_hex64(scheduler_task_state(poster)); serial_write(",");
        serial_write_hex64(scheduler_task_state(sender)); serial_write(",");
        serial_write_hex64(scheduler_task_state(receiver));
        serial_write(" sem="); serial_write_hex64(semaphore_ipc_test_sem.count);
        serial_write(" sem_waiters="); serial_write_hex64(semaphore_ipc_test_sem.waiters.waiters);
        serial_write(" sent="); serial_write_hex64(semaphore_ipc_test_sent);
        serial_write(" received="); serial_write_hex64(semaphore_ipc_test_received);
        serial_write(" ipc_count="); serial_write_hex64(semaphore_ipc_test_channel.count);
        serial_write(" readers="); serial_write_hex64(semaphore_ipc_test_channel.readers.waiters);
        serial_write(" writers="); serial_write_hex64(semaphore_ipc_test_channel.writers.waiters);
        serial_write(" errors="); serial_write_hex64(semaphore_ipc_test_errors);
        serial_write("
");
    }
    return passed && reaped;
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
    scheduler_process_timeouts(interrupts_ticks());
    if (preemptive_ticks >= preemptive_tick_limit) {
        for (uint32_t i = 0U; i < SCHEDULER_MAX_TASKS; ++i) {
            scheduler_task_t *task = &task_table[i];
            if (task->state == SCHEDULER_TASK_READY || task->state == SCHEDULER_TASK_BLOCKED) {
                scheduler_remove_waiter_locked(i);
                task->has_deadline = false;
                task->wait_deadline = 0U;
                task->state = SCHEDULER_TASK_FAILED;
                ++failed_count;
            }
        }
        preemptive_active = false;
        current_task_index = SCHEDULER_NO_TASK;
        void *resume = (void *)bootstrap_frame;
        bootstrap_frame = NULL;
        return resume != NULL ? resume : interrupt_frame;
    }

    const uint32_t next = find_next_preemptive_task(previous);
    if (next == SCHEDULER_NO_TASK) {
        if (scheduler_has_blocked_tasks() && bootstrap_frame != NULL) {
            /* Return to the test/idle caller, but retain the scheduler and its bootstrap frame.
               A later PIT tick can expire a timed waiter or select one made READY by an event. */
            current_task_index = SCHEDULER_NO_TASK;
            return bootstrap_frame;
        }
        if (scheduler_has_blocked_tasks()) {
            for (uint32_t i = 0U; i < SCHEDULER_MAX_TASKS; ++i) {
                if (task_table[i].state == SCHEDULER_TASK_BLOCKED) {
                    scheduler_remove_waiter_locked(i);
                    task_table[i].state = SCHEDULER_TASK_FAILED;
                    task_table[i].has_deadline = false;
                    ++failed_count;
                }
            }
        }
        preemptive_active = false;
        current_task_index = SCHEDULER_NO_TASK;
        void *resume = (void *)bootstrap_frame;
        bootstrap_frame = NULL;
        return resume != NULL ? resume : interrupt_frame;
    }
    if (previous < SCHEDULER_MAX_TASKS && previous != next) ++preemptive_switches;
    task_table[next].state = SCHEDULER_TASK_RUNNING;
    current_task_index = next;
    return (void *)task_table[next].saved_frame;
}

static scheduler_wait_queue_t blocking_test_queue;
static scheduler_mutex_t blocking_test_mutex;
static volatile uint32_t blocking_test_waiting;
static volatile uint32_t blocking_test_resumed;
static volatile uint32_t blocking_test_wake_ok;
static volatile uint32_t blocking_test_protected_count;
static volatile uint32_t blocking_test_mutex_errors;

static void blocking_test_waiter(void) {
    blocking_test_waiting = 1U;
    if (scheduler_wait_queue_wait(&blocking_test_queue)) ++blocking_test_resumed;
}
static void blocking_test_signaler(void) {
    uint64_t guard = 0U;
    while (blocking_test_queue.waiters == 0U && guard++ < UINT64_C(1000000))
        __asm__ volatile ("pause");
    if (blocking_test_queue.waiters != 0U &&
        scheduler_wait_queue_wake_one(&blocking_test_queue)) ++blocking_test_wake_ok;
}
static void blocking_test_mutex_worker(void) {
    if (!scheduler_mutex_lock(&blocking_test_mutex)) {
        ++blocking_test_mutex_errors;
        return;
    }
    for (uint32_t i = 0U; i < 10000U; ++i) {
        ++blocking_test_protected_count;
        __asm__ volatile ("pause");
    }
    if (!scheduler_mutex_unlock(&blocking_test_mutex)) ++blocking_test_mutex_errors;
}

bool scheduler_blocking_sync_self_test(void) {
    scheduler_init();
    scheduler_wait_queue_init(&blocking_test_queue);
    scheduler_mutex_init(&blocking_test_mutex);
    blocking_test_waiting = 0U;
    blocking_test_resumed = 0U;
    blocking_test_wake_ok = 0U;
    blocking_test_protected_count = 0U;
    blocking_test_mutex_errors = 0U;
    scheduler_task_id_t waiter = 0U, signaler = 0U, worker_a = 0U, worker_b = 0U;
    if (!scheduler_task_create(blocking_test_waiter, &waiter) ||
        !scheduler_task_create(blocking_test_signaler, &signaler) ||
        !scheduler_task_create(blocking_test_mutex_worker, &worker_a) ||
        !scheduler_task_create(blocking_test_mutex_worker, &worker_b)) return false;
    preemptive_active = true;
    bootstrap_frame = NULL;
    current_task_index = SCHEDULER_NO_TASK;
    preemptive_ticks = 0U;
    preemptive_switches = 0U;
    preemptive_tick_limit = SCHEDULER_BLOCKING_TEST_TICKS;
    const uint64_t target = interrupts_ticks() + SCHEDULER_BLOCKING_TEST_TICKS;
    interrupts_enable();
    const bool waited = interrupts_wait_for_ticks(target);
    interrupts_disable();
    const bool passed = waited && !preemptive_active && bootstrap_frame == NULL &&
        scheduler_task_state(waiter) == SCHEDULER_TASK_EXITED &&
        scheduler_task_state(signaler) == SCHEDULER_TASK_EXITED &&
        scheduler_task_state(worker_a) == SCHEDULER_TASK_EXITED &&
        scheduler_task_state(worker_b) == SCHEDULER_TASK_EXITED &&
        blocking_test_waiting == 1U && blocking_test_resumed == 1U &&
        blocking_test_wake_ok == 1U &&
        blocking_test_protected_count == 20000U &&
        blocking_test_mutex_errors == 0U &&
        blocking_test_queue.waiters == 0U &&
        blocking_test_mutex.waiters.waiters == 0U &&
        blocking_test_mutex.locked == 0U && blocking_test_mutex.owner == 0U;
    const bool reaped = passed && scheduler_task_reap(waiter) && scheduler_task_reap(signaler) &&
        scheduler_task_reap(worker_a) && scheduler_task_reap(worker_b);
    if (!passed || !reaped) {
        serial_write("HYBRID: BLOCKING_DIAG states=");
        serial_write_hex64(scheduler_task_state(waiter)); serial_write(",");
        serial_write_hex64(scheduler_task_state(signaler)); serial_write(",");
        serial_write_hex64(scheduler_task_state(worker_a)); serial_write(",");
        serial_write_hex64(scheduler_task_state(worker_b));
        serial_write(" wake="); serial_write_hex64(blocking_test_wake_ok);
        serial_write(" resumed="); serial_write_hex64(blocking_test_resumed);
        serial_write(" count="); serial_write_hex64(blocking_test_protected_count);
        serial_write(" mutex_errors="); serial_write_hex64(blocking_test_mutex_errors);
        serial_write(" q="); serial_write_hex64(blocking_test_queue.waiters);
        serial_write(" mq="); serial_write_hex64(blocking_test_mutex.waiters.waiters);
        serial_write(" locked="); serial_write_hex64(blocking_test_mutex.locked);
        serial_write(" ticks="); serial_write_hex64(preemptive_ticks);
        serial_write(" active="); serial_write_hex64(preemptive_active ? 1U : 0U);
        serial_write("\\n");
    }
    return passed && reaped;
}


static scheduler_wait_queue_t timer_test_timeout_queue;
static scheduler_wait_queue_t timer_test_cancel_queue;
static scheduler_task_id_t timer_test_cancel_target_id;
static volatile uint32_t timer_test_timeout_started;
static volatile uint32_t timer_test_timeout_completed;
static volatile uint32_t timer_test_timeout_elapsed;
static volatile uint32_t timer_test_sleep_completed;
static volatile uint32_t timer_test_sleep_elapsed;
static volatile uint32_t timer_test_cancel_started;
static volatile uint32_t timer_test_cancelled;
static volatile uint32_t timer_test_cancel_waiter_resumed;

static void timer_test_timeout_waiter(void) {
    const uint64_t start = interrupts_ticks();
    timer_test_timeout_started = 1U;
    const bool signaled = scheduler_wait_queue_wait_timeout(&timer_test_timeout_queue, 4U);
    timer_test_timeout_elapsed = (uint32_t)(interrupts_ticks() - start);
    timer_test_timeout_completed = signaled ? 1U : 2U; /* 2 = timeout, 1 = signal */
}
static void timer_test_sleep_worker(void) {
    const uint64_t start = interrupts_ticks();
    if (scheduler_sleep_ticks(6U)) {
        timer_test_sleep_elapsed = (uint32_t)(interrupts_ticks() - start);
        timer_test_sleep_completed = 1U;
    }
}
static void timer_test_cancel_waiter(void) {
    timer_test_cancel_started = 1U;
    if (scheduler_wait_queue_wait(&timer_test_cancel_queue))
        ++timer_test_cancel_waiter_resumed;
}
static void timer_test_cancel_controller(void) {
    uint64_t guard = 0U;
    while (scheduler_task_state(timer_test_cancel_target_id) != SCHEDULER_TASK_BLOCKED &&
           guard++ < UINT64_C(1000000))
        __asm__ volatile ("pause");
    if (scheduler_task_state(timer_test_cancel_target_id) == SCHEDULER_TASK_BLOCKED &&
        scheduler_task_cancel(timer_test_cancel_target_id))
        timer_test_cancelled = 1U;
}

bool scheduler_timer_timeout_cancel_self_test(void) {
    scheduler_init();
    scheduler_wait_queue_init(&timer_test_timeout_queue);
    scheduler_wait_queue_init(&timer_test_cancel_queue);
    timer_test_cancel_target_id = 0U;
    timer_test_timeout_started = 0U;
    timer_test_timeout_completed = 0U;
    timer_test_timeout_elapsed = 0U;
    timer_test_sleep_completed = 0U;
    timer_test_sleep_elapsed = 0U;
    timer_test_cancel_started = 0U;
    timer_test_cancelled = 0U;
    timer_test_cancel_waiter_resumed = 0U;
    scheduler_task_id_t timeout_id = 0U, sleep_id = 0U, cancel_id = 0U, controller_id = 0U;
    if (!scheduler_task_create(timer_test_timeout_waiter, &timeout_id) ||
        !scheduler_task_create(timer_test_sleep_worker, &sleep_id) ||
        !scheduler_task_create(timer_test_cancel_waiter, &cancel_id) ||
        !scheduler_task_create(timer_test_cancel_controller, &controller_id)) return false;
    timer_test_cancel_target_id = cancel_id;
    preemptive_active = true;
    bootstrap_frame = NULL;
    current_task_index = SCHEDULER_NO_TASK;
    preemptive_ticks = 0U;
    preemptive_switches = 0U;
    preemptive_tick_limit = 48U;
    const uint64_t target = interrupts_ticks() + 48U;
    interrupts_enable();
    const bool waited = interrupts_wait_for_ticks(target);
    interrupts_disable();
    const bool states_ok =
        scheduler_task_state(timeout_id) == SCHEDULER_TASK_EXITED &&
        scheduler_task_state(sleep_id) == SCHEDULER_TASK_EXITED &&
        scheduler_task_state(cancel_id) == SCHEDULER_TASK_CANCELLED &&
        scheduler_task_state(controller_id) == SCHEDULER_TASK_EXITED;
    const bool results_ok = timer_test_timeout_started == 1U &&
        timer_test_timeout_completed == 2U && timer_test_timeout_elapsed >= 4U &&
        timer_test_sleep_completed == 1U && timer_test_sleep_elapsed >= 6U &&
        timer_test_cancel_started == 1U && timer_test_cancelled == 1U &&
        timer_test_cancel_waiter_resumed == 0U &&
        timer_test_timeout_queue.waiters == 0U && timer_test_cancel_queue.waiters == 0U &&
        scheduler_timer_expiration_count() >= 2U && scheduler_failed_count() == 0U;
    const bool reaped = !preemptive_active &&
        scheduler_task_reap(timeout_id) && scheduler_task_reap(sleep_id) &&
        scheduler_task_reap(cancel_id) && scheduler_task_reap(controller_id);
    if (!waited || !states_ok || !results_ok || !reaped) {
        serial_write("HYBRID: TIMER_TIMEOUT_DIAG states=");
        serial_write_hex64(scheduler_task_state(timeout_id)); serial_write(",");
        serial_write_hex64(scheduler_task_state(sleep_id)); serial_write(",");
        serial_write_hex64(scheduler_task_state(cancel_id)); serial_write(",");
        serial_write_hex64(scheduler_task_state(controller_id));
        serial_write(" timeout="); serial_write_hex64(timer_test_timeout_completed);
        serial_write(" timeout_elapsed="); serial_write_hex64(timer_test_timeout_elapsed);
        serial_write(" sleep="); serial_write_hex64(timer_test_sleep_completed);
        serial_write(" sleep_elapsed="); serial_write_hex64(timer_test_sleep_elapsed);
        serial_write(" cancelled="); serial_write_hex64(timer_test_cancelled);
        serial_write(" resumed="); serial_write_hex64(timer_test_cancel_waiter_resumed);
        serial_write(" expirations="); serial_write_hex64(scheduler_timer_expiration_count());
        serial_write(" failed="); serial_write_hex64(scheduler_failed_count());
        serial_write("\n");
    }
    return waited && states_ok && results_ok && reaped;
}


static scheduler_step_result_t scheduler_lifecycle_probe_step(void *context) {
    if (context == NULL) return SCHEDULER_STEP_COMPLETE;
    ++*(uint32_t *)context;
    return SCHEDULER_STEP_COMPLETE;
}
bool scheduler_task_lifecycle_self_test(void) {
    scheduler_init();
    const uint64_t pages_before = memory_allocated_pages();
    uint32_t runs = 0U;
    scheduler_task_id_t first = 0U, second = 0U;
    if (!scheduler_create_task(scheduler_lifecycle_probe_step, &runs, &first)) return false;
    if (!scheduler_task_block(first) ||
        scheduler_task_state(first) != SCHEDULER_TASK_BLOCKED ||
        scheduler_run_once() ||
        !scheduler_task_wake(first) ||
        scheduler_task_state(first) != SCHEDULER_TASK_READY) return false;
    if (!scheduler_run_once() || runs != 1U ||
        scheduler_task_state(first) != SCHEDULER_TASK_EXITED) return false;
    if (!scheduler_task_reap(first) ||
        scheduler_task_state(first) != SCHEDULER_TASK_UNUSED) return false;
    runs = 0U;
    if (!scheduler_create_task(scheduler_lifecycle_probe_step, &runs, &second) ||
        second == first) return false;
    if (!scheduler_run_once() || runs != 1U ||
        scheduler_task_state(second) != SCHEDULER_TASK_EXITED) return false;
    if (!scheduler_task_reap(second) ||
        scheduler_task_state(second) != SCHEDULER_TASK_UNUSED) return false;
    return memory_allocated_pages() == pages_before && scheduler_ready_count() == 0U;
}

static void preemptive_test_a(void) {
    for (uint64_t i = 0U; i < SCHEDULER_TASK_TEST_ITERATIONS; ++i) {
        ++test_counter_a;
        __asm__ volatile ("pause");
    }
}
static void preemptive_test_b(void) {
    for (uint64_t i = 0U; i < SCHEDULER_TASK_TEST_ITERATIONS; ++i) {
        ++test_counter_b;
        __asm__ volatile ("pause");
    }
}

bool scheduler_preemptive_self_test(void) {
    scheduler_init();
    const uint64_t allocated_before = memory_allocated_pages();
    scheduler_task_id_t a = 0U, b = 0U;
    if (!scheduler_task_create(preemptive_test_a, &a)) return false;
    if (!scheduler_task_create(preemptive_test_b, &b)) {
        const uint32_t index = find_task_index(a);
        if (index < SCHEDULER_MAX_TASKS) task_table[index].state = SCHEDULER_TASK_FAILED;
        (void)scheduler_task_reap(a);
        return false;
    }
    preemptive_active = true;
    bootstrap_frame = NULL;
    current_task_index = SCHEDULER_NO_TASK;
    preemptive_ticks = 0U;
    preemptive_switches = 0U;
    preemptive_tick_limit = SCHEDULER_PREEMPTIVE_TEST_TICKS;
    const uint64_t target = interrupts_ticks() + SCHEDULER_PREEMPTIVE_TEST_TICKS;
    interrupts_enable();
    const bool waited = interrupts_wait_for_ticks(target);
    interrupts_disable();
    if (current_task_index == SCHEDULER_NO_TASK) preemptive_active = false;

    const bool ran = waited && !preemptive_active && bootstrap_frame == NULL &&
        scheduler_task_state(a) == SCHEDULER_TASK_EXITED &&
        scheduler_task_state(b) == SCHEDULER_TASK_EXITED &&
        test_counter_a >= SCHEDULER_TASK_TEST_ITERATIONS &&
        test_counter_b >= SCHEDULER_TASK_TEST_ITERATIONS &&
        preemptive_switches >= 1U;
    const bool reaped_a = scheduler_task_reap(a);
    const bool reaped_b = scheduler_task_reap(b);
    return ran && reaped_a && reaped_b &&
        scheduler_task_state(a) == SCHEDULER_TASK_UNUSED &&
        scheduler_task_state(b) == SCHEDULER_TASK_UNUSED &&
        memory_allocated_pages() == allocated_before;
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

#include "scheduler.h"
#include "interrupts.h"
#include "memory.h"
#include "console.h"
#include <stddef.h>
#include <stdint.h>

typedef struct {
    bool active;
    bool completed;
    bool expired;
    bool cancelled;
    bool waiting;
    scheduler_ipc_service_id_t source_service_id;
    scheduler_ipc_service_id_t destination_service_id;
    uint32_t request_id;
    uint32_t owner_task_index;
    scheduler_wait_queue_t waiters;
    scheduler_ipc_message_t response;
} scheduler_ipc_pending_call_t;

typedef struct {
    bool valid;
    scheduler_ipc_service_id_t source_service_id;
    scheduler_ipc_service_id_t destination_service_id;
    uint32_t request_id;
} scheduler_ipc_expired_call_t;

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
static scheduler_ipc_pending_call_t ipc_pending_calls[SCHEDULER_IPC_MAX_PENDING_CALLS];
static uint64_t ipc_diag_calls_started;
static uint64_t ipc_diag_calls_completed;
static uint64_t ipc_diag_calls_timed_out;
static uint64_t ipc_diag_calls_cancelled;
static uint64_t ipc_diag_calls_service_unavailable;
static uint64_t ipc_diag_calls_send_failed;
static uint64_t ipc_diag_replies_dropped;
static uint64_t ipc_diag_services_closed;
#define SCHEDULER_IPC_EXPIRED_HISTORY 16U
static scheduler_ipc_expired_call_t ipc_expired_calls[SCHEDULER_IPC_EXPIRED_HISTORY];
static uint32_t ipc_expired_history_cursor;
static uint32_t next_ipc_runtime_request_id = 1U;
static void scheduler_ipc_pending_owner_cancel_locked(uint32_t owner_index);
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

static void scheduler_ipc_expired_record_locked(
    scheduler_ipc_service_id_t source_service_id,
    scheduler_ipc_service_id_t destination_service_id,
    uint32_t request_id) {
    if (source_service_id == 0U || destination_service_id == 0U || request_id == 0U)
        return;
    scheduler_ipc_expired_call_t *entry =
        &ipc_expired_calls[ipc_expired_history_cursor % SCHEDULER_IPC_EXPIRED_HISTORY];
    entry->valid = true;
    entry->source_service_id = source_service_id;
    entry->destination_service_id = destination_service_id;
    entry->request_id = request_id;
    ipc_expired_history_cursor = (ipc_expired_history_cursor + 1U) % SCHEDULER_IPC_EXPIRED_HISTORY;
}

static bool scheduler_ipc_expired_match_locked(
    scheduler_ipc_service_id_t source_service_id,
    scheduler_ipc_service_id_t destination_service_id,
    uint32_t request_id) {
    for (uint32_t i = 0U; i < SCHEDULER_IPC_EXPIRED_HISTORY; ++i) {
        const scheduler_ipc_expired_call_t *entry = &ipc_expired_calls[i];
        if (entry->valid && entry->source_service_id == source_service_id &&
            entry->destination_service_id == destination_service_id &&
            entry->request_id == request_id) return true;
    }
    return false;
}

static void scheduler_ipc_pending_expire_task_locked(uint32_t task_index) {
    if (task_index >= SCHEDULER_MAX_TASKS) return;
    for (uint32_t i = 0U; i < SCHEDULER_IPC_MAX_PENDING_CALLS; ++i) {
        scheduler_ipc_pending_call_t *pending = &ipc_pending_calls[i];
        if (!pending->active || pending->completed || pending->cancelled ||
            pending->expired || pending->owner_task_index != task_index ||
            !pending->waiting) continue;
        pending->expired = true;
        ++ipc_diag_calls_timed_out;
        scheduler_ipc_expired_record_locked(pending->source_service_id,
            pending->destination_service_id, pending->request_id);
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
        scheduler_ipc_pending_expire_task_locked(i);
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
    scheduler_ipc_pending_owner_cancel_locked(index);
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

bool scheduler_semaphore_init(scheduler_semaphore_t *semaphore,
                               uint32_t initial_count, uint32_t max_count) {
    if (semaphore == NULL || max_count == 0U || initial_count > max_count) return false;
    const uint64_t flags = scheduler_irq_save_disable();
    semaphore->count = initial_count;
    semaphore->max_count = max_count;
    semaphore->waiters.waiters = 0U;
    scheduler_irq_restore(flags);
    return true;
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
        channel->messages[i].protocol_version = 0U;
        channel->messages[i].kind = SCHEDULER_IPC_KIND_DATA;
        channel->messages[i].status = SCHEDULER_IPC_STATUS_OK;
        channel->messages[i].source_service_id = 0U;
        channel->messages[i].request_id = 0U;
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
    destination->protocol_version = source->protocol_version;
    destination->kind = source->kind;
    destination->status = source->status;
    destination->source_service_id = source->source_service_id;
    destination->request_id = source->request_id;
    destination->delivery_id = source->delivery_id;
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
        if (task_table[index].wait_result == SCHEDULER_WAIT_CANCELLED ||
            task_table[index].wait_result == SCHEDULER_WAIT_TIMED_OUT) {
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
        if (task_table[index].wait_result == SCHEDULER_WAIT_CANCELLED ||
            task_table[index].wait_result == SCHEDULER_WAIT_TIMED_OUT) {
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
    if (!scheduler_semaphore_init(&semaphore_ipc_test_sem, 0U, 1U)) return false;
    scheduler_ipc_channel_init(&semaphore_ipc_test_channel);
    semaphore_ipc_test_sem_acquired = 0U;
    semaphore_ipc_test_sem_posted = 0U;
    semaphore_ipc_test_sent = 0U;
    semaphore_ipc_test_received = 0U;
    semaphore_ipc_test_errors = 0U;

    if (semaphore_ipc_test_sem.count != 0U ||
        semaphore_ipc_test_sem.max_count != 1U ||
        scheduler_semaphore_init(&semaphore_ipc_test_sem, 1U, 0U) ||
        scheduler_semaphore_init(&semaphore_ipc_test_sem, 2U, 1U) ||
        scheduler_semaphore_wait(NULL) ||
        scheduler_semaphore_post(NULL) ||
        scheduler_ipc_send(NULL, NULL) ||
        scheduler_ipc_receive(NULL, NULL)) return false;
    if (!scheduler_semaphore_post(&semaphore_ipc_test_sem) ||
        scheduler_semaphore_post(&semaphore_ipc_test_sem) ||
        !scheduler_semaphore_init(&semaphore_ipc_test_sem, 0U, 1U)) return false;
    scheduler_ipc_message_t invalid = {0};
    invalid.length = SCHEDULER_IPC_PAYLOAD_SIZE + 1U;
    if (scheduler_ipc_send(&semaphore_ipc_test_channel, &invalid)) return false;

    scheduler_task_id_t waiter = 0U, poster = 0U, sender = 0U, receiver = 0U;
    if (!scheduler_task_create(semaphore_ipc_test_waiter, &waiter) ||
        !scheduler_task_create(semaphore_ipc_test_poster, &poster) ||
        !scheduler_task_create(semaphore_ipc_test_receiver, &receiver) ||
        !scheduler_task_create(semaphore_ipc_test_sender, &sender)) return false;

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
        serial_write("\n");
    }
    return passed && reaped;
}


typedef struct {
    bool active;
    bool closing;
    scheduler_ipc_service_id_t id;
    uint32_t capabilities;
    uint32_t active_calls;
    scheduler_ipc_queue_policy_t queue_policy;
    uint32_t queue_high_watermark;
    uint32_t priority;
    uint32_t congestion_high_watermark;
    uint32_t congestion_low_watermark;
    bool congestion_control_enabled;
    bool congested;
    uint64_t messages_enqueued;
    uint64_t messages_received;
    uint64_t backpressure_events;
    uint64_t messages_dropped_newest;
    uint64_t messages_dropped_oldest;
    uint64_t messages_rejected_full;
    scheduler_ipc_circuit_state_t circuit_state;
    scheduler_ipc_health_state_t health_state;
    uint32_t breaker_failure_threshold;
    uint32_t consecutive_failures;
    uint64_t breaker_reset_timeout_ticks;
    uint64_t breaker_open_until_tick;
    uint64_t health_total_successes;
    uint64_t health_total_failures;
    uint64_t health_last_success_tick;
    uint64_t health_last_failure_tick;
    bool half_open_probe_in_flight;
    uint32_t received_delivery_ids[SCHEDULER_IPC_DELIVERY_HISTORY];
    uint32_t received_delivery_cursor;
    char name[SCHEDULER_IPC_SERVICE_NAME_SIZE];
    scheduler_ipc_channel_t channel;
} scheduler_ipc_service_t;

typedef struct {
    bool valid;
    bool timer_driven;
    bool backoff_enabled;
    bool dead_lettered;
    scheduler_ipc_delivery_receipt_t receipt;
    scheduler_ipc_message_t message;
} scheduler_ipc_delivery_track_t;

static scheduler_ipc_service_t ipc_services[SCHEDULER_IPC_MAX_SERVICES];
static scheduler_ipc_delivery_track_t ipc_delivery_tracks[SCHEDULER_IPC_MAX_TRACKED_DELIVERIES];
static scheduler_ipc_service_id_t next_ipc_service_id = 1U;
static uint64_t ipc_diag_messages_enqueued;
static uint64_t ipc_diag_messages_received;
static uint64_t ipc_diag_backpressure_events;
static uint64_t ipc_diag_messages_dropped_newest;
static uint64_t ipc_diag_messages_dropped_oldest;
static uint64_t ipc_diag_messages_rejected_full;
static uint64_t ipc_diag_delivery_failures;
static uint32_t ipc_diag_max_queue_depth;
static uint64_t ipc_diag_deliveries_started;
static uint64_t ipc_diag_deliveries_acknowledged;
static uint64_t ipc_diag_delivery_retry_attempts;
static uint64_t ipc_diag_delivery_retries_exhausted;
static uint64_t ipc_diag_duplicate_deliveries_suppressed;
static uint64_t ipc_diag_automatic_retry_attempts;
static uint64_t ipc_diag_delivery_deadlines_expired;
static uint64_t ipc_diag_dead_letters_enqueued;
static uint64_t ipc_diag_dead_letters_dropped;
static uint64_t ipc_diag_service_health_successes;
static uint64_t ipc_diag_service_health_failures;
static uint64_t ipc_diag_circuit_breaker_trips;
static uint64_t ipc_diag_circuit_breaker_recoveries;
static uint64_t ipc_diag_circuit_open_rejections;
static scheduler_ipc_dead_letter_t ipc_dead_letters[SCHEDULER_IPC_DEAD_LETTER_CAPACITY];
static uint32_t ipc_dead_letter_head;
static uint32_t ipc_dead_letter_count;


static uint32_t scheduler_ipc_name_length(const char *name) {
    if (name == NULL) return 0U;
    uint32_t length = 0U;
    while (length < SCHEDULER_IPC_SERVICE_NAME_SIZE && name[length] != '\0')
        ++length;
    return length > 0U && length < SCHEDULER_IPC_SERVICE_NAME_SIZE ? length : 0U;
}

static bool scheduler_ipc_name_equal(const char *left, const char *right) {
    for (uint32_t i = 0U; i < SCHEDULER_IPC_SERVICE_NAME_SIZE; ++i) {
        if (left[i] != right[i]) return false;
        if (left[i] == '\0') return true;
    }
    return false;
}

static scheduler_ipc_service_t *scheduler_ipc_find_service_locked(
    scheduler_ipc_service_id_t service_id) {
    if (service_id == 0U) return NULL;
    for (uint32_t i = 0U; i < SCHEDULER_IPC_MAX_SERVICES; ++i)
        if (ipc_services[i].active && ipc_services[i].id == service_id)
            return &ipc_services[i];
    return NULL;
}

static void scheduler_ipc_service_finalize_locked(scheduler_ipc_service_t *service) {
    if (service == NULL || !service->active || !service->closing ||
        service->active_calls != 0U || service->channel.readers.waiters != 0U ||
        service->channel.writers.waiters != 0U) return;
    service->active = false;
    service->closing = false;
    service->id = 0U;
    service->capabilities = 0U;
    service->active_calls = 0U;
    service->queue_policy = SCHEDULER_IPC_QUEUE_BLOCK;
    service->queue_high_watermark = 0U;
    service->messages_enqueued = 0U;
    service->messages_received = 0U;
    service->backpressure_events = 0U;
    service->messages_dropped_newest = 0U;
    service->messages_dropped_oldest = 0U;
    service->messages_rejected_full = 0U;
    service->circuit_state = SCHEDULER_IPC_CIRCUIT_CLOSED;
    service->health_state = SCHEDULER_IPC_HEALTHY;
    service->breaker_failure_threshold = 0U;
    service->consecutive_failures = 0U;
    service->breaker_reset_timeout_ticks = 0U;
    service->breaker_open_until_tick = 0U;
    service->health_total_successes = 0U;
    service->health_total_failures = 0U;
    service->health_last_success_tick = 0U;
    service->health_last_failure_tick = 0U;
    service->half_open_probe_in_flight = false;
    for (uint32_t i = 0U; i < SCHEDULER_IPC_DELIVERY_HISTORY; ++i)
        service->received_delivery_ids[i] = 0U;
    service->received_delivery_cursor = 0U;
    service->channel.head = 0U;
    service->channel.tail = 0U;
    service->channel.count = 0U;
    service->channel.readers.waiters = 0U;
    service->channel.writers.waiters = 0U;
    for (uint32_t i = 0U; i < SCHEDULER_IPC_SERVICE_NAME_SIZE; ++i)
        service->name[i] = '\0';
}

static void scheduler_ipc_service_release_ref_locked(scheduler_ipc_service_t *service) {
    if (service == NULL) return;
    if (service->active_calls > 0U) --service->active_calls;
    scheduler_ipc_service_finalize_locked(service);
}

static void scheduler_ipc_service_cancel_waiters_locked(scheduler_ipc_service_t *service) {
    if (service == NULL) return;
    for (uint32_t i = 0U; i < SCHEDULER_MAX_TASKS; ++i) {
        scheduler_task_t *task = &task_table[i];
        if (task->state != SCHEDULER_TASK_BLOCKED ||
            (task->waiting_on != &service->channel.readers &&
             task->waiting_on != &service->channel.writers)) continue;
        scheduler_remove_waiter_locked(i);
        task->has_deadline = false;
        task->wait_deadline = 0U;
        task->wait_result = SCHEDULER_WAIT_CANCELLED;
        task->state = SCHEDULER_TASK_READY;
    }
}

static void scheduler_ipc_pending_release_locked(uint32_t index) {
    if (index >= SCHEDULER_IPC_MAX_PENDING_CALLS) return;
    scheduler_ipc_pending_call_t *pending = &ipc_pending_calls[index];
    if (!pending->active) return;
    if (pending->expired || pending->cancelled) {
        scheduler_ipc_expired_record_locked(pending->source_service_id,
            pending->destination_service_id, pending->request_id);
    }
    scheduler_ipc_service_t *source =
        scheduler_ipc_find_service_locked(pending->source_service_id);
    scheduler_ipc_service_t *destination =
        scheduler_ipc_find_service_locked(pending->destination_service_id);
    if (source != NULL) scheduler_ipc_service_release_ref_locked(source);
    if (destination != NULL) scheduler_ipc_service_release_ref_locked(destination);
    pending->active = false;
    pending->completed = false;
    pending->expired = false;
    pending->cancelled = false;
    pending->waiting = false;
    pending->source_service_id = 0U;
    pending->destination_service_id = 0U;
    pending->request_id = 0U;
    pending->owner_task_index = SCHEDULER_NO_TASK;
    pending->waiters.waiters = 0U;
    pending->response = (scheduler_ipc_message_t){0};
}

static void scheduler_ipc_pending_owner_cancel_locked(uint32_t owner_index) {
    if (owner_index >= SCHEDULER_MAX_TASKS) return;
    for (uint32_t i = 0U; i < SCHEDULER_IPC_MAX_PENDING_CALLS; ++i) {
        scheduler_ipc_pending_call_t *pending = &ipc_pending_calls[i];
        if (!pending->active || pending->owner_task_index != owner_index) continue;
        pending->cancelled = true;
        pending->expired = true;
        ++ipc_diag_calls_cancelled;
        scheduler_ipc_expired_record_locked(pending->source_service_id,
            pending->destination_service_id, pending->request_id);
        scheduler_ipc_pending_release_locked(i);
    }
}

bool scheduler_ipc_service_register(const char *name,
                                    scheduler_ipc_service_id_t *service_id) {
    return scheduler_ipc_service_register_ex(name,
        SCHEDULER_IPC_SERVICE_CAP_ONE_WAY |
        SCHEDULER_IPC_SERVICE_CAP_REQUEST_RESPONSE |
        SCHEDULER_IPC_SERVICE_CAP_EVENTS, service_id);
}

bool scheduler_ipc_service_register_ex(const char *name, uint32_t capabilities,
                                       scheduler_ipc_service_id_t *service_id) {
    const uint32_t name_length = scheduler_ipc_name_length(name);
    if (name_length == 0U || service_id == NULL ||
        (capabilities & ~SCHEDULER_IPC_SERVICE_CAP_ALL) != 0U) return false;
    const uint64_t flags = scheduler_irq_save_disable();
    scheduler_ipc_service_t *free_slot = NULL;
    for (uint32_t i = 0U; i < SCHEDULER_IPC_MAX_SERVICES; ++i) {
        scheduler_ipc_service_t *service = &ipc_services[i];
        if (service->active && scheduler_ipc_name_equal(service->name, name)) {
            scheduler_irq_restore(flags);
            return false;
        }
        if (!service->active && service->active_calls == 0U && free_slot == NULL)
            free_slot = service;
    }
    if (free_slot == NULL || next_ipc_service_id == 0U) {
        scheduler_irq_restore(flags);
        return false;
    }
    free_slot->active = false;
    free_slot->closing = false;
    free_slot->id = next_ipc_service_id++;
    free_slot->capabilities = capabilities;
    free_slot->active_calls = 0U;
    free_slot->queue_policy = SCHEDULER_IPC_QUEUE_BLOCK;
    free_slot->queue_high_watermark = 0U;
    free_slot->priority = 1U;
    free_slot->congestion_high_watermark = 0U;
    free_slot->congestion_low_watermark = 0U;
    free_slot->congestion_control_enabled = false;
    free_slot->congested = false;
    free_slot->messages_enqueued = 0U;
    free_slot->messages_received = 0U;
    free_slot->backpressure_events = 0U;
    free_slot->messages_dropped_newest = 0U;
    free_slot->messages_dropped_oldest = 0U;
    free_slot->messages_rejected_full = 0U;
    free_slot->circuit_state = SCHEDULER_IPC_CIRCUIT_CLOSED;
    free_slot->health_state = SCHEDULER_IPC_HEALTHY;
    free_slot->breaker_failure_threshold = 0U;
    free_slot->consecutive_failures = 0U;
    free_slot->breaker_reset_timeout_ticks = 0U;
    free_slot->breaker_open_until_tick = 0U;
    free_slot->health_total_successes = 0U;
    free_slot->health_total_failures = 0U;
    free_slot->health_last_success_tick = 0U;
    free_slot->health_last_failure_tick = 0U;
    free_slot->half_open_probe_in_flight = false;
    for (uint32_t i = 0U; i < SCHEDULER_IPC_DELIVERY_HISTORY; ++i)
        free_slot->received_delivery_ids[i] = 0U;
    free_slot->received_delivery_cursor = 0U;
    for (uint32_t i = 0U; i < SCHEDULER_IPC_SERVICE_NAME_SIZE; ++i)
        free_slot->name[i] = '\0';
    for (uint32_t i = 0U; i < name_length; ++i)
        free_slot->name[i] = name[i];
    scheduler_ipc_channel_init(&free_slot->channel);
    free_slot->active = true;
    *service_id = free_slot->id;
    scheduler_irq_restore(flags);
    return true;
}

bool scheduler_ipc_service_lookup(const char *name,
                                  scheduler_ipc_service_id_t *service_id) {
    if (scheduler_ipc_name_length(name) == 0U || service_id == NULL) return false;
    const uint64_t flags = scheduler_irq_save_disable();
    for (uint32_t i = 0U; i < SCHEDULER_IPC_MAX_SERVICES; ++i) {
        scheduler_ipc_service_t *service = &ipc_services[i];
        if (service->active && !service->closing &&
            scheduler_ipc_name_equal(service->name, name)) {
            *service_id = service->id;
            scheduler_irq_restore(flags);
            return true;
        }
    }
    scheduler_irq_restore(flags);
    return false;
}


static void scheduler_ipc_service_info_copy_locked(const scheduler_ipc_service_t *service,
                                                   scheduler_ipc_service_info_t *info) {
    info->id = service->id;
    info->capabilities = service->capabilities;
    info->queued_messages = service->channel.count;
    info->queue_capacity = SCHEDULER_IPC_CAPACITY;
    info->queue_policy = service->queue_policy;
    info->queue_high_watermark = service->queue_high_watermark;
    info->priority = service->priority;
    info->congested = service->congested;
    info->messages_enqueued = service->messages_enqueued;
    info->messages_received = service->messages_received;
    info->backpressure_events = service->backpressure_events;
    info->messages_dropped_newest = service->messages_dropped_newest;
    info->messages_dropped_oldest = service->messages_dropped_oldest;
    info->messages_rejected_full = service->messages_rejected_full;
    for (uint32_t i = 0U; i < SCHEDULER_IPC_SERVICE_NAME_SIZE; ++i)
        info->name[i] = service->name[i];
}

bool scheduler_ipc_service_enumerate(uint32_t active_index,
                                     scheduler_ipc_service_info_t *info) {
    if (info == NULL) return false;
    const uint64_t flags = scheduler_irq_save_disable();
    uint32_t current = 0U;
    for (uint32_t i = 0U; i < SCHEDULER_IPC_MAX_SERVICES; ++i) {
        if (!ipc_services[i].active || ipc_services[i].closing) continue;
        if (current++ != active_index) continue;
        scheduler_ipc_service_info_copy_locked(&ipc_services[i], info);
        scheduler_irq_restore(flags);
        return true;
    }
    scheduler_irq_restore(flags);
    return false;
}

bool scheduler_ipc_service_get_info(scheduler_ipc_service_id_t service_id,
                                    scheduler_ipc_service_info_t *info) {
    if (info == NULL) return false;
    const uint64_t flags = scheduler_irq_save_disable();
    scheduler_ipc_service_t *service = scheduler_ipc_find_service_locked(service_id);
    if (service == NULL || service->closing) {
        scheduler_irq_restore(flags);
        return false;
    }
    scheduler_ipc_service_info_copy_locked(service, info);
    scheduler_irq_restore(flags);
    return true;
}

bool scheduler_ipc_service_set_queue_policy(scheduler_ipc_service_id_t service_id,
                                             scheduler_ipc_queue_policy_t policy) {
    if ((uint32_t)policy > (uint32_t)SCHEDULER_IPC_QUEUE_DROP_OLDEST)
        return false;
    const uint64_t flags = scheduler_irq_save_disable();
    scheduler_ipc_service_t *service = scheduler_ipc_find_service_locked(service_id);
    if (service == NULL || service->closing) {
        scheduler_irq_restore(flags);
        return false;
    }
    service->queue_policy = policy;
    scheduler_irq_restore(flags);
    return true;
}


bool scheduler_ipc_service_set_priority(scheduler_ipc_service_id_t service_id, uint32_t priority) {
    if (priority < SCHEDULER_IPC_PRIORITY_MIN || priority > SCHEDULER_IPC_PRIORITY_MAX)
        return false;
    const uint64_t flags = scheduler_irq_save_disable();
    scheduler_ipc_service_t *service = scheduler_ipc_find_service_locked(service_id);
    if (service == NULL || service->closing) {
        scheduler_irq_restore(flags);
        return false;
    }
    service->priority = priority;
    scheduler_irq_restore(flags);
    return true;
}

bool scheduler_ipc_service_set_congestion_control(scheduler_ipc_service_id_t service_id,
                                                    uint32_t high_watermark,
                                                    uint32_t low_watermark) {
    if (high_watermark == 0U || high_watermark > SCHEDULER_IPC_CAPACITY ||
        low_watermark >= high_watermark) return false;
    const uint64_t flags = scheduler_irq_save_disable();
    scheduler_ipc_service_t *service = scheduler_ipc_find_service_locked(service_id);
    if (service == NULL || service->closing) {
        scheduler_irq_restore(flags);
        return false;
    }
    service->congestion_high_watermark = high_watermark;
    service->congestion_low_watermark = low_watermark;
    service->congestion_control_enabled = true;
    service->congested = service->channel.count >= high_watermark;
    scheduler_irq_restore(flags);
    return true;
}

/* Lower weighted queue occupancy wins; priority 8 has eight times the
 * effective capacity of priority 1. Ties rotate across the candidate list. */
bool scheduler_ipc_service_select_adaptive(const scheduler_ipc_service_id_t *candidates,
                                           uint32_t candidate_count,
                                           scheduler_ipc_service_id_t *selected_service_id) {
    if (candidates == NULL || selected_service_id == NULL ||
        candidate_count == 0U || candidate_count > SCHEDULER_IPC_MAX_SERVICES)
        return false;
    const uint64_t flags = scheduler_irq_save_disable();
    bool found = false;
    uint64_t best_score = UINT64_MAX;
    scheduler_ipc_service_id_t best_id = 0U;
    static uint32_t tie_cursor;
    for (uint32_t n = 0U; n < candidate_count; ++n) {
        const uint32_t i = (tie_cursor + n) % candidate_count;
        scheduler_ipc_service_t *service = scheduler_ipc_find_service_locked(candidates[i]);
        if (service == NULL || service->closing || service->congested ||
            service->circuit_state == SCHEDULER_IPC_CIRCUIT_OPEN ||
            service->channel.count > SCHEDULER_IPC_CAPACITY) continue;
        const uint64_t score = ((uint64_t)service->channel.count * UINT64_C(1000)) /
                               (uint64_t)service->priority;
        if (!found || score < best_score) {
            found = true;
            best_score = score;
            best_id = service->id;
        }
    }
    if (found) {
        *selected_service_id = best_id;
        for (uint32_t i = 0U; i < candidate_count; ++i) {
            if (candidates[i] == best_id) {
                tie_cursor = (i + 1U) % candidate_count;
                break;
            }
        }
    }
    scheduler_irq_restore(flags);
    return found;
}

bool scheduler_ipc_service_set_circuit_breaker(scheduler_ipc_service_id_t service_id,
                                                uint32_t failure_threshold,
                                                uint64_t reset_timeout_ticks) {
    if ((failure_threshold == 0U) != (reset_timeout_ticks == 0U)) return false;
    const uint64_t flags = scheduler_irq_save_disable();
    scheduler_ipc_service_t *service = scheduler_ipc_find_service_locked(service_id);
    if (service == NULL || service->closing) {
        scheduler_irq_restore(flags);
        return false;
    }
    service->breaker_failure_threshold = failure_threshold;
    service->breaker_reset_timeout_ticks = reset_timeout_ticks;
    service->consecutive_failures = 0U;
    service->circuit_state = SCHEDULER_IPC_CIRCUIT_CLOSED;
    service->health_state = SCHEDULER_IPC_HEALTHY;
    service->breaker_open_until_tick = 0U;
    service->half_open_probe_in_flight = false;
    scheduler_irq_restore(flags);
    return true;
}

bool scheduler_ipc_service_get_health(scheduler_ipc_service_id_t service_id,
                                      scheduler_ipc_service_health_t *health) {
    if (health == NULL) return false;
    const uint64_t flags = scheduler_irq_save_disable();
    scheduler_ipc_service_t *service = scheduler_ipc_find_service_locked(service_id);
    if (service == NULL || service->closing) {
        scheduler_irq_restore(flags);
        return false;
    }
    health->service_id = service->id;
    health->circuit_state = service->circuit_state;
    health->health_state = service->health_state;
    health->failure_threshold = service->breaker_failure_threshold;
    health->consecutive_failures = service->consecutive_failures;
    health->reset_timeout_ticks = service->breaker_reset_timeout_ticks;
    health->open_until_tick = service->breaker_open_until_tick;
    health->total_successes = service->health_total_successes;
    health->total_failures = service->health_total_failures;
    health->last_success_tick = service->health_last_success_tick;
    health->last_failure_tick = service->health_last_failure_tick;
    scheduler_irq_restore(flags);
    return true;
}

bool scheduler_ipc_service_unregister(scheduler_ipc_service_id_t service_id) {
    if (service_id == 0U) return false;
    const uint64_t flags = scheduler_irq_save_disable();
    scheduler_ipc_service_t *service = scheduler_ipc_find_service_locked(service_id);
    if (service == NULL) {
        scheduler_irq_restore(flags);
        return false;
    }
    if (!service->closing) {
        service->closing = true;
        ++ipc_diag_services_closed;
        service->channel.head = 0U;
        service->channel.tail = 0U;
        service->channel.count = 0U;
        scheduler_ipc_service_cancel_waiters_locked(service);

        /* Complete in-flight calls touching this endpoint with a typed NOT_FOUND. */
        for (uint32_t i = 0U; i < SCHEDULER_IPC_MAX_PENDING_CALLS; ++i) {
            scheduler_ipc_pending_call_t *pending = &ipc_pending_calls[i];
            if (!pending->active || pending->completed || pending->expired ||
                pending->cancelled ||
                (pending->source_service_id != service_id &&
                 pending->destination_service_id != service_id)) continue;
            pending->response = (scheduler_ipc_message_t){0};
            pending->response.protocol_version = SCHEDULER_IPC_PROTOCOL_VERSION;
            pending->response.kind = SCHEDULER_IPC_KIND_ERROR;
            pending->response.status = SCHEDULER_IPC_STATUS_NOT_FOUND;
            pending->response.source_service_id = pending->destination_service_id;
            pending->response.request_id = pending->request_id;
            pending->completed = true;
            ++ipc_diag_calls_service_unavailable;
            if (pending->waiting && pending->owner_task_index < SCHEDULER_MAX_TASKS) {
                scheduler_task_t *owner = &task_table[pending->owner_task_index];
                if (owner->state == SCHEDULER_TASK_BLOCKED &&
                    owner->waiting_on == &pending->waiters)
                    (void)wait_queue_wake_one_locked(&pending->waiters);
            }
        }
    }
    scheduler_ipc_service_finalize_locked(service);
    scheduler_irq_restore(flags);
    /* True means closure was accepted; existing users release the slot as they unwind. */
    return true;
}

static void scheduler_ipc_record_enqueue_locked(scheduler_ipc_service_t *service) {
    if (service == NULL) return;
    ++service->messages_enqueued;
    ++ipc_diag_messages_enqueued;
    if (service->channel.count > service->queue_high_watermark)
        service->queue_high_watermark = service->channel.count;
    if (service->channel.count > ipc_diag_max_queue_depth)
        ipc_diag_max_queue_depth = service->channel.count;
}

static bool scheduler_ipc_delivery_accepted(scheduler_ipc_delivery_result_t result) {
    return result == SCHEDULER_IPC_DELIVERY_DELIVERED ||
           result == SCHEDULER_IPC_DELIVERY_DELIVERED_DROPPED_OLDEST;
}

static scheduler_ipc_delivery_track_t *scheduler_ipc_find_delivery_locked(uint32_t delivery_id) {
    if (delivery_id == 0U) return NULL;
    for (uint32_t i = 0U; i < SCHEDULER_IPC_MAX_TRACKED_DELIVERIES; ++i)
        if (ipc_delivery_tracks[i].valid &&
            ipc_delivery_tracks[i].receipt.delivery_id == delivery_id)
            return &ipc_delivery_tracks[i];
    return NULL;
}

static bool scheduler_ipc_service_has_received_delivery_locked(
    const scheduler_ipc_service_t *service, uint32_t delivery_id) {
    if (service == NULL || delivery_id == 0U) return false;
    for (uint32_t i = 0U; i < SCHEDULER_IPC_DELIVERY_HISTORY; ++i)
        if (service->received_delivery_ids[i] == delivery_id) return true;
    return false;
}

static bool scheduler_ipc_service_has_queued_delivery_locked(
    const scheduler_ipc_service_t *service, uint32_t delivery_id) {
    if (service == NULL || delivery_id == 0U ||
        service->channel.count > SCHEDULER_IPC_CAPACITY) return false;
    for (uint32_t i = 0U; i < service->channel.count; ++i) {
        const uint32_t index = (service->channel.head + i) % SCHEDULER_IPC_CAPACITY;
        if (service->channel.messages[index].delivery_id == delivery_id) return true;
    }
    return false;
}

static bool scheduler_ipc_delivery_already_seen_locked(
    const scheduler_ipc_service_t *service, uint32_t delivery_id) {
    return scheduler_ipc_service_has_queued_delivery_locked(service, delivery_id) ||
           scheduler_ipc_service_has_received_delivery_locked(service, delivery_id);
}

static uint64_t scheduler_ipc_tick_add_saturating(uint64_t tick, uint64_t delta) {
    return UINT64_MAX - tick < delta ? UINT64_MAX : tick + delta;
}

static bool scheduler_ipc_circuit_admit_locked(scheduler_ipc_service_t *service,
                                                uint64_t now) {
    if (service == NULL || service->breaker_failure_threshold == 0U) return true;
    if (service->circuit_state == SCHEDULER_IPC_CIRCUIT_OPEN) {
        if (now < service->breaker_open_until_tick) {
            ++ipc_diag_circuit_open_rejections;
            return false;
        }
        service->circuit_state = SCHEDULER_IPC_CIRCUIT_HALF_OPEN;
        service->health_state = SCHEDULER_IPC_HEALTH_DEGRADED;
        service->half_open_probe_in_flight = false;
    }
    if (service->circuit_state == SCHEDULER_IPC_CIRCUIT_HALF_OPEN) {
        if (service->half_open_probe_in_flight) {
            ++ipc_diag_circuit_open_rejections;
            return false;
        }
        service->half_open_probe_in_flight = true;
    }
    return true;
}

static void scheduler_ipc_service_report_locked(scheduler_ipc_service_t *service,
                                                bool success, uint64_t now) {
    if (service == NULL) return;
    if (success) {
        ++service->health_total_successes;
        ++ipc_diag_service_health_successes;
        service->health_last_success_tick = now;
        service->consecutive_failures = 0U;
        service->health_state = SCHEDULER_IPC_HEALTHY;
        if (service->circuit_state != SCHEDULER_IPC_CIRCUIT_CLOSED) {
            ++ipc_diag_circuit_breaker_recoveries;
            service->circuit_state = SCHEDULER_IPC_CIRCUIT_CLOSED;
        }
        service->breaker_open_until_tick = 0U;
        service->half_open_probe_in_flight = false;
        return;
    }
    ++service->health_total_failures;
    ++ipc_diag_service_health_failures;
    service->health_last_failure_tick = now;
    if (service->consecutive_failures != UINT32_MAX) ++service->consecutive_failures;
    service->health_state = SCHEDULER_IPC_HEALTH_DEGRADED;
    const bool half_open_failure = service->circuit_state == SCHEDULER_IPC_CIRCUIT_HALF_OPEN;
    if (service->breaker_failure_threshold != 0U &&
        (half_open_failure || service->consecutive_failures >= service->breaker_failure_threshold)) {
        service->circuit_state = SCHEDULER_IPC_CIRCUIT_OPEN;
        service->breaker_open_until_tick = scheduler_ipc_tick_add_saturating(
            now, service->breaker_reset_timeout_ticks);
        service->half_open_probe_in_flight = false;
        service->health_state = SCHEDULER_IPC_HEALTH_UNAVAILABLE;
        ++ipc_diag_circuit_breaker_trips;
    }
}

/* Caller holds local interrupt exclusion; the queue is intentionally bounded. */
static void scheduler_ipc_dead_letter_enqueue_locked(
    scheduler_ipc_delivery_track_t *track,
    scheduler_ipc_delivery_result_t reason, uint64_t now) {
    if (track == NULL || !track->valid || track->dead_lettered) return;
    if (ipc_dead_letter_count == SCHEDULER_IPC_DEAD_LETTER_CAPACITY) {
        ipc_dead_letter_head = (ipc_dead_letter_head + 1U) % SCHEDULER_IPC_DEAD_LETTER_CAPACITY;
        --ipc_dead_letter_count;
        ++ipc_diag_dead_letters_dropped;
    }
    const uint32_t index = (ipc_dead_letter_head + ipc_dead_letter_count) %
                           SCHEDULER_IPC_DEAD_LETTER_CAPACITY;
    ipc_dead_letters[index].receipt = track->receipt;
    ipc_dead_letters[index].receipt.last_result = reason;
    scheduler_ipc_message_copy(&ipc_dead_letters[index].message, &track->message);
    ipc_dead_letters[index].reason = reason;
    ipc_dead_letters[index].recorded_tick = now;
    ++ipc_dead_letter_count;
    ++ipc_diag_dead_letters_enqueued;
    track->dead_lettered = true;
}

scheduler_ipc_delivery_result_t scheduler_ipc_service_send_ex(
    scheduler_ipc_service_id_t source_service_id,
    scheduler_ipc_service_id_t destination_service_id,
    uint32_t request_id,
    const scheduler_ipc_message_t *message) {
    if (source_service_id == 0U || destination_service_id == 0U ||
        !scheduler_ipc_message_valid(message))
        return SCHEDULER_IPC_DELIVERY_INVALID_ARGUMENT;

    const uint64_t flags = scheduler_irq_save_disable();
    scheduler_ipc_service_t *source = scheduler_ipc_find_service_locked(source_service_id);
    scheduler_ipc_service_t *destination =
        scheduler_ipc_find_service_locked(destination_service_id);
    if (source == NULL || destination == NULL) {
        ++ipc_diag_delivery_failures;
        scheduler_irq_restore(flags);
        return SCHEDULER_IPC_DELIVERY_SERVICE_NOT_FOUND;
    }
    if (source->closing || destination->closing) {
        ++ipc_diag_delivery_failures;
        scheduler_ipc_service_report_locked(destination, false, interrupts_ticks());
        scheduler_irq_restore(flags);
        return SCHEDULER_IPC_DELIVERY_SERVICE_CLOSED;
    }
    if (!scheduler_ipc_circuit_admit_locked(destination, interrupts_ticks())) {
        scheduler_irq_restore(flags);
        return SCHEDULER_IPC_DELIVERY_CIRCUIT_OPEN;
    }

    scheduler_ipc_channel_t *channel = &destination->channel;
    if (destination->congestion_control_enabled && destination->congested) {
        ++destination->backpressure_events;
        ++ipc_diag_backpressure_events;
        scheduler_irq_restore(flags);
        return SCHEDULER_IPC_DELIVERY_BACKPRESSURE;
    }
    if (channel->count > SCHEDULER_IPC_CAPACITY ||
        channel->head >= SCHEDULER_IPC_CAPACITY ||
        channel->tail >= SCHEDULER_IPC_CAPACITY) {
        ++ipc_diag_delivery_failures;
        scheduler_ipc_service_report_locked(destination, false, interrupts_ticks());
        scheduler_irq_restore(flags);
        return SCHEDULER_IPC_DELIVERY_INTERNAL_ERROR;
    }

    scheduler_ipc_delivery_result_t result = SCHEDULER_IPC_DELIVERY_DELIVERED;
    if (channel->count == SCHEDULER_IPC_CAPACITY) {
        ++destination->backpressure_events;
        ++ipc_diag_backpressure_events;
        if (destination->queue_policy == SCHEDULER_IPC_QUEUE_DROP_NEWEST) {
            ++destination->messages_dropped_newest;
            ++ipc_diag_messages_dropped_newest;
            scheduler_ipc_service_report_locked(destination, false, interrupts_ticks());
            scheduler_irq_restore(flags);
            return SCHEDULER_IPC_DELIVERY_DROPPED_NEWEST;
        }
        if (destination->queue_policy == SCHEDULER_IPC_QUEUE_DROP_OLDEST) {
            channel->head = (channel->head + 1U) % SCHEDULER_IPC_CAPACITY;
            --channel->count;
            ++destination->messages_dropped_oldest;
            ++ipc_diag_messages_dropped_oldest;
            result = SCHEDULER_IPC_DELIVERY_DELIVERED_DROPPED_OLDEST;
        } else {
            ++destination->messages_rejected_full;
            ++ipc_diag_messages_rejected_full;
            scheduler_ipc_service_report_locked(destination, false, interrupts_ticks());
            scheduler_irq_restore(flags);
            return SCHEDULER_IPC_DELIVERY_BACKPRESSURE;
        }
    }

    scheduler_ipc_message_t forwarded;
    scheduler_ipc_message_copy(&forwarded, message);
    forwarded.source_service_id = source_service_id;
    forwarded.request_id = request_id;
    scheduler_ipc_message_copy(&channel->messages[channel->tail], &forwarded);
    channel->tail = (channel->tail + 1U) % SCHEDULER_IPC_CAPACITY;
    ++channel->count;
    scheduler_ipc_record_enqueue_locked(destination);
    if (destination->congestion_control_enabled &&
        channel->count >= destination->congestion_high_watermark)
        destination->congested = true;
    if (channel->readers.waiters != 0U)
        (void)wait_queue_wake_one_locked(&channel->readers);
    scheduler_ipc_service_report_locked(destination, true, interrupts_ticks());
    scheduler_irq_restore(flags);
    return result;
}

bool scheduler_ipc_service_send(scheduler_ipc_service_id_t source_service_id,
                                scheduler_ipc_service_id_t destination_service_id,
                                uint32_t request_id,
                                const scheduler_ipc_message_t *message) {
    if (!scheduler_ipc_message_valid(message)) return false;
    const uint64_t flags = scheduler_irq_save_disable();
    scheduler_ipc_service_t *source =
        scheduler_ipc_find_service_locked(source_service_id);
    scheduler_ipc_service_t *destination =
        scheduler_ipc_find_service_locked(destination_service_id);
    if (source == NULL || destination == NULL || source->closing ||
        destination->closing || source->active_calls == UINT32_MAX ||
        destination->active_calls == UINT32_MAX) {
        scheduler_irq_restore(flags);
        return false;
    }
    if (destination->queue_policy != SCHEDULER_IPC_QUEUE_BLOCK) {
        scheduler_irq_restore(flags);
        return scheduler_ipc_delivery_accepted(
            scheduler_ipc_service_send_ex(source_service_id, destination_service_id,
                                          request_id, message));
    }
    ++source->active_calls;
    ++destination->active_calls;
    scheduler_ipc_channel_t *channel = &destination->channel;
    scheduler_irq_restore(flags);

    scheduler_ipc_message_t forwarded;
    scheduler_ipc_message_copy(&forwarded, message);
    forwarded.source_service_id = source_service_id;
    forwarded.request_id = request_id;
    const bool sent = scheduler_ipc_send(channel, &forwarded);

    const uint64_t finish_flags = scheduler_irq_save_disable();
    if (sent) scheduler_ipc_record_enqueue_locked(destination);
    else ++ipc_diag_delivery_failures;
    scheduler_ipc_service_release_ref_locked(source);
    scheduler_ipc_service_release_ref_locked(destination);
    scheduler_irq_restore(finish_flags);
    return sent;
}

bool scheduler_ipc_service_receive(scheduler_ipc_service_id_t service_id,
                                   scheduler_ipc_message_t *message) {
    if (message == NULL) return false;
    const uint64_t flags = scheduler_irq_save_disable();
    scheduler_ipc_service_t *service = scheduler_ipc_find_service_locked(service_id);
    if (service == NULL || service->closing || service->active_calls == UINT32_MAX) {
        scheduler_irq_restore(flags);
        return false;
    }
    ++service->active_calls;
    scheduler_ipc_channel_t *channel = &service->channel;
    scheduler_irq_restore(flags);

    const bool received = scheduler_ipc_receive(channel, message);

    const uint64_t finish_flags = scheduler_irq_save_disable();
    if (received) {
        ++service->messages_received;
        ++ipc_diag_messages_received;
        if (service->congestion_control_enabled && service->congested &&
            service->channel.count <= service->congestion_low_watermark)
            service->congested = false;
        if (message->delivery_id != 0U) {
            service->received_delivery_ids[service->received_delivery_cursor] = message->delivery_id;
            service->received_delivery_cursor =
                (service->received_delivery_cursor + 1U) % SCHEDULER_IPC_DELIVERY_HISTORY;
        }
    }
    scheduler_ipc_service_release_ref_locked(service);
    scheduler_irq_restore(finish_flags);
    return received;
}



/* Bounded in-memory delivery tracking. ACK must follow receiver-side processing;
 * receipts are not persistent across reboot. */
scheduler_ipc_delivery_result_t scheduler_ipc_service_send_reliable_backoff(
    scheduler_ipc_service_id_t source_service_id,
    scheduler_ipc_service_id_t destination_service_id,
    uint32_t delivery_id, uint32_t request_id,
    const scheduler_ipc_message_t *message, uint32_t max_attempts,
    uint64_t retry_interval_ticks, uint64_t max_retry_interval_ticks,
    uint64_t deadline_ticks) {
    if (source_service_id == 0U || destination_service_id == 0U ||
        delivery_id == 0U || request_id == 0U || max_attempts == 0U ||
        ((retry_interval_ticks == 0U) != (deadline_ticks == 0U)) ||
        (retry_interval_ticks == 0U && max_retry_interval_ticks != 0U) ||
        (retry_interval_ticks != 0U && max_retry_interval_ticks < retry_interval_ticks) ||
        !scheduler_ipc_message_valid(message))
        return SCHEDULER_IPC_DELIVERY_INVALID_ARGUMENT;

    const uint64_t flags = scheduler_irq_save_disable();
    if (scheduler_ipc_find_delivery_locked(delivery_id) != NULL) {
        scheduler_irq_restore(flags);
        return SCHEDULER_IPC_DELIVERY_INVALID_ARGUMENT;
    }
    scheduler_ipc_delivery_track_t *slot = NULL;
    for (uint32_t i = 0U; i < SCHEDULER_IPC_MAX_TRACKED_DELIVERIES; ++i) {
        scheduler_ipc_delivery_track_t *candidate = &ipc_delivery_tracks[i];
        if (!candidate->valid ||
            candidate->receipt.state != SCHEDULER_IPC_DELIVERY_STATE_PENDING) {
            slot = candidate;
            if (!candidate->valid) break;
        }
    }
    if (slot == NULL) {
        scheduler_irq_restore(flags);
        return SCHEDULER_IPC_DELIVERY_TRACKING_CAPACITY;
    }
    slot->valid = true;
    slot->receipt.delivery_id = delivery_id;
    slot->receipt.state = SCHEDULER_IPC_DELIVERY_STATE_PENDING;
    slot->receipt.attempts = 1U;
    slot->receipt.max_attempts = max_attempts;
    slot->receipt.source_service_id = source_service_id;
    slot->receipt.destination_service_id = destination_service_id;
    slot->receipt.request_id = request_id;
    slot->receipt.last_result = SCHEDULER_IPC_DELIVERY_BACKPRESSURE;
    slot->receipt.retry_interval_ticks = retry_interval_ticks;
    slot->receipt.max_retry_interval_ticks = max_retry_interval_ticks;
    const uint64_t now = interrupts_ticks();
    slot->receipt.deadline_tick = deadline_ticks == 0U ? 0U :
        scheduler_ipc_tick_add_saturating(now, deadline_ticks);
    slot->receipt.next_retry_tick = retry_interval_ticks == 0U ? 0U :
        scheduler_ipc_tick_add_saturating(now, retry_interval_ticks);
    slot->timer_driven = retry_interval_ticks != 0U;
    slot->backoff_enabled = max_retry_interval_ticks > retry_interval_ticks;
    slot->dead_lettered = false;
    scheduler_ipc_message_copy(&slot->message, message);
    slot->message.delivery_id = delivery_id;
    ++ipc_diag_deliveries_started;
    scheduler_irq_restore(flags);

    const scheduler_ipc_delivery_result_t result = scheduler_ipc_service_send_ex(
        source_service_id, destination_service_id, request_id, &slot->message);
    const uint64_t finish_flags = scheduler_irq_save_disable();
    scheduler_ipc_delivery_track_t *current = scheduler_ipc_find_delivery_locked(delivery_id);
    if (current != NULL && current->receipt.state == SCHEDULER_IPC_DELIVERY_STATE_PENDING) {
        current->receipt.last_result = result;
        if (result == SCHEDULER_IPC_DELIVERY_INVALID_ARGUMENT ||
            result == SCHEDULER_IPC_DELIVERY_SERVICE_NOT_FOUND ||
            result == SCHEDULER_IPC_DELIVERY_SERVICE_CLOSED ||
            result == SCHEDULER_IPC_DELIVERY_INTERNAL_ERROR) {
            current->receipt.state = SCHEDULER_IPC_DELIVERY_STATE_FAILED;
        } else if (current->receipt.attempts >= current->receipt.max_attempts &&
                   !scheduler_ipc_delivery_accepted(result) &&
                   result != SCHEDULER_IPC_DELIVERY_DUPLICATE_SUPPRESSED &&
                   result != SCHEDULER_IPC_DELIVERY_CIRCUIT_OPEN) {
            current->receipt.state = SCHEDULER_IPC_DELIVERY_STATE_EXHAUSTED;
            current->receipt.last_result = SCHEDULER_IPC_DELIVERY_RETRY_EXHAUSTED;
            ++ipc_diag_delivery_retries_exhausted;
        }
        if (current->receipt.state == SCHEDULER_IPC_DELIVERY_STATE_FAILED ||
            current->receipt.state == SCHEDULER_IPC_DELIVERY_STATE_EXHAUSTED)
            scheduler_ipc_dead_letter_enqueue_locked(current, current->receipt.last_result,
                                                      interrupts_ticks());
    }
    scheduler_irq_restore(finish_flags);
    return result;
}

scheduler_ipc_delivery_result_t scheduler_ipc_service_send_reliable_timed(
    scheduler_ipc_service_id_t source_service_id,
    scheduler_ipc_service_id_t destination_service_id,
    uint32_t delivery_id, uint32_t request_id,
    const scheduler_ipc_message_t *message, uint32_t max_attempts,
    uint64_t retry_interval_ticks, uint64_t deadline_ticks) {
    return scheduler_ipc_service_send_reliable_backoff(source_service_id,
        destination_service_id, delivery_id, request_id, message, max_attempts,
        retry_interval_ticks, retry_interval_ticks, deadline_ticks);
}

scheduler_ipc_delivery_result_t scheduler_ipc_service_send_reliable(
    scheduler_ipc_service_id_t source_service_id,
    scheduler_ipc_service_id_t destination_service_id,
    uint32_t delivery_id, uint32_t request_id,
    const scheduler_ipc_message_t *message, uint32_t max_attempts) {
    return scheduler_ipc_service_send_reliable_timed(source_service_id,
        destination_service_id, delivery_id, request_id, message, max_attempts, 0U, 0U);
}

bool scheduler_ipc_service_ack_delivery(scheduler_ipc_service_id_t destination_service_id,
                                        uint32_t delivery_id) {
    if (destination_service_id == 0U || delivery_id == 0U) return false;
    const uint64_t flags = scheduler_irq_save_disable();
    scheduler_ipc_delivery_track_t *track = scheduler_ipc_find_delivery_locked(delivery_id);
    scheduler_ipc_service_t *destination = scheduler_ipc_find_service_locked(destination_service_id);
    if (track == NULL || destination == NULL ||
        track->receipt.destination_service_id != destination_service_id ||
        (track->receipt.state != SCHEDULER_IPC_DELIVERY_STATE_PENDING &&
         track->receipt.state != SCHEDULER_IPC_DELIVERY_STATE_EXHAUSTED) ||
        !scheduler_ipc_service_has_received_delivery_locked(destination, delivery_id)) {
        scheduler_irq_restore(flags);
        return false;
    }
    track->receipt.state = SCHEDULER_IPC_DELIVERY_STATE_ACKNOWLEDGED;
    track->receipt.last_result = SCHEDULER_IPC_DELIVERY_ACKNOWLEDGED;
    ++ipc_diag_deliveries_acknowledged;
    scheduler_irq_restore(flags);
    return true;
}

scheduler_ipc_delivery_result_t scheduler_ipc_delivery_retry(uint32_t delivery_id) {
    if (delivery_id == 0U) return SCHEDULER_IPC_DELIVERY_INVALID_ARGUMENT;
    const uint64_t flags = scheduler_irq_save_disable();
    scheduler_ipc_delivery_track_t *track = scheduler_ipc_find_delivery_locked(delivery_id);
    if (track == NULL) {
        scheduler_irq_restore(flags);
        return SCHEDULER_IPC_DELIVERY_INVALID_ARGUMENT;
    }
    if (track->receipt.state == SCHEDULER_IPC_DELIVERY_STATE_ACKNOWLEDGED) {
        scheduler_irq_restore(flags);
        return SCHEDULER_IPC_DELIVERY_ALREADY_ACKNOWLEDGED;
    }
    if (track->receipt.state == SCHEDULER_IPC_DELIVERY_STATE_EXHAUSTED) {
        scheduler_irq_restore(flags);
        return SCHEDULER_IPC_DELIVERY_RETRY_EXHAUSTED;
    }
    if (track->receipt.state != SCHEDULER_IPC_DELIVERY_STATE_PENDING) {
        scheduler_irq_restore(flags);
        return track->receipt.last_result;
    }
    scheduler_ipc_service_t *gate_destination =
        scheduler_ipc_find_service_locked(track->receipt.destination_service_id);
    if (gate_destination != NULL && gate_destination->breaker_failure_threshold != 0U) {
        const uint64_t now = interrupts_ticks();
        if (gate_destination->circuit_state == SCHEDULER_IPC_CIRCUIT_OPEN &&
            now < gate_destination->breaker_open_until_tick) {
            ++ipc_diag_circuit_open_rejections;
            track->receipt.last_result = SCHEDULER_IPC_DELIVERY_CIRCUIT_OPEN;
            scheduler_irq_restore(flags);
            return SCHEDULER_IPC_DELIVERY_CIRCUIT_OPEN;
        }
        if (gate_destination->circuit_state == SCHEDULER_IPC_CIRCUIT_OPEN &&
            now >= gate_destination->breaker_open_until_tick) {
            gate_destination->circuit_state = SCHEDULER_IPC_CIRCUIT_HALF_OPEN;
            gate_destination->health_state = SCHEDULER_IPC_HEALTH_DEGRADED;
            gate_destination->half_open_probe_in_flight = false;
        }
        if (gate_destination->circuit_state == SCHEDULER_IPC_CIRCUIT_HALF_OPEN &&
            gate_destination->half_open_probe_in_flight) {
            ++ipc_diag_circuit_open_rejections;
            track->receipt.last_result = SCHEDULER_IPC_DELIVERY_CIRCUIT_OPEN;
            scheduler_irq_restore(flags);
            return SCHEDULER_IPC_DELIVERY_CIRCUIT_OPEN;
        }
    }
    if (track->receipt.attempts >= track->receipt.max_attempts) {
        track->receipt.state = SCHEDULER_IPC_DELIVERY_STATE_EXHAUSTED;
        track->receipt.last_result = SCHEDULER_IPC_DELIVERY_RETRY_EXHAUSTED;
        ++ipc_diag_delivery_retries_exhausted;
        scheduler_ipc_dead_letter_enqueue_locked(track, SCHEDULER_IPC_DELIVERY_RETRY_EXHAUSTED,
                                                  interrupts_ticks());
        scheduler_irq_restore(flags);
        return SCHEDULER_IPC_DELIVERY_RETRY_EXHAUSTED;
    }
    ++track->receipt.attempts;
    ++ipc_diag_delivery_retry_attempts;
    const scheduler_ipc_service_id_t destination_id = track->receipt.destination_service_id;
    scheduler_ipc_service_t *destination = scheduler_ipc_find_service_locked(destination_id);
    if (destination != NULL && scheduler_ipc_delivery_already_seen_locked(destination, delivery_id)) {
        track->receipt.last_result = SCHEDULER_IPC_DELIVERY_DUPLICATE_SUPPRESSED;
        ++ipc_diag_duplicate_deliveries_suppressed;
        scheduler_irq_restore(flags);
        return SCHEDULER_IPC_DELIVERY_DUPLICATE_SUPPRESSED;
    }
    scheduler_ipc_message_t message;
    scheduler_ipc_message_copy(&message, &track->message);
    const scheduler_ipc_service_id_t source_id = track->receipt.source_service_id;
    const uint32_t request_id = track->receipt.request_id;
    scheduler_irq_restore(flags);

    const scheduler_ipc_delivery_result_t result = scheduler_ipc_service_send_ex(
        source_id, destination_id, request_id, &message);
    const uint64_t finish_flags = scheduler_irq_save_disable();
    track = scheduler_ipc_find_delivery_locked(delivery_id);
    if (track != NULL && track->receipt.state == SCHEDULER_IPC_DELIVERY_STATE_PENDING) {
        track->receipt.last_result = result;
        if (result == SCHEDULER_IPC_DELIVERY_INVALID_ARGUMENT ||
            result == SCHEDULER_IPC_DELIVERY_SERVICE_NOT_FOUND ||
            result == SCHEDULER_IPC_DELIVERY_SERVICE_CLOSED ||
            result == SCHEDULER_IPC_DELIVERY_INTERNAL_ERROR) {
            track->receipt.state = SCHEDULER_IPC_DELIVERY_STATE_FAILED;
        } else if (track->receipt.attempts >= track->receipt.max_attempts &&
                   !scheduler_ipc_delivery_accepted(result) &&
                   result != SCHEDULER_IPC_DELIVERY_DUPLICATE_SUPPRESSED &&
                   result != SCHEDULER_IPC_DELIVERY_CIRCUIT_OPEN) {
            track->receipt.state = SCHEDULER_IPC_DELIVERY_STATE_EXHAUSTED;
            track->receipt.last_result = SCHEDULER_IPC_DELIVERY_RETRY_EXHAUSTED;
            ++ipc_diag_delivery_retries_exhausted;
        }
        if (track->receipt.state == SCHEDULER_IPC_DELIVERY_STATE_FAILED ||
            track->receipt.state == SCHEDULER_IPC_DELIVERY_STATE_EXHAUSTED)
            scheduler_ipc_dead_letter_enqueue_locked(track, track->receipt.last_result,
                                                      interrupts_ticks());
    }
    scheduler_irq_restore(finish_flags);
    if (result != SCHEDULER_IPC_DELIVERY_DELIVERED &&
        result != SCHEDULER_IPC_DELIVERY_DELIVERED_DROPPED_OLDEST &&
        result != SCHEDULER_IPC_DELIVERY_DUPLICATE_SUPPRESSED) {
        scheduler_ipc_delivery_receipt_t final_receipt = {0};
        if (scheduler_ipc_delivery_get_receipt(delivery_id, &final_receipt) &&
            final_receipt.state == SCHEDULER_IPC_DELIVERY_STATE_EXHAUSTED)
            return SCHEDULER_IPC_DELIVERY_RETRY_EXHAUSTED;
    }
    return result;
}

bool scheduler_ipc_delivery_get_receipt(uint32_t delivery_id,
                                        scheduler_ipc_delivery_receipt_t *receipt) {
    if (delivery_id == 0U || receipt == NULL) return false;
    const uint64_t flags = scheduler_irq_save_disable();
    scheduler_ipc_delivery_track_t *track = scheduler_ipc_find_delivery_locked(delivery_id);
    if (track == NULL) {
        scheduler_irq_restore(flags);
        return false;
    }
    *receipt = track->receipt;
    scheduler_irq_restore(flags);
    return true;
}

uint32_t scheduler_ipc_delivery_pending_count(void) {
    const uint64_t flags = scheduler_irq_save_disable();
    uint32_t count = 0U;
    for (uint32_t i = 0U; i < SCHEDULER_IPC_MAX_TRACKED_DELIVERIES; ++i)
        if (ipc_delivery_tracks[i].valid &&
            ipc_delivery_tracks[i].receipt.state == SCHEDULER_IPC_DELIVERY_STATE_PENDING)
            ++count;
    scheduler_irq_restore(flags);
    return count;
}

uint32_t scheduler_ipc_dead_letter_count(void) {
    const uint64_t flags = scheduler_irq_save_disable();
    const uint32_t count = ipc_dead_letter_count;
    scheduler_irq_restore(flags);
    return count;
}

bool scheduler_ipc_dead_letter_receive(scheduler_ipc_dead_letter_t *entry) {
    if (entry == NULL) return false;
    const uint64_t flags = scheduler_irq_save_disable();
    if (ipc_dead_letter_count == 0U) {
        scheduler_irq_restore(flags);
        return false;
    }
    *entry = ipc_dead_letters[ipc_dead_letter_head];
    ipc_dead_letters[ipc_dead_letter_head].receipt.delivery_id = 0U;
    ipc_dead_letters[ipc_dead_letter_head].message.delivery_id = 0U;
    ipc_dead_letter_head = (ipc_dead_letter_head + 1U) % SCHEDULER_IPC_DEAD_LETTER_CAPACITY;
    --ipc_dead_letter_count;
    scheduler_irq_restore(flags);
    return true;
}

static void scheduler_ipc_process_delivery_timers(uint64_t now) {
    for (uint32_t i = 0U; i < SCHEDULER_IPC_MAX_TRACKED_DELIVERIES; ++i) {
        uint32_t delivery_id = 0U;
        const uint64_t flags = scheduler_irq_save_disable();
        scheduler_ipc_delivery_track_t *track = &ipc_delivery_tracks[i];
        if (!track->valid || !track->timer_driven ||
            track->receipt.state != SCHEDULER_IPC_DELIVERY_STATE_PENDING) {
            scheduler_irq_restore(flags);
            continue;
        }
        if (track->receipt.deadline_tick != 0U && now >= track->receipt.deadline_tick) {
            track->receipt.state = SCHEDULER_IPC_DELIVERY_STATE_EXPIRED;
            track->receipt.last_result = SCHEDULER_IPC_DELIVERY_DEADLINE_EXPIRED;
            ++ipc_diag_delivery_deadlines_expired;
            scheduler_ipc_dead_letter_enqueue_locked(track,
                SCHEDULER_IPC_DELIVERY_DEADLINE_EXPIRED, now);
            scheduler_irq_restore(flags);
            continue;
        }
        if (track->receipt.next_retry_tick != 0U && now >= track->receipt.next_retry_tick &&
            track->receipt.attempts < track->receipt.max_attempts) {
            delivery_id = track->receipt.delivery_id;
            if (track->backoff_enabled &&
                track->receipt.retry_interval_ticks < track->receipt.max_retry_interval_ticks) {
                uint64_t doubled = track->receipt.retry_interval_ticks > UINT64_MAX / 2U ?
                    UINT64_MAX : track->receipt.retry_interval_ticks * 2U;
                if (doubled > track->receipt.max_retry_interval_ticks)
                    doubled = track->receipt.max_retry_interval_ticks;
                track->receipt.retry_interval_ticks = doubled;
            }
            track->receipt.next_retry_tick = scheduler_ipc_tick_add_saturating(
                now, track->receipt.retry_interval_ticks);
            ++ipc_diag_automatic_retry_attempts;
        }
        scheduler_irq_restore(flags);
        if (delivery_id != 0U) (void)scheduler_ipc_delivery_retry(delivery_id);
    }
}

bool scheduler_ipc_receipt_retry_self_test(void) {
    if (scheduler_ipc_service_count() != 0U ||
        scheduler_ipc_delivery_pending_count() != 0U) return false;
    scheduler_ipc_service_id_t source_id = 0U, destination_id = 0U;
    const uint32_t caps = SCHEDULER_IPC_SERVICE_CAP_ONE_WAY;
    if (!scheduler_ipc_service_register_ex("receipt.src", caps, &source_id) ||
        !scheduler_ipc_service_register_ex("receipt.dst", caps, &destination_id))
        return false;

    scheduler_ipc_message_t message = {0};
    message.type = UINT32_C(0x41434B31);
    message.length = 1U;
    message.payload[0] = UINT8_C(0x5A);
    const uint32_t delivery_id = UINT32_C(0x14000101);
    if (scheduler_ipc_service_send_reliable(source_id, destination_id, delivery_id,
            41U, &message, 5U) != SCHEDULER_IPC_DELIVERY_DELIVERED) return false;
    scheduler_ipc_delivery_receipt_t receipt = {0};
    if (!scheduler_ipc_delivery_get_receipt(delivery_id, &receipt) ||
        receipt.state != SCHEDULER_IPC_DELIVERY_STATE_PENDING ||
        receipt.attempts != 1U || receipt.max_attempts != 5U) return false;
    if (scheduler_ipc_delivery_retry(delivery_id) !=
        SCHEDULER_IPC_DELIVERY_DUPLICATE_SUPPRESSED) return false;
    scheduler_ipc_message_t received = {0};
    if (!scheduler_ipc_service_receive(destination_id, &received) ||
        received.delivery_id != delivery_id || received.request_id != 41U ||
        received.payload[0] != UINT8_C(0x5A)) return false;
    if (scheduler_ipc_delivery_retry(delivery_id) !=
        SCHEDULER_IPC_DELIVERY_DUPLICATE_SUPPRESSED) return false;
    if (!scheduler_ipc_service_ack_delivery(destination_id, delivery_id) ||
        !scheduler_ipc_delivery_get_receipt(delivery_id, &receipt) ||
        receipt.state != SCHEDULER_IPC_DELIVERY_STATE_ACKNOWLEDGED ||
        scheduler_ipc_delivery_retry(delivery_id) !=
            SCHEDULER_IPC_DELIVERY_ALREADY_ACKNOWLEDGED) return false;

    for (uint32_t i = 0U; i < SCHEDULER_IPC_CAPACITY; ++i) {
        scheduler_ipc_message_t queued = {0};
        queued.type = UINT32_C(0x51554555);
        if (scheduler_ipc_service_send_ex(source_id, destination_id, 700U + i,
                &queued) != SCHEDULER_IPC_DELIVERY_DELIVERED) return false;
    }
    const uint32_t exhausted_id = UINT32_C(0x14000102);
    if (scheduler_ipc_service_send_reliable(source_id, destination_id, exhausted_id,
            42U, &message, 2U) != SCHEDULER_IPC_DELIVERY_BACKPRESSURE ||
        scheduler_ipc_delivery_retry(exhausted_id) !=
            SCHEDULER_IPC_DELIVERY_RETRY_EXHAUSTED ||
        scheduler_ipc_delivery_retry(exhausted_id) !=
            SCHEDULER_IPC_DELIVERY_RETRY_EXHAUSTED ||
        !scheduler_ipc_delivery_get_receipt(exhausted_id, &receipt) ||
        receipt.state != SCHEDULER_IPC_DELIVERY_STATE_EXHAUSTED ||
        receipt.attempts != 2U) return false;
    for (uint32_t i = 0U; i < SCHEDULER_IPC_CAPACITY; ++i)
        if (!scheduler_ipc_service_receive(destination_id, &received)) return false;
    if (scheduler_ipc_delivery_pending_count() != 0U) return false;
    return scheduler_ipc_service_unregister(destination_id) &&
           scheduler_ipc_service_unregister(source_id);
}

 scheduler_ipc_delivery_result_t scheduler_ipc_service_send_request_ex(
    scheduler_ipc_service_id_t source_service_id,
    scheduler_ipc_service_id_t destination_service_id,
    uint32_t request_id,
    const scheduler_ipc_message_t *request) {
    if (request_id == 0U || !scheduler_ipc_message_valid(request))
        return SCHEDULER_IPC_DELIVERY_INVALID_ARGUMENT;
    const uint64_t flags = scheduler_irq_save_disable();
    scheduler_ipc_service_t *source = scheduler_ipc_find_service_locked(source_service_id);
    scheduler_ipc_service_t *destination =
        scheduler_ipc_find_service_locked(destination_service_id);
    if (source == NULL || destination == NULL) {
        scheduler_irq_restore(flags);
        return SCHEDULER_IPC_DELIVERY_SERVICE_NOT_FOUND;
    }
    if (source->closing || destination->closing) {
        scheduler_irq_restore(flags);
        return SCHEDULER_IPC_DELIVERY_SERVICE_CLOSED;
    }
    const bool allowed =
        (source->capabilities & SCHEDULER_IPC_SERVICE_CAP_REQUEST_RESPONSE) != 0U &&
        (destination->capabilities & SCHEDULER_IPC_SERVICE_CAP_REQUEST_RESPONSE) != 0U;
    scheduler_irq_restore(flags);
    if (!allowed) return SCHEDULER_IPC_DELIVERY_INVALID_ARGUMENT;

    scheduler_ipc_message_t message;
    scheduler_ipc_message_copy(&message, request);
    message.protocol_version = SCHEDULER_IPC_PROTOCOL_VERSION;
    message.kind = SCHEDULER_IPC_KIND_REQUEST;
    message.status = SCHEDULER_IPC_STATUS_OK;
    return scheduler_ipc_service_send_ex(source_service_id, destination_service_id,
                                         request_id, &message);
}

bool scheduler_ipc_service_send_request(scheduler_ipc_service_id_t source_service_id,
                                        scheduler_ipc_service_id_t destination_service_id,
                                        uint32_t request_id,
                                        const scheduler_ipc_message_t *request) {
    if (request_id == 0U || !scheduler_ipc_message_valid(request)) return false;
    const uint64_t flags = scheduler_irq_save_disable();
    scheduler_ipc_service_t *source = scheduler_ipc_find_service_locked(source_service_id);
    scheduler_ipc_service_t *destination =
        scheduler_ipc_find_service_locked(destination_service_id);
    const bool allowed = source != NULL && destination != NULL &&
        !source->closing && !destination->closing &&
        (source->capabilities & SCHEDULER_IPC_SERVICE_CAP_REQUEST_RESPONSE) != 0U &&
        (destination->capabilities & SCHEDULER_IPC_SERVICE_CAP_REQUEST_RESPONSE) != 0U;
    scheduler_irq_restore(flags);
    if (!allowed) return false;

    scheduler_ipc_message_t message;
    scheduler_ipc_message_copy(&message, request);
    message.protocol_version = SCHEDULER_IPC_PROTOCOL_VERSION;
    message.kind = SCHEDULER_IPC_KIND_REQUEST;
    message.status = SCHEDULER_IPC_STATUS_OK;
    /* Keep legacy BLOCK behavior for the old API. The *_ex API is always nonblocking. */
    return scheduler_ipc_service_send(source_service_id, destination_service_id,
                                      request_id, &message);
}


bool scheduler_ipc_timer_retry_dead_letter_self_test(void) {
    if (scheduler_ipc_service_count() != 0U ||
        scheduler_ipc_delivery_pending_count() != 0U) return false;

    bool prior_exhaustion_seen = false;
    scheduler_ipc_dead_letter_t letter = {0};
    while (scheduler_ipc_dead_letter_receive(&letter)) {
        if (letter.receipt.delivery_id == UINT32_C(0x14000102) &&
            letter.reason == SCHEDULER_IPC_DELIVERY_RETRY_EXHAUSTED)
            prior_exhaustion_seen = true;
    }
    if (!prior_exhaustion_seen) return false;

    scheduler_ipc_service_id_t source_id = 0U, destination_id = 0U;
    const uint32_t caps = SCHEDULER_IPC_SERVICE_CAP_ONE_WAY;
    if (!scheduler_ipc_service_register_ex("retry.timer.src", caps, &source_id) ||
        !scheduler_ipc_service_register_ex("retry.timer.dst", caps, &destination_id))
        return false;

    scheduler_ipc_message_t message = {0};
    message.type = UINT32_C(0x54494D52);
    message.length = 2U;
    message.payload[0] = UINT8_C(0xA5);
    message.payload[1] = UINT8_C(0x5A);
    scheduler_ipc_diagnostics_t before = {0}, after = {0};
    scheduler_ipc_get_diagnostics(&before);

    const uint32_t good_id = UINT32_C(0x15000101);
    if (scheduler_ipc_service_send_reliable_timed(source_id, destination_id,
            good_id, 51U, &message, 5U, 2U, 12U) !=
            SCHEDULER_IPC_DELIVERY_DELIVERED) return false;
    scheduler_ipc_delivery_receipt_t receipt = {0};
    if (!scheduler_ipc_delivery_get_receipt(good_id, &receipt) ||
        receipt.deadline_tick == 0U || receipt.retry_interval_ticks != 2U ||
        receipt.next_retry_tick == 0U) return false;

    interrupts_enable();
    const bool retry_waited = interrupts_wait_for_ticks(receipt.next_retry_tick + 1U);
    interrupts_disable();
    if (!retry_waited ||
        !scheduler_ipc_delivery_get_receipt(good_id, &receipt) ||
        receipt.state != SCHEDULER_IPC_DELIVERY_STATE_PENDING ||
        receipt.attempts < 2U) return false;

    scheduler_ipc_message_t received = {0};
    if (!scheduler_ipc_service_receive(destination_id, &received) ||
        received.delivery_id != good_id ||
        received.payload[0] != UINT8_C(0xA5) ||
        received.payload[1] != UINT8_C(0x5A) ||
        !scheduler_ipc_service_ack_delivery(destination_id, good_id) ||
        !scheduler_ipc_delivery_get_receipt(good_id, &receipt) ||
        receipt.state != SCHEDULER_IPC_DELIVERY_STATE_ACKNOWLEDGED) return false;

    for (uint32_t i = 0U; i < SCHEDULER_IPC_CAPACITY; ++i) {
        scheduler_ipc_message_t filler = {0};
        filler.type = UINT32_C(0x444C5146);
        filler.length = 1U;
        filler.payload[0] = (uint8_t)i;
        if (scheduler_ipc_service_send_ex(source_id, destination_id, 800U + i,
                &filler) != SCHEDULER_IPC_DELIVERY_DELIVERED) return false;
    }
    const uint32_t expired_id = UINT32_C(0x15000102);
    if (scheduler_ipc_service_send_reliable_timed(source_id, destination_id,
            expired_id, 52U, &message, 10U, 1U, 4U) !=
            SCHEDULER_IPC_DELIVERY_BACKPRESSURE ||
        !scheduler_ipc_delivery_get_receipt(expired_id, &receipt) ||
        receipt.state != SCHEDULER_IPC_DELIVERY_STATE_PENDING) return false;

    const uint64_t deadline_target = receipt.deadline_tick + 1U;
    interrupts_enable();
    const bool deadline_waited = interrupts_wait_for_ticks(deadline_target);
    interrupts_disable();
    if (!deadline_waited ||
        !scheduler_ipc_delivery_get_receipt(expired_id, &receipt) ||
        receipt.state != SCHEDULER_IPC_DELIVERY_STATE_EXPIRED ||
        receipt.last_result != SCHEDULER_IPC_DELIVERY_DEADLINE_EXPIRED ||
        scheduler_ipc_dead_letter_count() != 1U) return false;

    bool expired_letter_seen = false;
    if (scheduler_ipc_dead_letter_receive(&letter) &&
        letter.receipt.delivery_id == expired_id &&
        letter.reason == SCHEDULER_IPC_DELIVERY_DEADLINE_EXPIRED &&
        letter.message.delivery_id == expired_id &&
        letter.recorded_tick >= receipt.deadline_tick)
        expired_letter_seen = true;
    if (!expired_letter_seen || scheduler_ipc_dead_letter_count() != 0U) return false;

    for (uint32_t i = 0U; i < SCHEDULER_IPC_CAPACITY; ++i)
        if (!scheduler_ipc_service_receive(destination_id, &received)) return false;
    scheduler_ipc_get_diagnostics(&after);
    const bool diagnostics_ok =
        after.automatic_retry_attempts >= before.automatic_retry_attempts + 1U &&
        after.delivery_deadlines_expired == before.delivery_deadlines_expired + 1U &&
        after.dead_letters_enqueued >= before.dead_letters_enqueued + 1U &&
        after.dead_letters_pending == 0U &&
        scheduler_ipc_delivery_pending_count() == 0U;
    const bool unregistered = scheduler_ipc_service_unregister(destination_id) &&
        scheduler_ipc_service_unregister(source_id) &&
        scheduler_ipc_service_count() == 0U;
    return diagnostics_ok && unregistered;
}


bool scheduler_ipc_adaptive_load_self_test(void) {
    if (scheduler_ipc_service_count() != 0U) return false;
    scheduler_ipc_service_id_t source = 0U, low = 0U, high = 0U;
    const uint32_t caps = SCHEDULER_IPC_SERVICE_CAP_ONE_WAY;
    if (!scheduler_ipc_service_register_ex("load.src", caps, &source) ||
        !scheduler_ipc_service_register_ex("load.low", caps, &low) ||
        !scheduler_ipc_service_register_ex("load.high", caps, &high)) return false;
    if (!scheduler_ipc_service_set_priority(low, 1U) ||
        !scheduler_ipc_service_set_priority(high, 4U) ||
        scheduler_ipc_service_set_priority(high, 0U) ||
        !scheduler_ipc_service_set_congestion_control(low, 2U, 0U) ||
        scheduler_ipc_service_set_congestion_control(high, 5U, 2U)) return false;
    scheduler_ipc_service_id_t candidates[2] = { high, low }, selected = 0U;
    if (!scheduler_ipc_service_select_adaptive(candidates, 2U, &selected) ||
        selected != high) return false;
    scheduler_ipc_message_t message = {0};
    message.type = UINT32_C(0x4C4F4144);
    message.length = 1U;
    for (uint32_t i = 0U; i < 2U; ++i) {
        message.payload[0] = (uint8_t)i;
        if (scheduler_ipc_service_send_ex(source, low, 100U + i, &message) !=
            SCHEDULER_IPC_DELIVERY_DELIVERED) return false;
    }
    if (!scheduler_ipc_service_select_adaptive(candidates, 2U, &selected) ||
        selected != high) return false;
    if (scheduler_ipc_service_send_ex(source, low, 103U, &message) !=
        SCHEDULER_IPC_DELIVERY_BACKPRESSURE) return false;
    scheduler_ipc_service_info_t info = {0};
    if (!scheduler_ipc_service_get_info(low, &info) || !info.congested ||
        info.priority != 1U || info.queued_messages != 2U) return false;
    scheduler_ipc_message_t received = {0};
    if (!scheduler_ipc_service_receive(low, &received) ||
        !scheduler_ipc_service_receive(low, &received) ||
        !scheduler_ipc_service_get_info(low, &info) || info.congested ||
        !scheduler_ipc_service_select_adaptive(candidates, 2U, &selected)) return false;
    const bool cleanup = scheduler_ipc_service_unregister(low) &&
        scheduler_ipc_service_unregister(high) &&
        scheduler_ipc_service_unregister(source) && scheduler_ipc_service_count() == 0U;
    return cleanup;
}

bool scheduler_ipc_circuit_breaker_self_test(void) {
    if (scheduler_ipc_service_count() != 0U ||
        scheduler_ipc_delivery_pending_count() != 0U ||
        scheduler_ipc_dead_letter_count() != 0U) return false;

    scheduler_ipc_service_id_t source_id = 0U, destination_id = 0U;
    const uint32_t caps = SCHEDULER_IPC_SERVICE_CAP_ONE_WAY;
    if (!scheduler_ipc_service_register_ex("breaker.src", caps, &source_id) ||
        !scheduler_ipc_service_register_ex("breaker.dst", caps, &destination_id))
        return false;

    for (uint32_t i = 0U; i < SCHEDULER_IPC_CAPACITY; ++i) {
        scheduler_ipc_message_t filler = {0};
        filler.type = UINT32_C(0x43425246);
        filler.length = 1U;
        filler.payload[0] = (uint8_t)i;
        if (scheduler_ipc_service_send_ex(source_id, destination_id, 900U + i,
                &filler) != SCHEDULER_IPC_DELIVERY_DELIVERED) return false;
    }
    if (!scheduler_ipc_service_set_circuit_breaker(destination_id, 2U, 6U) ||
        scheduler_ipc_service_set_circuit_breaker(destination_id, 0U, 6U) ||
        scheduler_ipc_service_set_circuit_breaker(destination_id, 2U, 0U))
        return false;

    scheduler_ipc_diagnostics_t before = {0}, after = {0};
    scheduler_ipc_get_diagnostics(&before);
    scheduler_ipc_message_t message = {0};
    message.type = UINT32_C(0x4241434B);
    message.length = 1U;
    message.payload[0] = UINT8_C(0x7B);
    const uint32_t delivery_id = UINT32_C(0x16000101);
    if (scheduler_ipc_service_send_reliable_backoff(source_id, destination_id,
            delivery_id, 61U, &message, 8U, 1U, 4U, 30U) !=
            SCHEDULER_IPC_DELIVERY_BACKPRESSURE) return false;

    scheduler_ipc_delivery_receipt_t receipt = {0};
    if (!scheduler_ipc_delivery_get_receipt(delivery_id, &receipt) ||
        receipt.attempts != 1U || receipt.retry_interval_ticks != 1U ||
        receipt.max_retry_interval_ticks != 4U) return false;
    const uint64_t first_retry_tick = receipt.next_retry_tick;
    interrupts_enable();
    const bool first_waited = interrupts_wait_for_ticks(first_retry_tick + 1U);
    interrupts_disable();
    if (!first_waited ||
        !scheduler_ipc_delivery_get_receipt(delivery_id, &receipt) ||
        receipt.attempts != 2U || receipt.retry_interval_ticks != 2U)
        return false;

    scheduler_ipc_service_health_t health = {0};
    if (!scheduler_ipc_service_get_health(destination_id, &health) ||
        health.circuit_state != SCHEDULER_IPC_CIRCUIT_OPEN ||
        health.health_state != SCHEDULER_IPC_HEALTH_UNAVAILABLE ||
        health.consecutive_failures < 2U ||
        scheduler_ipc_service_send_ex(source_id, destination_id, 990U, &message) !=
            SCHEDULER_IPC_DELIVERY_CIRCUIT_OPEN)
        return false;
    const uint64_t open_until = health.open_until_tick;
    if (open_until <= interrupts_ticks()) return false;

    scheduler_ipc_message_t received = {0};
    for (uint32_t i = 0U; i < SCHEDULER_IPC_CAPACITY; ++i) {
        if (!scheduler_ipc_service_receive(destination_id, &received) ||
            received.request_id != 900U + i) return false;
    }
    interrupts_enable();
    const bool cooldown_waited = interrupts_wait_for_ticks(open_until + 1U);
    interrupts_disable();
    if (!cooldown_waited ||
        !scheduler_ipc_service_get_health(destination_id, &health) ||
        health.circuit_state != SCHEDULER_IPC_CIRCUIT_CLOSED ||
        health.health_state != SCHEDULER_IPC_HEALTHY ||
        !scheduler_ipc_delivery_get_receipt(delivery_id, &receipt) ||
        receipt.state != SCHEDULER_IPC_DELIVERY_STATE_PENDING ||
        receipt.retry_interval_ticks != 4U)
        return false;

    if (!scheduler_ipc_service_receive(destination_id, &received) ||
        received.delivery_id != delivery_id || received.request_id != 61U ||
        received.payload[0] != UINT8_C(0x7B) ||
        !scheduler_ipc_service_ack_delivery(destination_id, delivery_id) ||
        !scheduler_ipc_delivery_get_receipt(delivery_id, &receipt) ||
        receipt.state != SCHEDULER_IPC_DELIVERY_STATE_ACKNOWLEDGED)
        return false;

    scheduler_ipc_get_diagnostics(&after);
    const bool stats_ok =
        after.circuit_breaker_trips == before.circuit_breaker_trips + 1U &&
        after.circuit_breaker_recoveries == before.circuit_breaker_recoveries + 1U &&
        after.circuit_open_rejections > before.circuit_open_rejections &&
        after.service_health_failures >= before.service_health_failures + 2U &&
        after.service_health_successes > before.service_health_successes;
    const bool unregistered = scheduler_ipc_service_unregister(destination_id) &&
        scheduler_ipc_service_unregister(source_id) &&
        scheduler_ipc_service_count() == 0U;
    return stats_ok && unregistered;
}

bool scheduler_ipc_service_reply(scheduler_ipc_service_id_t responder_service_id,
                                 const scheduler_ipc_message_t *request,
                                 const scheduler_ipc_message_t *response,
                                 scheduler_ipc_status_t status) {
    if (request == NULL || !scheduler_ipc_message_valid(response) ||
        request->protocol_version != SCHEDULER_IPC_PROTOCOL_VERSION ||
        request->kind != SCHEDULER_IPC_KIND_REQUEST ||
        request->status != SCHEDULER_IPC_STATUS_OK ||
        request->request_id == 0U || request->source_service_id == 0U ||
        (uint32_t)status > (uint32_t)SCHEDULER_IPC_STATUS_INTERNAL)
        return false;

    const uint64_t flags = scheduler_irq_save_disable();
    scheduler_ipc_service_t *responder =
        scheduler_ipc_find_service_locked(responder_service_id);
    scheduler_ipc_service_t *requester =
        scheduler_ipc_find_service_locked(request->source_service_id);
    const bool allowed = responder != NULL && requester != NULL &&
        !responder->closing && !requester->closing &&
        (responder->capabilities & SCHEDULER_IPC_SERVICE_CAP_REQUEST_RESPONSE) != 0U &&
        (requester->capabilities & SCHEDULER_IPC_SERVICE_CAP_REQUEST_RESPONSE) != 0U;
    if (!allowed) {
        scheduler_irq_restore(flags);
        return false;
    }

    bool active_pair = false;
    uint32_t pending_index = SCHEDULER_NO_TASK;
    for (uint32_t i = 0U; i < SCHEDULER_IPC_MAX_PENDING_CALLS; ++i) {
        scheduler_ipc_pending_call_t *pending = &ipc_pending_calls[i];
        if (!pending->active ||
            pending->source_service_id != request->source_service_id ||
            pending->destination_service_id != responder_service_id) continue;
        active_pair = true;
        if (pending->request_id == request->request_id) {
            pending_index = i;
            break;
        }
    }

    if (pending_index != SCHEDULER_NO_TASK) {
        scheduler_ipc_pending_call_t *pending = &ipc_pending_calls[pending_index];
        if (pending->completed || pending->expired || pending->cancelled) {
            ++ipc_diag_replies_dropped;
            scheduler_ipc_expired_record_locked(pending->source_service_id,
                pending->destination_service_id, pending->request_id);
            scheduler_irq_restore(flags);
            return false;
        }
        if (pending->owner_task_index >= SCHEDULER_MAX_TASKS) {
            scheduler_irq_restore(flags);
            return false;
        }
        scheduler_task_t *owner = &task_table[pending->owner_task_index];
        if (pending->waiting && owner->wait_result == SCHEDULER_WAIT_TIMED_OUT) {
            pending->expired = true;
            scheduler_ipc_expired_record_locked(pending->source_service_id,
                pending->destination_service_id, pending->request_id);
            scheduler_irq_restore(flags);
            return false;
        }
        if (pending->waiting && owner->wait_result == SCHEDULER_WAIT_CANCELLED) {
            pending->cancelled = true;
            pending->expired = true;
            scheduler_ipc_expired_record_locked(pending->source_service_id,
                pending->destination_service_id, pending->request_id);
            scheduler_irq_restore(flags);
            return false;
        }

        scheduler_ipc_message_t routed;
        scheduler_ipc_message_copy(&routed, response);
        routed.protocol_version = SCHEDULER_IPC_PROTOCOL_VERSION;
        routed.kind = status == SCHEDULER_IPC_STATUS_OK
            ? SCHEDULER_IPC_KIND_RESPONSE : SCHEDULER_IPC_KIND_ERROR;
        routed.status = status;
        routed.source_service_id = responder_service_id;
        routed.request_id = request->request_id;
        pending->response = routed;
        pending->completed = true;
        ++ipc_diag_calls_completed;
        if (pending->waiting && owner->state == SCHEDULER_TASK_BLOCKED &&
            owner->waiting_on == &pending->waiters)
            (void)wait_queue_wake_one_locked(&pending->waiters);
        scheduler_irq_restore(flags);
        return true;
    }

    /* Never leak late, timed-out, cancelled, or mismatched replies into a client's
       ordinary receive queue while that endpoint has active pending calls. */
    if (active_pair || scheduler_ipc_expired_match_locked(request->source_service_id,
            responder_service_id, request->request_id)) {
        ++ipc_diag_replies_dropped;
        scheduler_irq_restore(flags);
        return false;
    }
    scheduler_irq_restore(flags);

    scheduler_ipc_message_t message;
    scheduler_ipc_message_copy(&message, response);
    message.protocol_version = SCHEDULER_IPC_PROTOCOL_VERSION;
    message.kind = status == SCHEDULER_IPC_STATUS_OK
        ? SCHEDULER_IPC_KIND_RESPONSE : SCHEDULER_IPC_KIND_ERROR;
    message.status = status;
    return scheduler_ipc_service_send(responder_service_id,
                                      request->source_service_id,
                                      request->request_id, &message);
}

bool scheduler_ipc_message_is_response_for(const scheduler_ipc_message_t *message,
                                           uint32_t request_id) {
    if (message == NULL || request_id == 0U ||
        message->protocol_version != SCHEDULER_IPC_PROTOCOL_VERSION ||
        message->request_id != request_id ||
        (uint32_t)message->status > (uint32_t)SCHEDULER_IPC_STATUS_INTERNAL)
        return false;
    if (message->kind == SCHEDULER_IPC_KIND_RESPONSE)
        return message->status == SCHEDULER_IPC_STATUS_OK;
    if (message->kind == SCHEDULER_IPC_KIND_ERROR)
        return message->status != SCHEDULER_IPC_STATUS_OK;
    return false;
}


bool scheduler_ipc_service_call(scheduler_ipc_service_id_t source_service_id,
                                scheduler_ipc_service_id_t destination_service_id,
                                const scheduler_ipc_message_t *request,
                                uint64_t timeout_ticks,
                                scheduler_ipc_message_t *response,
                                volatile uint32_t *request_id_out) {
    if (!scheduler_ipc_message_valid(request) || response == NULL ||
        request_id_out == NULL || timeout_ticks == 0U ||
        timeout_ticks > (uint64_t)INT64_MAX) return false;

    uint32_t pending_index = SCHEDULER_NO_TASK;
    uint32_t owner_index = SCHEDULER_NO_TASK;
    uint32_t request_id = 0U;
    const uint64_t flags = scheduler_irq_save_disable();
    owner_index = current_task_index;
    if (!preemptive_active || owner_index >= SCHEDULER_MAX_TASKS ||
        task_table[owner_index].state != SCHEDULER_TASK_RUNNING ||
        task_table[owner_index].waiting_on != NULL || next_ipc_runtime_request_id == 0U) {
        scheduler_irq_restore(flags);
        return false;
    }

    scheduler_ipc_service_t *source = scheduler_ipc_find_service_locked(source_service_id);
    scheduler_ipc_service_t *destination =
        scheduler_ipc_find_service_locked(destination_service_id);
    if (source == NULL || destination == NULL || source->closing || destination->closing ||
        (source->capabilities & SCHEDULER_IPC_SERVICE_CAP_REQUEST_RESPONSE) == 0U ||
        (destination->capabilities & SCHEDULER_IPC_SERVICE_CAP_REQUEST_RESPONSE) == 0U ||
        source->active_calls > UINT32_MAX - (source == destination ? 2U : 1U) ||
        destination->active_calls > UINT32_MAX - 1U) {
        scheduler_irq_restore(flags);
        return false;
    }
    for (uint32_t i = 0U; i < SCHEDULER_IPC_MAX_PENDING_CALLS; ++i) {
        if (!ipc_pending_calls[i].active) { pending_index = i; break; }
    }
    if (pending_index == SCHEDULER_NO_TASK) {
        scheduler_irq_restore(flags);
        return false;
    }

    request_id = next_ipc_runtime_request_id++;
    scheduler_ipc_pending_call_t *pending = &ipc_pending_calls[pending_index];
    pending->active = true;
    pending->completed = false;
    pending->expired = false;
    pending->cancelled = false;
    pending->waiting = false;
    pending->source_service_id = source_service_id;
    pending->destination_service_id = destination_service_id;
    pending->request_id = request_id;
    pending->owner_task_index = owner_index;
    pending->waiters.waiters = 0U;
    pending->response = (scheduler_ipc_message_t){0};
    ++source->active_calls;
    ++destination->active_calls;
    ++ipc_diag_calls_started;
    *request_id_out = request_id;
    scheduler_irq_restore(flags);

    scheduler_ipc_message_t outgoing;
    scheduler_ipc_message_copy(&outgoing, request);
    outgoing.protocol_version = SCHEDULER_IPC_PROTOCOL_VERSION;
    outgoing.kind = SCHEDULER_IPC_KIND_REQUEST;
    outgoing.status = SCHEDULER_IPC_STATUS_OK;
    if (!scheduler_ipc_service_send_request(source_service_id, destination_service_id,
                                            request_id, &outgoing)) {
        const uint64_t cleanup_flags = scheduler_irq_save_disable();
        ++ipc_diag_calls_send_failed;
        scheduler_ipc_pending_release_locked(pending_index);
        scheduler_irq_restore(cleanup_flags);
        return false;
    }

    const uint64_t wait_flags = scheduler_irq_save_disable();
    pending = &ipc_pending_calls[pending_index];
    if (!pending->active) {
        scheduler_irq_restore(wait_flags);
        return false;
    }
    if (!pending->completed && !pending->expired && !pending->cancelled) {
        owner_index = current_task_index;
        if (owner_index >= SCHEDULER_MAX_TASKS ||
            task_table[owner_index].state != SCHEDULER_TASK_RUNNING ||
            task_table[owner_index].waiting_on != NULL) {
            scheduler_ipc_pending_release_locked(pending_index);
            scheduler_irq_restore(wait_flags);
            return false;
        }
        pending->owner_task_index = owner_index;
        pending->waiting = true;
        scheduler_task_t *owner = &task_table[owner_index];
        pending->waiters.waiters |= UINT32_C(1) << owner_index;
        owner->waiting_on = &pending->waiters;
        owner->wait_deadline = interrupts_ticks() + timeout_ticks;
        owner->has_deadline = true;
        owner->wait_result = SCHEDULER_WAITING;
        owner->state = SCHEDULER_TASK_BLOCKED;
        scheduler_irq_restore(wait_flags);
        while (task_table[owner_index].state == SCHEDULER_TASK_BLOCKED)
            __asm__ volatile ("sti; hlt; cli" : : : "memory");
    } else {
        scheduler_irq_restore(wait_flags);
    }

    const uint64_t finish_flags = scheduler_irq_save_disable();
    pending = &ipc_pending_calls[pending_index];
    bool success = pending->active && pending->completed &&
        !pending->expired && !pending->cancelled;
    if (pending->owner_task_index < SCHEDULER_MAX_TASKS) {
        const scheduler_wait_result_t result =
            task_table[pending->owner_task_index].wait_result;
        if (result == SCHEDULER_WAIT_TIMED_OUT) {
            pending->expired = true;
            scheduler_ipc_expired_record_locked(pending->source_service_id,
                pending->destination_service_id, pending->request_id);
            success = false;
        } else if (result == SCHEDULER_WAIT_CANCELLED) {
            pending->cancelled = true;
            pending->expired = true;
            scheduler_ipc_expired_record_locked(pending->source_service_id,
                pending->destination_service_id, pending->request_id);
            success = false;
        }
    }
    if (success) {
        scheduler_ipc_message_copy(response, &pending->response);
        success = scheduler_ipc_message_is_response_for(response, request_id) &&
                  response->source_service_id == destination_service_id;
    }
    scheduler_ipc_pending_release_locked(pending_index);
    scheduler_irq_restore(finish_flags);
    return success;
}

bool scheduler_ipc_call_cancel(scheduler_ipc_service_id_t source_service_id,
                               uint32_t request_id) {
    if (source_service_id == 0U || request_id == 0U) return false;
    const uint64_t flags = scheduler_irq_save_disable();
    for (uint32_t i = 0U; i < SCHEDULER_IPC_MAX_PENDING_CALLS; ++i) {
        scheduler_ipc_pending_call_t *pending = &ipc_pending_calls[i];
        if (!pending->active || pending->source_service_id != source_service_id ||
            pending->request_id != request_id) continue;
        if (pending->completed || pending->expired || pending->cancelled) {
            scheduler_irq_restore(flags);
            return false;
        }
        pending->cancelled = true;
        pending->expired = true;
        ++ipc_diag_calls_cancelled;
        scheduler_ipc_expired_record_locked(pending->source_service_id,
            pending->destination_service_id, pending->request_id);
        if (pending->owner_task_index < SCHEDULER_MAX_TASKS) {
            scheduler_task_t *owner = &task_table[pending->owner_task_index];
            const uint32_t bit = UINT32_C(1) << pending->owner_task_index;
            if (owner->state == SCHEDULER_TASK_BLOCKED && owner->waiting_on != NULL) {
                owner->waiting_on->waiters &= ~bit;
                owner->waiting_on = NULL;
                owner->has_deadline = false;
                owner->wait_deadline = 0U;
                owner->wait_result = SCHEDULER_WAIT_CANCELLED;
                owner->state = SCHEDULER_TASK_READY;
            } else if (owner->state == SCHEDULER_TASK_READY ||
                       owner->state == SCHEDULER_TASK_RUNNING) {
                owner->wait_result = SCHEDULER_WAIT_CANCELLED;
            }
        }
        scheduler_irq_restore(flags);
        return true;
    }
    scheduler_irq_restore(flags);
    return false;
}

uint32_t scheduler_ipc_pending_call_count(void) {
    const uint64_t flags = scheduler_irq_save_disable();
    uint32_t count = 0U;
    for (uint32_t i = 0U; i < SCHEDULER_IPC_MAX_PENDING_CALLS; ++i)
        if (ipc_pending_calls[i].active) ++count;
    scheduler_irq_restore(flags);
    return count;
}

void scheduler_ipc_get_diagnostics(scheduler_ipc_diagnostics_t *diagnostics) {
    if (diagnostics == NULL) return;
    const uint64_t flags = scheduler_irq_save_disable();
    diagnostics->calls_started = ipc_diag_calls_started;
    diagnostics->calls_completed = ipc_diag_calls_completed;
    diagnostics->calls_timed_out = ipc_diag_calls_timed_out;
    diagnostics->calls_cancelled = ipc_diag_calls_cancelled;
    diagnostics->calls_service_unavailable = ipc_diag_calls_service_unavailable;
    diagnostics->calls_send_failed = ipc_diag_calls_send_failed;
    diagnostics->replies_dropped = ipc_diag_replies_dropped;
    diagnostics->services_closed = ipc_diag_services_closed;
    diagnostics->messages_enqueued = ipc_diag_messages_enqueued;
    diagnostics->messages_received = ipc_diag_messages_received;
    diagnostics->backpressure_events = ipc_diag_backpressure_events;
    diagnostics->messages_dropped_newest = ipc_diag_messages_dropped_newest;
    diagnostics->messages_dropped_oldest = ipc_diag_messages_dropped_oldest;
    diagnostics->messages_rejected_full = ipc_diag_messages_rejected_full;
    diagnostics->delivery_failures = ipc_diag_delivery_failures;
    diagnostics->deliveries_started = ipc_diag_deliveries_started;
    diagnostics->deliveries_acknowledged = ipc_diag_deliveries_acknowledged;
    diagnostics->delivery_retry_attempts = ipc_diag_delivery_retry_attempts;
    diagnostics->delivery_retries_exhausted = ipc_diag_delivery_retries_exhausted;
    diagnostics->duplicate_deliveries_suppressed = ipc_diag_duplicate_deliveries_suppressed;
    diagnostics->automatic_retry_attempts = ipc_diag_automatic_retry_attempts;
    diagnostics->delivery_deadlines_expired = ipc_diag_delivery_deadlines_expired;
    diagnostics->dead_letters_enqueued = ipc_diag_dead_letters_enqueued;
    diagnostics->dead_letters_dropped = ipc_diag_dead_letters_dropped;
    diagnostics->service_health_successes = ipc_diag_service_health_successes;
    diagnostics->service_health_failures = ipc_diag_service_health_failures;
    diagnostics->circuit_breaker_trips = ipc_diag_circuit_breaker_trips;
    diagnostics->circuit_breaker_recoveries = ipc_diag_circuit_breaker_recoveries;
    diagnostics->circuit_open_rejections = ipc_diag_circuit_open_rejections;
    diagnostics->dead_letters_pending = ipc_dead_letter_count;
    diagnostics->pending_deliveries = 0U;
    diagnostics->max_queue_depth = ipc_diag_max_queue_depth;
    diagnostics->pending_calls = 0U;
    diagnostics->active_services = 0U;
    for (uint32_t i = 0U; i < SCHEDULER_IPC_MAX_PENDING_CALLS; ++i)
        if (ipc_pending_calls[i].active) ++diagnostics->pending_calls;
    for (uint32_t i = 0U; i < SCHEDULER_IPC_MAX_TRACKED_DELIVERIES; ++i)
        if (ipc_delivery_tracks[i].valid &&
            ipc_delivery_tracks[i].receipt.state == SCHEDULER_IPC_DELIVERY_STATE_PENDING)
            ++diagnostics->pending_deliveries;
    for (uint32_t i = 0U; i < SCHEDULER_IPC_MAX_SERVICES; ++i)
        if (ipc_services[i].active && !ipc_services[i].closing)
            ++diagnostics->active_services;
    scheduler_irq_restore(flags);
}

uint32_t scheduler_ipc_service_count(void) {
    const uint64_t flags = scheduler_irq_save_disable();
    uint32_t count = 0U;
    for (uint32_t i = 0U; i < SCHEDULER_IPC_MAX_SERVICES; ++i)
        if (ipc_services[i].active && !ipc_services[i].closing) ++count;
    scheduler_irq_restore(flags);
    return count;
}

static scheduler_ipc_service_id_t ipc_service_test_server_id;
static scheduler_ipc_service_id_t ipc_service_test_client_id;
static scheduler_ipc_service_id_t ipc_service_test_sink_id;
static volatile uint32_t ipc_service_test_requests_ok;
static volatile uint32_t ipc_service_test_send_ok;
static volatile uint32_t ipc_service_test_responses_ok;
static volatile uint32_t ipc_service_test_errors;

static void ipc_service_test_server(void) {
    for (uint32_t i = 0U; i < 2U; ++i) {
        scheduler_ipc_message_t request = {0};
        if (!scheduler_ipc_service_receive(ipc_service_test_server_id, &request)) {
            ++ipc_service_test_errors;
            return;
        }
        const uint32_t expected_id = UINT32_C(0x1234ABCD) + i;
        const uint8_t expected_first = i == 0U ? UINT8_C(0x11) : UINT8_C(0x44);
        const uint8_t expected_second = i == 0U ? UINT8_C(0x22) : UINT8_C(0x55);
        const uint8_t expected_third = i == 0U ? UINT8_C(0x33) : UINT8_C(0x66);
        if (request.protocol_version != SCHEDULER_IPC_PROTOCOL_VERSION ||
            request.kind != SCHEDULER_IPC_KIND_REQUEST ||
            request.status != SCHEDULER_IPC_STATUS_OK ||
            request.source_service_id != ipc_service_test_client_id ||
            request.request_id != expected_id ||
            request.type != UINT32_C(0x48595356) || request.length != 3U ||
            request.payload[0] != expected_first ||
            request.payload[1] != expected_second ||
            request.payload[2] != expected_third) {
            ++ipc_service_test_errors;
        } else {
            ++ipc_service_test_requests_ok;
        }

        scheduler_ipc_message_t response = {0};
        response.type = UINT32_C(0x5253504E);
        response.length = 2U;
        response.payload[0] = (uint8_t)i;
        response.payload[1] = (uint8_t)(UINT8_C(0xC0) + i);
        const scheduler_ipc_status_t status = i == 0U
            ? SCHEDULER_IPC_STATUS_OK : SCHEDULER_IPC_STATUS_UNSUPPORTED;
        if (!scheduler_ipc_service_reply(ipc_service_test_server_id,
                                         &request, &response, status))
            ++ipc_service_test_errors;
    }
}

static void ipc_service_test_client(void) {
    for (uint32_t i = 0U; i < 2U; ++i) {
        const uint32_t request_id = UINT32_C(0x1234ABCD) + i;
        scheduler_ipc_message_t request = {0};
        request.type = UINT32_C(0x48595356);
        request.length = 3U;
        request.payload[0] = i == 0U ? UINT8_C(0x11) : UINT8_C(0x44);
        request.payload[1] = i == 0U ? UINT8_C(0x22) : UINT8_C(0x55);
        request.payload[2] = i == 0U ? UINT8_C(0x33) : UINT8_C(0x66);
        if (!scheduler_ipc_service_send_request(ipc_service_test_client_id,
                ipc_service_test_server_id, request_id, &request)) {
            ++ipc_service_test_errors;
            return;
        }
        ++ipc_service_test_send_ok;

        scheduler_ipc_message_t response = {0};
        if (!scheduler_ipc_service_receive(ipc_service_test_client_id, &response)) {
            ++ipc_service_test_errors;
            return;
        }
        const bool response_ok = scheduler_ipc_message_is_response_for(&response, request_id) &&
            response.source_service_id == ipc_service_test_server_id &&
            response.type == UINT32_C(0x5253504E) && response.length == 2U &&
            response.payload[0] == (uint8_t)i &&
            response.payload[1] == (uint8_t)(UINT8_C(0xC0) + i);
        if (i == 0U) {
            if (!response_ok || response.kind != SCHEDULER_IPC_KIND_RESPONSE ||
                response.status != SCHEDULER_IPC_STATUS_OK)
                ++ipc_service_test_errors;
            else
                ++ipc_service_test_responses_ok;
        } else {
            if (!response_ok || response.kind != SCHEDULER_IPC_KIND_ERROR ||
                response.status != SCHEDULER_IPC_STATUS_UNSUPPORTED)
                ++ipc_service_test_errors;
            else
                ++ipc_service_test_responses_ok;
        }
        if (scheduler_ipc_message_is_response_for(&response, request_id + 100U))
            ++ipc_service_test_errors;
    }
}

bool scheduler_ipc_service_self_test(void) {
    if (scheduler_ipc_service_count() != 0U) return false;
    ipc_service_test_server_id = 0U;
    ipc_service_test_client_id = 0U;
    ipc_service_test_sink_id = 0U;
    ipc_service_test_requests_ok = 0U;
    ipc_service_test_send_ok = 0U;
    ipc_service_test_responses_ok = 0U;
    ipc_service_test_errors = 0U;
    if (scheduler_ipc_service_register(NULL, &ipc_service_test_server_id) ||
        scheduler_ipc_service_register("", &ipc_service_test_server_id) ||
        scheduler_ipc_service_register("name-that-is-too-long", &ipc_service_test_server_id) ||
        scheduler_ipc_service_register("hyb.server", NULL) ||
        scheduler_ipc_service_register_ex("hyb.invalid", UINT32_C(0x80000000),
                                           &ipc_service_test_server_id) ||
        scheduler_ipc_service_lookup(NULL, &ipc_service_test_server_id) ||
        scheduler_ipc_service_unregister(0U)) return false;
    if (!scheduler_ipc_service_register_ex("hyb.server",
            SCHEDULER_IPC_SERVICE_CAP_REQUEST_RESPONSE | SCHEDULER_IPC_SERVICE_CAP_ONE_WAY,
            &ipc_service_test_server_id) ||
        !scheduler_ipc_service_register_ex("hyb.client",
            SCHEDULER_IPC_SERVICE_CAP_REQUEST_RESPONSE | SCHEDULER_IPC_SERVICE_CAP_ONE_WAY,
            &ipc_service_test_client_id) ||
        !scheduler_ipc_service_register_ex("hyb.sink",
            SCHEDULER_IPC_SERVICE_CAP_ONE_WAY, &ipc_service_test_sink_id))
        return false;

    scheduler_ipc_service_id_t looked_up = 0U;
    if (!scheduler_ipc_service_lookup("hyb.server", &looked_up) ||
        looked_up != ipc_service_test_server_id ||
        scheduler_ipc_service_lookup("HYB.server", &looked_up) ||
        scheduler_ipc_service_lookup("hyb.unknown", &looked_up) ||
        scheduler_ipc_service_register("hyb.server", &looked_up) ||
        scheduler_ipc_service_count() != 3U ||
        scheduler_ipc_service_send(0U, ipc_service_test_server_id, 1U, NULL))
        return false;

    scheduler_ipc_service_info_t info = {0};
    if (!scheduler_ipc_service_enumerate(0U, &info) ||
        info.id != ipc_service_test_server_id ||
        !scheduler_ipc_name_equal(info.name, "hyb.server") ||
        info.capabilities != (SCHEDULER_IPC_SERVICE_CAP_REQUEST_RESPONSE |
                              SCHEDULER_IPC_SERVICE_CAP_ONE_WAY) ||
        info.queue_capacity != SCHEDULER_IPC_CAPACITY ||
        info.queued_messages != 0U ||
        !scheduler_ipc_service_enumerate(1U, &info) ||
        info.id != ipc_service_test_client_id ||
        !scheduler_ipc_name_equal(info.name, "hyb.client") ||
        !scheduler_ipc_service_enumerate(2U, &info) ||
        info.id != ipc_service_test_sink_id ||
        !scheduler_ipc_name_equal(info.name, "hyb.sink") ||
        scheduler_ipc_service_enumerate(3U, &info) ||
        !scheduler_ipc_service_get_info(ipc_service_test_server_id, &info) ||
        info.id != ipc_service_test_server_id ||
        scheduler_ipc_service_get_info(UINT32_MAX, &info))
        return false;

    scheduler_ipc_message_t invalid_request = {0};
    if (scheduler_ipc_service_send_request(ipc_service_test_client_id,
            ipc_service_test_server_id, 0U, &invalid_request) ||
        scheduler_ipc_service_send_request(ipc_service_test_client_id,
            UINT32_MAX, 9U, &invalid_request))
        return false;
    if (scheduler_ipc_service_send_request(ipc_service_test_client_id,
            ipc_service_test_server_id, 1U, NULL)) return false;

    scheduler_init();
    scheduler_task_id_t server = 0U, client = 0U;
    if (!scheduler_task_create(ipc_service_test_server, &server) ||
        !scheduler_task_create(ipc_service_test_client, &client)) return false;
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

    const bool task_ok = waited && !preemptive_active && bootstrap_frame == NULL &&
        scheduler_task_state(server) == SCHEDULER_TASK_EXITED &&
        scheduler_task_state(client) == SCHEDULER_TASK_EXITED &&
        ipc_service_test_requests_ok == 2U && ipc_service_test_send_ok == 2U &&
        ipc_service_test_responses_ok == 2U &&
        ipc_service_test_errors == 0U && scheduler_failed_count() == 0U &&
        scheduler_task_reap(server) && scheduler_task_reap(client);
    if (!task_ok) {
        serial_write("HYBRID: IPC_REQUEST_RESPONSE_DIAG states=");
        serial_write_hex64(scheduler_task_state(server));
        serial_write(",");
        serial_write_hex64(scheduler_task_state(client));
        serial_write(" requests="); serial_write_hex64(ipc_service_test_requests_ok);
        serial_write(" sends="); serial_write_hex64(ipc_service_test_send_ok);
        serial_write(" responses="); serial_write_hex64(ipc_service_test_responses_ok);
        serial_write(" errors="); serial_write_hex64(ipc_service_test_errors);
        serial_write("\n");
    }
    if (!task_ok || !scheduler_ipc_service_send(ipc_service_test_client_id,
          ipc_service_test_sink_id, 7U,
          &(scheduler_ipc_message_t){.type = 9U, .length = 0U}))
        return false;
    /* v0.12.0 unregister actively closes the endpoint and drops queued work. */
    if (!scheduler_ipc_service_unregister(ipc_service_test_sink_id)) return false;
    scheduler_ipc_message_t sink_message = {0};
    if (scheduler_ipc_service_receive(ipc_service_test_sink_id, &sink_message) ||
        scheduler_ipc_service_unregister(ipc_service_test_sink_id) ||
        !scheduler_ipc_service_unregister(ipc_service_test_server_id) ||
        !scheduler_ipc_service_unregister(ipc_service_test_client_id) ||
        scheduler_ipc_service_count() != 0U)
        return false;
    return true;
}



static scheduler_ipc_service_id_t ipc_runtime_client_id;
static scheduler_ipc_service_id_t ipc_runtime_server_id;
static scheduler_ipc_service_id_t ipc_runtime_timeout_id;
static scheduler_ipc_service_id_t ipc_runtime_cancel_id;
static volatile uint32_t ipc_runtime_request_a;
static volatile uint32_t ipc_runtime_request_b;
static volatile uint32_t ipc_runtime_timeout_request;
static volatile uint32_t ipc_runtime_cancel_request;
static volatile uint32_t ipc_runtime_client_a_ok;
static volatile uint32_t ipc_runtime_client_b_ok;
static volatile uint32_t ipc_runtime_timeout_ok;
static volatile uint32_t ipc_runtime_cancel_ok;
static volatile uint32_t ipc_runtime_cancel_applied;
static volatile uint32_t ipc_runtime_cancel_server_received;
static volatile uint32_t ipc_runtime_late_timeout_dropped;
static volatile uint32_t ipc_runtime_late_cancel_dropped;
static volatile uint32_t ipc_runtime_bad_reply_dropped;
static volatile uint32_t ipc_runtime_errors;

static void ipc_runtime_client_a(void) {
    scheduler_ipc_message_t request = {0}, response = {0};
    request.type = UINT32_C(0x52544341);
    request.length = 1U;
    request.payload[0] = UINT8_C(0xA1);
    ipc_runtime_client_a_ok =
        scheduler_ipc_service_call(ipc_runtime_client_id, ipc_runtime_server_id,
            &request, 24U, &response, &ipc_runtime_request_a) &&
        response.request_id == ipc_runtime_request_a &&
        response.source_service_id == ipc_runtime_server_id &&
        response.kind == SCHEDULER_IPC_KIND_RESPONSE &&
        response.status == SCHEDULER_IPC_STATUS_OK &&
        response.length == 1U && response.payload[0] == UINT8_C(0xA1);
    if (!ipc_runtime_client_a_ok) ++ipc_runtime_errors;
}

static void ipc_runtime_client_b(void) {
    scheduler_ipc_message_t request = {0}, response = {0};
    request.type = UINT32_C(0x52544342);
    request.length = 1U;
    request.payload[0] = UINT8_C(0xB2);
    ipc_runtime_client_b_ok =
        scheduler_ipc_service_call(ipc_runtime_client_id, ipc_runtime_server_id,
            &request, 24U, &response, &ipc_runtime_request_b) &&
        response.request_id == ipc_runtime_request_b &&
        response.source_service_id == ipc_runtime_server_id &&
        response.kind == SCHEDULER_IPC_KIND_ERROR &&
        response.status == SCHEDULER_IPC_STATUS_UNSUPPORTED &&
        response.length == 1U && response.payload[0] == UINT8_C(0xB2);
    if (!ipc_runtime_client_b_ok) ++ipc_runtime_errors;
}

static void ipc_runtime_timeout_client(void) {
    scheduler_ipc_message_t request = {0}, response = {0};
    request.type = UINT32_C(0x54494D45);
    request.length = 1U;
    request.payload[0] = UINT8_C(0x54);
    ipc_runtime_timeout_ok =
        !scheduler_ipc_service_call(ipc_runtime_client_id, ipc_runtime_timeout_id,
            &request, 4U, &response, &ipc_runtime_timeout_request);
    if (!ipc_runtime_timeout_ok) ++ipc_runtime_errors;
}

static void ipc_runtime_cancel_client(void) {
    scheduler_ipc_message_t request = {0}, response = {0};
    request.type = UINT32_C(0x43414E43);
    request.length = 1U;
    request.payload[0] = UINT8_C(0x43);
    ipc_runtime_cancel_ok =
        !scheduler_ipc_service_call(ipc_runtime_client_id, ipc_runtime_cancel_id,
            &request, 28U, &response, &ipc_runtime_cancel_request);
    if (!ipc_runtime_cancel_ok) ++ipc_runtime_errors;
}

static void ipc_runtime_server(void) {
    scheduler_ipc_message_t first = {0}, second = {0};
    if (!scheduler_ipc_service_receive(ipc_runtime_server_id, &first) ||
        !scheduler_ipc_service_receive(ipc_runtime_server_id, &second)) {
        ++ipc_runtime_errors;
        return;
    }
    if (first.kind != SCHEDULER_IPC_KIND_REQUEST ||
        second.kind != SCHEDULER_IPC_KIND_REQUEST ||
        first.request_id == 0U || second.request_id == 0U ||
        first.request_id == second.request_id ||
        first.source_service_id != ipc_runtime_client_id ||
        second.source_service_id != ipc_runtime_client_id) {
        ++ipc_runtime_errors;
        return;
    }
    scheduler_ipc_message_t wrong = first;
    wrong.request_id = first.request_id ^ UINT32_C(0x00010000);
    scheduler_ipc_message_t bad_response = {0};
    if (!scheduler_ipc_service_reply(ipc_runtime_server_id, &wrong,
            &bad_response, SCHEDULER_IPC_STATUS_OK))
        ++ipc_runtime_bad_reply_dropped;
    else
        ++ipc_runtime_errors;

    scheduler_ipc_message_t response_b = {0};
    response_b.type = UINT32_C(0x52535042);
    response_b.length = 1U;
    response_b.payload[0] = second.payload[0];
    const scheduler_ipc_status_t status_b = second.payload[0] == UINT8_C(0xB2)
        ? SCHEDULER_IPC_STATUS_UNSUPPORTED : SCHEDULER_IPC_STATUS_OK;
    if (!scheduler_ipc_service_reply(ipc_runtime_server_id, &second,
                                     &response_b, status_b))
        ++ipc_runtime_errors;

    scheduler_ipc_message_t response_a = {0};
    response_a.type = UINT32_C(0x52535041);
    response_a.length = 1U;
    response_a.payload[0] = first.payload[0];
    const scheduler_ipc_status_t status_a = first.payload[0] == UINT8_C(0xA1)
        ? SCHEDULER_IPC_STATUS_OK : SCHEDULER_IPC_STATUS_UNSUPPORTED;
    if (!scheduler_ipc_service_reply(ipc_runtime_server_id, &first,
                                     &response_a, status_a))
        ++ipc_runtime_errors;
}

static void ipc_runtime_timeout_server(void) {
    scheduler_ipc_message_t request = {0}, response = {0};
    if (!scheduler_ipc_service_receive(ipc_runtime_timeout_id, &request) ||
        request.request_id == 0U) {
        ++ipc_runtime_errors;
        return;
    }
    if (!scheduler_sleep_ticks(9U)) { ++ipc_runtime_errors; return; }
    response.type = UINT32_C(0x4C415445);
    response.length = 1U;
    response.payload[0] = UINT8_C(0x54);
    if (!scheduler_ipc_service_reply(ipc_runtime_timeout_id, &request, &response,
                                     SCHEDULER_IPC_STATUS_OK))
        ++ipc_runtime_late_timeout_dropped;
    else
        ++ipc_runtime_errors;
}

static void ipc_runtime_cancel_server(void) {
    scheduler_ipc_message_t request = {0}, response = {0};
    if (!scheduler_ipc_service_receive(ipc_runtime_cancel_id, &request)) {
        ++ipc_runtime_errors;
        return;
    }
    ipc_runtime_cancel_server_received = 1U;
    if (!scheduler_sleep_ticks(9U)) { ++ipc_runtime_errors; return; }
    response.type = UINT32_C(0x4C415445);
    response.length = 1U;
    response.payload[0] = UINT8_C(0x43);
    if (!scheduler_ipc_service_reply(ipc_runtime_cancel_id, &request, &response,
                                     SCHEDULER_IPC_STATUS_OK))
        ++ipc_runtime_late_cancel_dropped;
    else
        ++ipc_runtime_errors;
}

static void ipc_runtime_cancel_controller(void) {
    uint64_t guard = 0U;
    while (ipc_runtime_cancel_server_received == 0U &&
           guard++ < UINT64_C(1000000))
        __asm__ volatile ("pause");
    if (ipc_runtime_cancel_server_received == 0U ||
        ipc_runtime_cancel_request == 0U) {
        ++ipc_runtime_errors;
        return;
    }
    if (!scheduler_sleep_ticks(2U)) { ++ipc_runtime_errors; return; }
    if (scheduler_ipc_call_cancel(ipc_runtime_client_id, ipc_runtime_cancel_request))
        ipc_runtime_cancel_applied = 1U;
    else
        ++ipc_runtime_errors;
}

bool scheduler_ipc_runtime_self_test(void) {
    scheduler_init();
    if (scheduler_ipc_service_count() != 0U) return false;
    ipc_runtime_client_a_ok = 0U;
    ipc_runtime_client_b_ok = 0U;
    ipc_runtime_timeout_ok = 0U;
    ipc_runtime_cancel_ok = 0U;
    ipc_runtime_cancel_applied = 0U;
    ipc_runtime_cancel_server_received = 0U;
    ipc_runtime_late_timeout_dropped = 0U;
    ipc_runtime_late_cancel_dropped = 0U;
    ipc_runtime_bad_reply_dropped = 0U;
    ipc_runtime_errors = 0U;
    ipc_runtime_request_a = 0U;
    ipc_runtime_request_b = 0U;
    ipc_runtime_timeout_request = 0U;
    ipc_runtime_cancel_request = 0U;

    const uint32_t caps = SCHEDULER_IPC_SERVICE_CAP_ONE_WAY |
                          SCHEDULER_IPC_SERVICE_CAP_REQUEST_RESPONSE;
    if (!scheduler_ipc_service_register_ex("rt.client", caps, &ipc_runtime_client_id) ||
        !scheduler_ipc_service_register_ex("rt.server", caps, &ipc_runtime_server_id) ||
        !scheduler_ipc_service_register_ex("rt.timeout", caps, &ipc_runtime_timeout_id) ||
        !scheduler_ipc_service_register_ex("rt.cancel", caps, &ipc_runtime_cancel_id))
        return false;

    scheduler_task_id_t client_a = 0U, client_b = 0U, timeout_client = 0U,
        cancel_client = 0U, server = 0U, timeout_server = 0U, cancel_server = 0U,
        cancel_controller = 0U;
    if (!scheduler_task_create(ipc_runtime_client_a, &client_a) ||
        !scheduler_task_create(ipc_runtime_client_b, &client_b) ||
        !scheduler_task_create(ipc_runtime_timeout_client, &timeout_client) ||
        !scheduler_task_create(ipc_runtime_cancel_client, &cancel_client) ||
        !scheduler_task_create(ipc_runtime_server, &server) ||
        !scheduler_task_create(ipc_runtime_timeout_server, &timeout_server) ||
        !scheduler_task_create(ipc_runtime_cancel_server, &cancel_server) ||
        !scheduler_task_create(ipc_runtime_cancel_controller, &cancel_controller))
        return false;

    preemptive_active = true;
    bootstrap_frame = NULL;
    current_task_index = SCHEDULER_NO_TASK;
    preemptive_ticks = 0U;
    preemptive_switches = 0U;
    preemptive_tick_limit = 64U;
    const uint64_t target = interrupts_ticks() + 64U;
    interrupts_enable();
    const bool waited = interrupts_wait_for_ticks(target);
    interrupts_disable();

    const bool states_ok = waited && !preemptive_active && bootstrap_frame == NULL &&
        scheduler_task_state(client_a) == SCHEDULER_TASK_EXITED &&
        scheduler_task_state(client_b) == SCHEDULER_TASK_EXITED &&
        scheduler_task_state(timeout_client) == SCHEDULER_TASK_EXITED &&
        scheduler_task_state(cancel_client) == SCHEDULER_TASK_EXITED &&
        scheduler_task_state(server) == SCHEDULER_TASK_EXITED &&
        scheduler_task_state(timeout_server) == SCHEDULER_TASK_EXITED &&
        scheduler_task_state(cancel_server) == SCHEDULER_TASK_EXITED &&
        scheduler_task_state(cancel_controller) == SCHEDULER_TASK_EXITED;
    const bool results_ok = ipc_runtime_client_a_ok == 1U &&
        ipc_runtime_client_b_ok == 1U && ipc_runtime_timeout_ok == 1U &&
        ipc_runtime_cancel_ok == 1U && ipc_runtime_cancel_applied == 1U &&
        ipc_runtime_late_timeout_dropped == 1U &&
        ipc_runtime_late_cancel_dropped == 1U &&
        ipc_runtime_bad_reply_dropped == 1U &&
        ipc_runtime_errors == 0U && scheduler_failed_count() == 0U &&
        scheduler_ipc_pending_call_count() == 0U &&
        scheduler_ipc_service_count() == 4U;
    const bool reaped = states_ok && results_ok &&
        scheduler_task_reap(client_a) && scheduler_task_reap(client_b) &&
        scheduler_task_reap(timeout_client) && scheduler_task_reap(cancel_client) &&
        scheduler_task_reap(server) && scheduler_task_reap(timeout_server) &&
        scheduler_task_reap(cancel_server) && scheduler_task_reap(cancel_controller);

    if (!states_ok || !results_ok || !reaped) {
        serial_write("HYBRID: IPC_RUNTIME_DIAG states=");
        serial_write_hex64(scheduler_task_state(client_a)); serial_write(",");
        serial_write_hex64(scheduler_task_state(client_b)); serial_write(",");
        serial_write_hex64(scheduler_task_state(timeout_client)); serial_write(",");
        serial_write_hex64(scheduler_task_state(cancel_client)); serial_write(",");
        serial_write_hex64(scheduler_task_state(server)); serial_write(",");
        serial_write_hex64(scheduler_task_state(timeout_server)); serial_write(",");
        serial_write_hex64(scheduler_task_state(cancel_server)); serial_write(",");
        serial_write_hex64(scheduler_task_state(cancel_controller));
        serial_write(" results="); serial_write_hex64(ipc_runtime_client_a_ok);
        serial_write(","); serial_write_hex64(ipc_runtime_client_b_ok);
        serial_write(","); serial_write_hex64(ipc_runtime_timeout_ok);
        serial_write(","); serial_write_hex64(ipc_runtime_cancel_ok);
        serial_write(" cancel="); serial_write_hex64(ipc_runtime_cancel_applied);
        serial_write(" late="); serial_write_hex64(ipc_runtime_late_timeout_dropped);
        serial_write(","); serial_write_hex64(ipc_runtime_late_cancel_dropped);
        serial_write(" bad="); serial_write_hex64(ipc_runtime_bad_reply_dropped);
        serial_write(" pending="); serial_write_hex64(scheduler_ipc_pending_call_count());
        serial_write(" errors="); serial_write_hex64(ipc_runtime_errors);
        serial_write("\n");
    }

    if (!reaped) return false;
    const bool unregistered = scheduler_ipc_service_unregister(ipc_runtime_client_id) &&
        scheduler_ipc_service_unregister(ipc_runtime_server_id) &&
        scheduler_ipc_service_unregister(ipc_runtime_timeout_id) &&
        scheduler_ipc_service_unregister(ipc_runtime_cancel_id);
    return unregistered && scheduler_ipc_service_count() == 0U;
}


static scheduler_ipc_service_id_t ipc_lifecycle_client_id;
static scheduler_ipc_service_id_t ipc_lifecycle_server_id;
static volatile uint32_t ipc_lifecycle_server_received;
static volatile uint32_t ipc_lifecycle_client_ok;
static volatile uint32_t ipc_lifecycle_unregister_ok;
static volatile uint32_t ipc_lifecycle_errors;

static void ipc_lifecycle_client(void) {
    scheduler_ipc_message_t request = {0}, response = {0};
    volatile uint32_t request_id = 0U;
    request.type = UINT32_C(0x4C494645);
    request.length = 1U;
    request.payload[0] = UINT8_C(0x12);
    const bool called = scheduler_ipc_service_call(ipc_lifecycle_client_id,
        ipc_lifecycle_server_id, &request, 24U, &response, &request_id);
    ipc_lifecycle_client_ok = called &&
        response.kind == SCHEDULER_IPC_KIND_ERROR &&
        response.status == SCHEDULER_IPC_STATUS_NOT_FOUND &&
        response.source_service_id == ipc_lifecycle_server_id &&
        response.request_id == request_id;
    if (!ipc_lifecycle_client_ok) ++ipc_lifecycle_errors;
}

static void ipc_lifecycle_server(void) {
    scheduler_ipc_message_t request = {0};
    if (scheduler_ipc_service_receive(ipc_lifecycle_server_id, &request) &&
        request.kind == SCHEDULER_IPC_KIND_REQUEST &&
        request.source_service_id == ipc_lifecycle_client_id &&
        request.request_id != 0U) {
        ipc_lifecycle_server_received = 1U;
    } else {
        ++ipc_lifecycle_errors;
    }
}

static void ipc_lifecycle_controller(void) {
    uint64_t guard = 0U;
    while (ipc_lifecycle_server_received == 0U && guard++ < UINT64_C(1000000))
        __asm__ volatile ("pause");
    if (ipc_lifecycle_server_received == 0U) {
        ++ipc_lifecycle_errors;
        return;
    }
    ipc_lifecycle_unregister_ok =
        scheduler_ipc_service_unregister(ipc_lifecycle_server_id) ? 1U : 0U;
    if (!ipc_lifecycle_unregister_ok) ++ipc_lifecycle_errors;
}


bool scheduler_ipc_backpressure_self_test(void) {
    if (scheduler_ipc_service_count() != 0U) return false;
    scheduler_ipc_diagnostics_t before = {0};
    scheduler_ipc_get_diagnostics(&before);
    scheduler_ipc_service_id_t source_id = 0U, destination_id = 0U;
    const uint32_t caps = SCHEDULER_IPC_SERVICE_CAP_ONE_WAY;
    if (!scheduler_ipc_service_register_ex("queue.src", caps, &source_id) ||
        !scheduler_ipc_service_register_ex("queue.dst", caps, &destination_id))
        return false;

    scheduler_ipc_service_info_t info = {0};
    if (!scheduler_ipc_service_get_info(destination_id, &info) ||
        info.queue_policy != SCHEDULER_IPC_QUEUE_BLOCK ||
        info.queued_messages != 0U) return false;

    /* A nonblocking send reports pressure even when the service's legacy policy blocks. */
    for (uint32_t i = 1U; i <= SCHEDULER_IPC_CAPACITY; ++i) {
        scheduler_ipc_message_t message = {0};
        message.type = UINT32_C(0x51554555);
        message.length = 1U;
        message.payload[0] = (uint8_t)i;
        if (scheduler_ipc_service_send_ex(source_id, destination_id, i, &message) !=
            SCHEDULER_IPC_DELIVERY_DELIVERED) return false;
    }
    scheduler_ipc_message_t message = {0};
    message.type = UINT32_C(0x51554555);
    if (scheduler_ipc_service_send_ex(source_id, destination_id, 99U, &message) !=
        SCHEDULER_IPC_DELIVERY_BACKPRESSURE) return false;
    for (uint32_t i = 1U; i <= SCHEDULER_IPC_CAPACITY; ++i) {
        scheduler_ipc_message_t received = {0};
        if (!scheduler_ipc_service_receive(destination_id, &received) ||
            received.request_id != i || received.payload[0] != (uint8_t)i)
            return false;
    }

    if (!scheduler_ipc_service_set_queue_policy(destination_id,
                                                 SCHEDULER_IPC_QUEUE_REJECT))
        return false;
    for (uint32_t i = 1U; i <= SCHEDULER_IPC_CAPACITY; ++i) {
        message.payload[0] = (uint8_t)i;
        if (scheduler_ipc_service_send_ex(source_id, destination_id, 100U + i,
                &message) != SCHEDULER_IPC_DELIVERY_DELIVERED) return false;
    }
    if (scheduler_ipc_service_send_ex(source_id, destination_id, 199U, &message) !=
        SCHEDULER_IPC_DELIVERY_BACKPRESSURE) return false;
    for (uint32_t i = 1U; i <= SCHEDULER_IPC_CAPACITY; ++i) {
        scheduler_ipc_message_t received = {0};
        if (!scheduler_ipc_service_receive(destination_id, &received) ||
            received.request_id != 100U + i) return false;
    }

    if (!scheduler_ipc_service_set_queue_policy(destination_id,
                                                 SCHEDULER_IPC_QUEUE_DROP_NEWEST))
        return false;
    for (uint32_t i = 1U; i <= SCHEDULER_IPC_CAPACITY; ++i) {
        message.payload[0] = (uint8_t)i;
        if (scheduler_ipc_service_send_ex(source_id, destination_id, 200U + i,
                &message) != SCHEDULER_IPC_DELIVERY_DELIVERED) return false;
    }
    if (scheduler_ipc_service_send_ex(source_id, destination_id, 299U, &message) !=
        SCHEDULER_IPC_DELIVERY_DROPPED_NEWEST) return false;
    for (uint32_t i = 1U; i <= SCHEDULER_IPC_CAPACITY; ++i) {
        scheduler_ipc_message_t received = {0};
        if (!scheduler_ipc_service_receive(destination_id, &received) ||
            received.request_id != 200U + i) return false;
    }

    if (!scheduler_ipc_service_set_queue_policy(destination_id,
                                                 SCHEDULER_IPC_QUEUE_DROP_OLDEST))
        return false;
    for (uint32_t i = 1U; i <= SCHEDULER_IPC_CAPACITY; ++i) {
        message.payload[0] = (uint8_t)i;
        if (scheduler_ipc_service_send_ex(source_id, destination_id, 300U + i,
                &message) != SCHEDULER_IPC_DELIVERY_DELIVERED) return false;
    }
    if (scheduler_ipc_service_send_ex(source_id, destination_id, 305U, &message) !=
        SCHEDULER_IPC_DELIVERY_DELIVERED_DROPPED_OLDEST) return false;
    for (uint32_t i = 2U; i <= SCHEDULER_IPC_CAPACITY + 1U; ++i) {
        scheduler_ipc_message_t received = {0};
        const uint32_t expected_id = 300U + i;
        if (!scheduler_ipc_service_receive(destination_id, &received) ||
            received.request_id != expected_id) return false;
    }

    if (!scheduler_ipc_service_get_info(destination_id, &info) ||
        info.queued_messages != 0U || info.queue_capacity != SCHEDULER_IPC_CAPACITY ||
        info.queue_policy != SCHEDULER_IPC_QUEUE_DROP_OLDEST ||
        info.queue_high_watermark != SCHEDULER_IPC_CAPACITY ||
        info.messages_enqueued != 17U || info.messages_received != 16U ||
        info.backpressure_events != 4U || info.messages_dropped_newest != 1U ||
        info.messages_dropped_oldest != 1U || info.messages_rejected_full != 2U)
        return false;

    scheduler_ipc_diagnostics_t after = {0};
    scheduler_ipc_get_diagnostics(&after);
    if (after.messages_enqueued != before.messages_enqueued + 17U ||
        after.messages_received != before.messages_received + 16U ||
        after.backpressure_events != before.backpressure_events + 4U ||
        after.messages_dropped_newest != before.messages_dropped_newest + 1U ||
        after.messages_dropped_oldest != before.messages_dropped_oldest + 1U ||
        after.messages_rejected_full != before.messages_rejected_full + 2U ||
        after.max_queue_depth < SCHEDULER_IPC_CAPACITY)
        return false;

    return scheduler_ipc_service_unregister(destination_id) &&
           scheduler_ipc_service_unregister(source_id) &&
           scheduler_ipc_service_count() == 0U;
}

bool scheduler_ipc_lifecycle_self_test(void) {
    scheduler_init();
    if (scheduler_ipc_service_count() != 0U) return false;
    scheduler_ipc_diagnostics_t before = {0};
    scheduler_ipc_get_diagnostics(&before);
    ipc_lifecycle_server_received = 0U;
    ipc_lifecycle_client_ok = 0U;
    ipc_lifecycle_unregister_ok = 0U;
    ipc_lifecycle_errors = 0U;

    const uint32_t caps = SCHEDULER_IPC_SERVICE_CAP_ONE_WAY |
                          SCHEDULER_IPC_SERVICE_CAP_REQUEST_RESPONSE;
    if (!scheduler_ipc_service_register_ex("life.client", caps, &ipc_lifecycle_client_id) ||
        !scheduler_ipc_service_register_ex("life.server", caps, &ipc_lifecycle_server_id))
        return false;

    scheduler_task_id_t client = 0U, server = 0U, controller = 0U;
    if (!scheduler_task_create(ipc_lifecycle_client, &client) ||
        !scheduler_task_create(ipc_lifecycle_server, &server) ||
        !scheduler_task_create(ipc_lifecycle_controller, &controller))
        return false;

    preemptive_active = true;
    bootstrap_frame = NULL;
    current_task_index = SCHEDULER_NO_TASK;
    preemptive_ticks = 0U;
    preemptive_switches = 0U;
    preemptive_tick_limit = 40U;
    const uint64_t target = interrupts_ticks() + 40U;
    interrupts_enable();
    const bool waited = interrupts_wait_for_ticks(target);
    interrupts_disable();

    scheduler_ipc_diagnostics_t after = {0};
    scheduler_ipc_get_diagnostics(&after);
    const bool tasks_ok = waited && !preemptive_active && bootstrap_frame == NULL &&
        scheduler_task_state(client) == SCHEDULER_TASK_EXITED &&
        scheduler_task_state(server) == SCHEDULER_TASK_EXITED &&
        scheduler_task_state(controller) == SCHEDULER_TASK_EXITED;
    const bool result_ok = ipc_lifecycle_server_received == 1U &&
        ipc_lifecycle_unregister_ok == 1U && ipc_lifecycle_client_ok == 1U &&
        ipc_lifecycle_errors == 0U && after.pending_calls == 0U &&
        after.calls_service_unavailable == before.calls_service_unavailable + 1U &&
        scheduler_ipc_service_count() == 1U;
    const bool reaped = tasks_ok && result_ok &&
        scheduler_task_reap(client) && scheduler_task_reap(server) &&
        scheduler_task_reap(controller);
    if (!tasks_ok || !result_ok || !reaped) {
        serial_write("HYBRID: IPC_LIFECYCLE_DIAG tasks=");
        serial_write_hex64(scheduler_task_state(client)); serial_write(",");
        serial_write_hex64(scheduler_task_state(server)); serial_write(",");
        serial_write_hex64(scheduler_task_state(controller));
        serial_write(" result="); serial_write_hex64(ipc_lifecycle_client_ok);
        serial_write(","); serial_write_hex64(ipc_lifecycle_unregister_ok);
        serial_write(" pending="); serial_write_hex64(after.pending_calls);
        serial_write(" unavailable="); serial_write_hex64(after.calls_service_unavailable);
        serial_write(" errors="); serial_write_hex64(ipc_lifecycle_errors);
        serial_write("\n");
    }
    if (!reaped) return false;
    const bool cleanup = scheduler_ipc_service_unregister(ipc_lifecycle_client_id);
    return cleanup && scheduler_ipc_service_count() == 0U;
}

bool scheduler_ipc_request_response_discovery_self_test(void) {
    if (scheduler_ipc_service_count() != 0U) return false;
    scheduler_ipc_service_info_t empty_info = {0};
    if (scheduler_ipc_service_enumerate(0U, &empty_info)) return false;

    scheduler_ipc_message_t probe = {0};
    probe.protocol_version = SCHEDULER_IPC_PROTOCOL_VERSION;
    probe.kind = SCHEDULER_IPC_KIND_RESPONSE;
    probe.status = SCHEDULER_IPC_STATUS_OK;
    probe.request_id = UINT32_C(0x778899AA);
    if (!scheduler_ipc_message_is_response_for(&probe, UINT32_C(0x778899AA)) ||
        scheduler_ipc_message_is_response_for(&probe, UINT32_C(0x778899AB)) ||
        scheduler_ipc_message_is_response_for(&probe, 0U))
        return false;

    probe.kind = SCHEDULER_IPC_KIND_RESPONSE;
    probe.status = SCHEDULER_IPC_STATUS_UNSUPPORTED;
    if (scheduler_ipc_message_is_response_for(&probe, UINT32_C(0x778899AA)))
        return false;
    probe.kind = SCHEDULER_IPC_KIND_ERROR;
    probe.status = SCHEDULER_IPC_STATUS_OK;
    if (scheduler_ipc_message_is_response_for(&probe, UINT32_C(0x778899AA)))
        return false;
    probe.kind = SCHEDULER_IPC_KIND_ERROR;
    probe.status = SCHEDULER_IPC_STATUS_UNSUPPORTED;
    if (!scheduler_ipc_message_is_response_for(&probe, UINT32_C(0x778899AA)))
        return false;
    probe.kind = SCHEDULER_IPC_KIND_REQUEST;
    if (scheduler_ipc_message_is_response_for(&probe, UINT32_C(0x778899AA)))
        return false;
    probe.kind = SCHEDULER_IPC_KIND_RESPONSE;
    probe.status = (scheduler_ipc_status_t)(SCHEDULER_IPC_STATUS_INTERNAL + 1U);
    if (scheduler_ipc_message_is_response_for(&probe, UINT32_C(0x778899AA)))
        return false;
    probe.status = SCHEDULER_IPC_STATUS_OK;
    probe.kind = SCHEDULER_IPC_KIND_RESPONSE;
    probe.protocol_version = SCHEDULER_IPC_PROTOCOL_VERSION + 1U;
    if (scheduler_ipc_message_is_response_for(&probe, UINT32_C(0x778899AA)))
        return false;

    /* Re-run the end-to-end service test after the v0.9 marker to exercise
       service discovery, request correlation, OK response and typed error response. */
    if (!scheduler_ipc_service_self_test()) return false;
    return scheduler_ipc_service_count() == 0U;
}

void *scheduler_on_timer_interrupt(void *interrupt_frame) {
    if (interrupt_frame == NULL) return interrupt_frame;
    scheduler_ipc_process_delivery_timers(interrupts_ticks());
    if (!preemptive_active) return interrupt_frame;
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

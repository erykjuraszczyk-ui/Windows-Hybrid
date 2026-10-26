#include "scheduler.h"
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
static bool scheduler_ever_initialized;
static volatile uint32_t current_cooperative_index;
static uint64_t dispatch_count;
static uint64_t completed_count;
static uint64_t failed_count;
#define SCHEDULER_NO_TASK UINT32_MAX
#define SCHEDULER_PREEMPTIVE_TEST_TICKS 12U
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
    current_cooperative_index = SCHEDULER_NO_TASK;
    preemptive_ticks = 0U;
    preemptive_switches = 0U;
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
    *task_id = task->id;
    scheduler_irq_restore(flags);
    return true;
}

bool scheduler_run_once(void) {
    const uint64_t select_flags = scheduler_irq_save_disable();
    if (dispatch_in_progress || preemptive_active) {
        scheduler_irq_restore(select_flags);
        return false;
    }
    scheduler_task_t *selected = NULL;
    uint32_t selected_index = SCHEDULER_NO_TASK;
    for (uint32_t offset = 0U; offset < SCHEDULER_MAX_TASKS; ++offset) {
        const uint32_t index = (scheduler_cursor + offset) % SCHEDULER_MAX_TASKS;
        if (task_table[index].state == SCHEDULER_TASK_READY &&
            task_table[index].step != NULL) {
            selected = &task_table[index];
            selected_index = index;
            break;
        }
    }
    if (selected == NULL) {
        scheduler_irq_restore(select_flags);
        return false;
    }
    selected->state = SCHEDULER_TASK_RUNNING;
    dispatch_in_progress = true;
    current_cooperative_index = selected_index;
    scheduler_irq_restore(select_flags);

    /* Never hold the scheduler critical section while executing task code. */
    const scheduler_step_result_t result = selected->step(selected->context);

    const uint64_t finish_flags = scheduler_irq_save_disable();
    dispatch_in_progress = false;
    current_cooperative_index = SCHEDULER_NO_TASK;
    ++dispatch_count;
    scheduler_cursor = (selected_index + 1U) % SCHEDULER_MAX_TASKS;
    if (result == SCHEDULER_STEP_YIELD) {
        selected->state = SCHEDULER_TASK_READY;
    } else if (result == SCHEDULER_STEP_BLOCK) {
        selected->state = SCHEDULER_TASK_BLOCKED;
    } else if (result == SCHEDULER_STEP_COMPLETE) {
        selected->state = SCHEDULER_TASK_EXITED;
        ++completed_count;
    } else {
        selected->state = SCHEDULER_TASK_FAILED;
        ++failed_count;
    }
    scheduler_irq_restore(finish_flags);
    return true;
}

static bool wait_queue_contains_locked(const scheduler_wait_queue_t *queue,
                                         scheduler_task_id_t task_id) {
    if (queue == NULL || queue->count > SCHEDULER_MAX_TASKS) return false;
    for (uint32_t i = 0U; i < queue->count; ++i)
        if (queue->waiters[i] == task_id) return true;
    return false;
}
static bool wait_queue_enqueue_locked(scheduler_wait_queue_t *queue,
                                      scheduler_task_id_t task_id) {
    if (queue == NULL || task_id == 0U ||
        queue->count > SCHEDULER_MAX_TASKS) return false;
    if (wait_queue_contains_locked(queue, task_id)) return true;
    if (queue->count >= SCHEDULER_MAX_TASKS) return false;
    queue->waiters[queue->count++] = task_id;
    return true;
}
static scheduler_task_id_t wait_queue_pop_locked(scheduler_wait_queue_t *queue) {
    if (queue == NULL || queue->count == 0U) return 0U;
    if (queue->count > SCHEDULER_MAX_TASKS) {
        for (uint32_t i = 0U; i < SCHEDULER_MAX_TASKS; ++i) queue->waiters[i] = 0U;
        queue->count = 0U;
        return 0U;
    }
    const scheduler_task_id_t result = queue->waiters[0];
    for (uint32_t i = 1U; i < queue->count; ++i)
        queue->waiters[i - 1U] = queue->waiters[i];
    --queue->count;
    queue->waiters[queue->count] = 0U;
    return result;
}
static bool task_wake_locked(scheduler_task_id_t task_id) {
    const uint32_t index = find_task_index(task_id);
    if (index == SCHEDULER_NO_TASK ||
        task_table[index].state != SCHEDULER_TASK_BLOCKED) return false;
    task_table[index].state = SCHEDULER_TASK_READY;
    return true;
}
scheduler_task_state_t scheduler_task_state(scheduler_task_id_t task_id) {
    if (task_id == 0U) return SCHEDULER_TASK_UNUSED;
    const uint64_t flags = scheduler_irq_save_disable();
    const uint32_t index = find_task_index(task_id);
    const scheduler_task_state_t state = index == SCHEDULER_NO_TASK
        ? SCHEDULER_TASK_UNUSED : task_table[index].state;
    scheduler_irq_restore(flags);
    return state;
}
scheduler_task_id_t scheduler_current_task_id(void) {
    const uint64_t flags = scheduler_irq_save_disable();
    scheduler_task_id_t id = 0U;
    const uint32_t index = current_cooperative_index;
    if (dispatch_in_progress && index < SCHEDULER_MAX_TASKS &&
        task_table[index].state == SCHEDULER_TASK_RUNNING)
        id = task_table[index].id;
    scheduler_irq_restore(flags);
    return id;
}
bool scheduler_task_wake(scheduler_task_id_t task_id) {
    const uint64_t flags = scheduler_irq_save_disable();
    const bool woke = task_wake_locked(task_id);
    scheduler_irq_restore(flags);
    return woke;
}
uint32_t scheduler_blocked_count(void) {
    const uint64_t flags = scheduler_irq_save_disable();
    uint32_t blocked = 0U;
    for (uint32_t i = 0U; i < SCHEDULER_MAX_TASKS; ++i)
        if (task_table[i].state == SCHEDULER_TASK_BLOCKED) ++blocked;
    scheduler_irq_restore(flags);
    return blocked;
}
uint32_t scheduler_ready_count(void) {
    const uint64_t flags = scheduler_irq_save_disable();
    uint32_t ready = 0U;
    for (uint32_t i = 0U; i < SCHEDULER_MAX_TASKS; ++i)
        if (task_table[i].state == SCHEDULER_TASK_READY) ++ready;
    scheduler_irq_restore(flags);
    return ready;
}
void scheduler_wait_queue_init(scheduler_wait_queue_t *queue) {
    if (queue == NULL) return;
    const uint64_t flags = scheduler_irq_save_disable();
    for (uint32_t i = 0U; i < SCHEDULER_MAX_TASKS; ++i) queue->waiters[i] = 0U;
    queue->count = 0U;
    scheduler_irq_restore(flags);
}
bool scheduler_wait_queue_wait_current(scheduler_wait_queue_t *queue) {
    if (queue == NULL || preemptive_active) return false;
    const uint64_t flags = scheduler_irq_save_disable();
    const uint32_t index = current_cooperative_index;
    const bool valid = dispatch_in_progress && index < SCHEDULER_MAX_TASKS &&
        task_table[index].state == SCHEDULER_TASK_RUNNING &&
        wait_queue_enqueue_locked(queue, task_table[index].id);
    scheduler_irq_restore(flags);
    /* The caller must return SCHEDULER_STEP_BLOCK from this step callback. */
    return valid;
}
bool scheduler_wait_queue_wake_one(scheduler_wait_queue_t *queue) {
    if (queue == NULL) return false;
    const uint64_t flags = scheduler_irq_save_disable();
    bool woke = false;
    while (queue->count > 0U) {
        const scheduler_task_id_t task_id = wait_queue_pop_locked(queue);
        if (task_wake_locked(task_id)) { woke = true; break; }
        /* Drop stale/non-blocked waiters; IDs are never reused. */
    }
    scheduler_irq_restore(flags);
    return woke;
}
uint32_t scheduler_wait_queue_wake_all(scheduler_wait_queue_t *queue) {
    if (queue == NULL) return 0U;
    const uint64_t flags = scheduler_irq_save_disable();
    uint32_t woke = 0U;
    while (queue->count > 0U) {
        const scheduler_task_id_t task_id = wait_queue_pop_locked(queue);
        if (task_wake_locked(task_id)) ++woke;
    }
    scheduler_irq_restore(flags);
    return woke;
}
void scheduler_mutex_init(scheduler_mutex_t *mutex) {
    if (mutex == NULL) return;
    const uint64_t flags = scheduler_irq_save_disable();
    mutex->locked = false;
    mutex->handoff_pending = false;
    mutex->owner = 0U;
    for (uint32_t i = 0U; i < SCHEDULER_MAX_TASKS; ++i)
        mutex->waiters.waiters[i] = 0U;
    mutex->waiters.count = 0U;
    scheduler_irq_restore(flags);
}
scheduler_sync_result_t scheduler_mutex_lock(scheduler_mutex_t *mutex) {
    if (mutex == NULL || preemptive_active) return SCHEDULER_SYNC_ERROR;
    const uint64_t flags = scheduler_irq_save_disable();
    const uint32_t index = current_cooperative_index;
    if (!dispatch_in_progress || index >= SCHEDULER_MAX_TASKS ||
        task_table[index].state != SCHEDULER_TASK_RUNNING) {
        scheduler_irq_restore(flags);
        return SCHEDULER_SYNC_ERROR;
    }
    const scheduler_task_id_t current_id = task_table[index].id;
    if (mutex->locked && mutex->owner == current_id) {
        if (mutex->handoff_pending) {
            mutex->handoff_pending = false;
            scheduler_irq_restore(flags);
            return SCHEDULER_SYNC_ACQUIRED;
        }
        scheduler_irq_restore(flags);
        return SCHEDULER_SYNC_ERROR; /* non-recursive mutex */
    }
    if (!mutex->locked) {
        mutex->locked = true;
        mutex->owner = current_id;
        mutex->handoff_pending = false;
        scheduler_irq_restore(flags);
        return SCHEDULER_SYNC_ACQUIRED;
    }
    const bool queued = wait_queue_enqueue_locked(&mutex->waiters, current_id);
    scheduler_irq_restore(flags);
    return queued ? SCHEDULER_SYNC_WAIT : SCHEDULER_SYNC_ERROR;
}
bool scheduler_mutex_unlock(scheduler_mutex_t *mutex) {
    if (mutex == NULL || preemptive_active) return false;
    const uint64_t flags = scheduler_irq_save_disable();
    const uint32_t index = current_cooperative_index;
    if (!dispatch_in_progress || index >= SCHEDULER_MAX_TASKS ||
        task_table[index].state != SCHEDULER_TASK_RUNNING ||
        !mutex->locked || mutex->owner != task_table[index].id ||
        mutex->handoff_pending) {
        scheduler_irq_restore(flags);
        return false;
    }
    while (mutex->waiters.count > 0U) {
        const scheduler_task_id_t waiting_id = wait_queue_pop_locked(&mutex->waiters);
        if (task_wake_locked(waiting_id)) {
            mutex->owner = waiting_id;
            mutex->handoff_pending = true;
            scheduler_irq_restore(flags);
            return true;
        }
    }
    mutex->locked = false;
    mutex->owner = 0U;
    mutex->handoff_pending = false;
    scheduler_irq_restore(flags);
    return true;
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
        current_task_index == index ||
        (task_table[index].state != SCHEDULER_TASK_EXITED &&
         task_table[index].state != SCHEDULER_TASK_FAILED)) {
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
    scheduler_irq_restore(flags);
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
    if (preemptive_ticks >= SCHEDULER_PREEMPTIVE_TEST_TICKS) {
        for (uint32_t i = 0U; i < SCHEDULER_MAX_TASKS; ++i) {
            if (task_table[i].entry != NULL && task_table[i].state == SCHEDULER_TASK_READY) {
                task_table[i].state = SCHEDULER_TASK_FAILED;
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

typedef struct {
    scheduler_wait_queue_t *queue;
    volatile uint32_t *completed;
    uint32_t stage;
} scheduler_wait_probe_t;
static scheduler_step_result_t scheduler_wait_probe_step(void *context) {
    scheduler_wait_probe_t *probe = (scheduler_wait_probe_t *)context;
    if (probe == NULL || probe->queue == NULL || probe->completed == NULL)
        return SCHEDULER_STEP_FAIL;
    if (probe->stage == 0U) {
        probe->stage = 1U;
        return scheduler_wait_queue_wait_current(probe->queue)
            ? SCHEDULER_STEP_BLOCK : SCHEDULER_STEP_FAIL;
    }
    if (probe->stage == 1U) {
        ++(*probe->completed);
        probe->stage = 2U;
        return SCHEDULER_STEP_COMPLETE;
    }
    return SCHEDULER_STEP_FAIL;
}
typedef struct {
    scheduler_wait_queue_t *queue;
    volatile uint32_t *woken;
    uint32_t stage;
} scheduler_waker_probe_t;
static scheduler_step_result_t scheduler_waker_probe_step(void *context) {
    scheduler_waker_probe_t *probe = (scheduler_waker_probe_t *)context;
    if (probe == NULL || probe->queue == NULL || probe->woken == NULL)
        return SCHEDULER_STEP_FAIL;
    if (probe->stage == 0U) {
        if (!scheduler_wait_queue_wake_one(probe->queue)) return SCHEDULER_STEP_FAIL;
        ++(*probe->woken);
        probe->stage = 1U;
        return SCHEDULER_STEP_YIELD;
    }
    if (probe->stage == 1U) {
        *probe->woken += scheduler_wait_queue_wake_all(probe->queue);
        probe->stage = 2U;
        return SCHEDULER_STEP_COMPLETE;
    }
    return SCHEDULER_STEP_FAIL;
}
typedef struct {
    scheduler_mutex_t *mutex;
    volatile uint32_t *shared;
    uint32_t stage;
} scheduler_mutex_probe_t;
static scheduler_step_result_t scheduler_mutex_owner_step(void *context) {
    scheduler_mutex_probe_t *probe = (scheduler_mutex_probe_t *)context;
    if (probe == NULL || probe->mutex == NULL || probe->shared == NULL)
        return SCHEDULER_STEP_FAIL;
    if (probe->stage == 0U) {
        if (scheduler_mutex_lock(probe->mutex) != SCHEDULER_SYNC_ACQUIRED)
            return SCHEDULER_STEP_FAIL;
        probe->stage = 1U;
        return SCHEDULER_STEP_YIELD;
    }
    if (probe->stage == 1U) {
        ++(*probe->shared);
        if (!scheduler_mutex_unlock(probe->mutex)) return SCHEDULER_STEP_FAIL;
        probe->stage = 2U;
        return SCHEDULER_STEP_COMPLETE;
    }
    return SCHEDULER_STEP_FAIL;
}
static scheduler_step_result_t scheduler_mutex_waiter_step(void *context) {
    scheduler_mutex_probe_t *probe = (scheduler_mutex_probe_t *)context;
    if (probe == NULL || probe->mutex == NULL || probe->shared == NULL)
        return SCHEDULER_STEP_FAIL;
    const scheduler_sync_result_t result = scheduler_mutex_lock(probe->mutex);
    if (result == SCHEDULER_SYNC_WAIT) return SCHEDULER_STEP_BLOCK;
    if (result != SCHEDULER_SYNC_ACQUIRED || probe->stage != 0U)
        return SCHEDULER_STEP_FAIL;
    probe->stage = 1U;
    ++(*probe->shared);
    if (!scheduler_mutex_unlock(probe->mutex)) return SCHEDULER_STEP_FAIL;
    return SCHEDULER_STEP_COMPLETE;
}
typedef struct { uint32_t stage; } scheduler_direct_wake_probe_t;
static scheduler_step_result_t scheduler_direct_wake_probe_step(void *context) {
    scheduler_direct_wake_probe_t *probe = (scheduler_direct_wake_probe_t *)context;
    if (probe == NULL) return SCHEDULER_STEP_FAIL;
    if (probe->stage == 0U) {
        probe->stage = 1U;
        return SCHEDULER_STEP_BLOCK;
    }
    if (probe->stage == 1U) {
        probe->stage = 2U;
        return SCHEDULER_STEP_COMPLETE;
    }
    return SCHEDULER_STEP_FAIL;
}
bool scheduler_sync_self_test(void) {
    scheduler_init();
    const uint64_t pages_before = memory_allocated_pages();
    scheduler_wait_queue_t queue;
    scheduler_wait_queue_init(&queue);
    volatile uint32_t completed = 0U;
    volatile uint32_t woken = 0U;
    scheduler_wait_probe_t waiter_a = {&queue, &completed, 0U};
    scheduler_wait_probe_t waiter_b = {&queue, &completed, 0U};
    scheduler_waker_probe_t waker = {&queue, &woken, 0U};
    scheduler_task_id_t a = 0U, b = 0U, c = 0U;
    if (!scheduler_create_task(scheduler_wait_probe_step, &waiter_a, &a) ||
        !scheduler_create_task(scheduler_wait_probe_step, &waiter_b, &b) ||
        !scheduler_create_task(scheduler_waker_probe_step, &waker, &c)) return false;
    if (!scheduler_run_once() || !scheduler_run_once() ||
        scheduler_task_state(a) != SCHEDULER_TASK_BLOCKED ||
        scheduler_task_state(b) != SCHEDULER_TASK_BLOCKED ||
        scheduler_blocked_count() != 2U) return false;
    if (!scheduler_run_once() || woken != 1U ||
        scheduler_task_state(a) != SCHEDULER_TASK_READY ||
        scheduler_task_state(b) != SCHEDULER_TASK_BLOCKED ||
        scheduler_blocked_count() != 1U) return false;
    if (!scheduler_run_once() || scheduler_task_state(a) != SCHEDULER_TASK_EXITED)
        return false;
    if (!scheduler_run_once() || woken != 2U ||
        scheduler_task_state(b) != SCHEDULER_TASK_READY ||
        scheduler_wait_queue_wake_one(&queue)) return false;
    for (uint32_t guard = 0U; guard < 4U && scheduler_ready_count() > 0U; ++guard)
        (void)scheduler_run_once();
    if (completed != 2U ||
        scheduler_task_state(a) != SCHEDULER_TASK_EXITED ||
        scheduler_task_state(b) != SCHEDULER_TASK_EXITED ||
        scheduler_task_state(c) != SCHEDULER_TASK_EXITED ||
        !scheduler_task_reap(a) || !scheduler_task_reap(b) ||
        !scheduler_task_reap(c)) return false;

    scheduler_direct_wake_probe_t direct_probe = {0U};
    scheduler_task_id_t direct_id = 0U;
    if (!scheduler_create_task(scheduler_direct_wake_probe_step, &direct_probe, &direct_id) ||
        !scheduler_run_once() ||
        scheduler_task_state(direct_id) != SCHEDULER_TASK_BLOCKED ||
        !scheduler_task_wake(direct_id) ||
        scheduler_task_state(direct_id) != SCHEDULER_TASK_READY ||
        !scheduler_run_once() || direct_probe.stage != 2U ||
        scheduler_task_state(direct_id) != SCHEDULER_TASK_EXITED ||
        !scheduler_task_reap(direct_id)) return false;

    scheduler_init();
    scheduler_mutex_t mutex;
    scheduler_mutex_init(&mutex);
    volatile uint32_t shared = 0U;
    scheduler_mutex_probe_t owner = {&mutex, &shared, 0U};
    scheduler_mutex_probe_t waiter = {&mutex, &shared, 0U};
    scheduler_task_id_t owner_id = 0U, waiter_id = 0U;
    if (!scheduler_create_task(scheduler_mutex_owner_step, &owner, &owner_id) ||
        !scheduler_create_task(scheduler_mutex_waiter_step, &waiter, &waiter_id)) return false;
    if (!scheduler_run_once() || !scheduler_run_once() ||
        scheduler_task_state(waiter_id) != SCHEDULER_TASK_BLOCKED ||
        scheduler_blocked_count() != 1U) return false;
    if (!scheduler_run_once() ||
        scheduler_task_state(waiter_id) != SCHEDULER_TASK_READY ||
        mutex.owner != waiter_id || !mutex.locked || !mutex.handoff_pending) return false;
    if (!scheduler_run_once() || shared != 2U ||
        scheduler_task_state(owner_id) != SCHEDULER_TASK_EXITED ||
        scheduler_task_state(waiter_id) != SCHEDULER_TASK_EXITED ||
        mutex.locked || mutex.owner != 0U ||
        !scheduler_task_reap(owner_id) || !scheduler_task_reap(waiter_id)) return false;
    return memory_allocated_pages() == pages_before;
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

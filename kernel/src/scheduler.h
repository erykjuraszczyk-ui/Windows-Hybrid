#ifndef HYBRID_SCHEDULER_H
#define HYBRID_SCHEDULER_H
#include <stdbool.h>
#include <stdint.h>
#define SCHEDULER_MAX_TASKS 8U
typedef uint32_t scheduler_task_id_t;
typedef enum {
    SCHEDULER_TASK_UNUSED = 0, SCHEDULER_TASK_READY, SCHEDULER_TASK_RUNNING,
    SCHEDULER_TASK_BLOCKED, SCHEDULER_TASK_EXITED, SCHEDULER_TASK_FAILED, SCHEDULER_TASK_CANCELLED
} scheduler_task_state_t;
typedef enum { SCHEDULER_STEP_YIELD = 0, SCHEDULER_STEP_COMPLETE = 1 } scheduler_step_result_t;
typedef scheduler_step_result_t (*scheduler_step_fn)(void *context);
typedef void (*scheduler_task_entry_fn)(void);
typedef struct { volatile uint32_t waiters; } scheduler_wait_queue_t;
typedef enum {
    SCHEDULER_WAIT_NONE = 0, SCHEDULER_WAITING, SCHEDULER_WAIT_SIGNALED,
    SCHEDULER_WAIT_TIMED_OUT, SCHEDULER_WAIT_SLEEPING, SCHEDULER_WAIT_SLEEP_COMPLETE,
    SCHEDULER_WAIT_CANCELLED
} scheduler_wait_result_t;
typedef struct {
    volatile uint32_t locked;
    scheduler_task_id_t owner;
    scheduler_wait_queue_t waiters;
} scheduler_mutex_t;
void scheduler_init(void);
bool scheduler_create_task(scheduler_step_fn step, void *context, scheduler_task_id_t *task_id);
bool scheduler_run_once(void);
scheduler_task_state_t scheduler_task_state(scheduler_task_id_t task_id);
uint32_t scheduler_ready_count(void);
uint64_t scheduler_dispatch_count(void);
uint64_t scheduler_completed_count(void);
uint64_t scheduler_failed_count(void);
bool scheduler_self_test(void);
bool scheduler_task_lifecycle_self_test(void);
bool scheduler_task_create(scheduler_task_entry_fn entry, scheduler_task_id_t *task_id);
bool scheduler_create_preemptive_task(scheduler_task_entry_fn entry, scheduler_task_id_t *task_id);
bool scheduler_task_reap(scheduler_task_id_t task_id);
bool scheduler_task_block(scheduler_task_id_t task_id);
bool scheduler_task_wake(scheduler_task_id_t task_id);
/* Cancels READY/BLOCKED tasks only. A running task must cooperate and exit itself. */
bool scheduler_task_cancel(scheduler_task_id_t task_id);
bool scheduler_sleep_ticks(uint64_t ticks);
bool scheduler_wait_queue_wait_timeout(scheduler_wait_queue_t *queue, uint64_t timeout_ticks);
uint64_t scheduler_timer_expiration_count(void);
bool scheduler_timer_timeout_cancel_self_test(void);
void scheduler_wait_queue_init(scheduler_wait_queue_t *queue);
bool scheduler_wait_queue_wait(scheduler_wait_queue_t *queue);
bool scheduler_wait_queue_wake_one(scheduler_wait_queue_t *queue);
uint32_t scheduler_wait_queue_wake_all(scheduler_wait_queue_t *queue);
void scheduler_mutex_init(scheduler_mutex_t *mutex);
bool scheduler_mutex_lock(scheduler_mutex_t *mutex);
bool scheduler_mutex_unlock(scheduler_mutex_t *mutex);
bool scheduler_blocking_sync_self_test(void);
void *scheduler_on_timer_interrupt(void *interrupt_frame);
bool scheduler_preemptive_self_test(void);
uint64_t scheduler_preemptive_switch_count(void);
uint64_t scheduler_preemptive_counter_a(void);
uint64_t scheduler_preemptive_counter_b(void);
#endif

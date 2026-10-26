#ifndef HYBRID_SCHEDULER_H
#define HYBRID_SCHEDULER_H
#include <stdbool.h>
#include <stdint.h>
#define SCHEDULER_MAX_TASKS 8U
typedef uint32_t scheduler_task_id_t;
/* Cooperative step/callback scheduler; no CPU stack switch or preemption yet. */
typedef enum {
    SCHEDULER_TASK_UNUSED = 0,
    SCHEDULER_TASK_READY,
    SCHEDULER_TASK_RUNNING,
    SCHEDULER_TASK_EXITED,
    SCHEDULER_TASK_FAILED
} scheduler_task_state_t;
typedef enum {
    SCHEDULER_STEP_YIELD = 0,
    SCHEDULER_STEP_COMPLETE = 1
} scheduler_step_result_t;
typedef scheduler_step_result_t (*scheduler_step_fn)(void *context);
void scheduler_init(void);
bool scheduler_create_task(scheduler_step_fn step, void *context, scheduler_task_id_t *task_id);
bool scheduler_run_once(void);
scheduler_task_state_t scheduler_task_state(scheduler_task_id_t task_id);
uint32_t scheduler_ready_count(void);
uint64_t scheduler_dispatch_count(void);
uint64_t scheduler_completed_count(void);
uint64_t scheduler_failed_count(void);
bool scheduler_self_test(void);
#endif

#include "scheduler.h"
#include <stddef.h>
#include <stdint.h>

typedef struct {
    scheduler_task_id_t id;
    scheduler_task_state_t state;
    scheduler_step_fn step;
    void *context;
} scheduler_task_t;

static scheduler_task_t task_table[SCHEDULER_MAX_TASKS];
static uint32_t scheduler_cursor;
static uint32_t next_task_id;
static bool dispatch_in_progress;
static uint64_t dispatch_count;
static uint64_t completed_count;
static uint64_t failed_count;

void scheduler_init(void) {
    for (uint32_t i = 0U; i < SCHEDULER_MAX_TASKS; ++i) {
        task_table[i].id = 0U;
        task_table[i].state = SCHEDULER_TASK_UNUSED;
        task_table[i].step = NULL;
        task_table[i].context = NULL;
    }
    scheduler_cursor = 0U;
    next_task_id = 1U;
    dispatch_in_progress = false;
    dispatch_count = 0U;
    completed_count = 0U;
    failed_count = 0U;
}

bool scheduler_create_task(scheduler_step_fn step, void *context, scheduler_task_id_t *task_id) {
    if (step == NULL || task_id == NULL || dispatch_in_progress || next_task_id == 0U) {
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
        *task_id = task->id;
        return true;
    }
    return false;
}

bool scheduler_run_once(void) {
    if (dispatch_in_progress) return false;
    scheduler_task_t *selected = NULL;
    uint32_t selected_index = 0U;
    for (uint32_t offset = 0U; offset < SCHEDULER_MAX_TASKS; ++offset) {
        const uint32_t index = (scheduler_cursor + offset) % SCHEDULER_MAX_TASKS;
        if (task_table[index].state == SCHEDULER_TASK_READY) {
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

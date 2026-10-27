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
#define SCHEDULER_IPC_CAPACITY 4U
#define SCHEDULER_IPC_PAYLOAD_SIZE 24U
#define SCHEDULER_IPC_MAX_SERVICES 8U
#define SCHEDULER_IPC_MAX_PENDING_CALLS 8U
#define SCHEDULER_IPC_SERVICE_NAME_SIZE 16U
#define SCHEDULER_IPC_PROTOCOL_VERSION 1U
#define SCHEDULER_IPC_SERVICE_CAP_ONE_WAY (UINT32_C(1) << 0U)
#define SCHEDULER_IPC_SERVICE_CAP_REQUEST_RESPONSE (UINT32_C(1) << 1U)
#define SCHEDULER_IPC_SERVICE_CAP_EVENTS (UINT32_C(1) << 2U)
#define SCHEDULER_IPC_SERVICE_CAP_ALL (SCHEDULER_IPC_SERVICE_CAP_ONE_WAY | SCHEDULER_IPC_SERVICE_CAP_REQUEST_RESPONSE | SCHEDULER_IPC_SERVICE_CAP_EVENTS)
typedef uint32_t scheduler_ipc_service_id_t;
typedef enum {
    SCHEDULER_IPC_KIND_DATA = 0U,
    SCHEDULER_IPC_KIND_REQUEST = 1U,
    SCHEDULER_IPC_KIND_RESPONSE = 2U,
    SCHEDULER_IPC_KIND_ERROR = 3U,
    SCHEDULER_IPC_KIND_EVENT = 4U
} scheduler_ipc_message_kind_t;
typedef enum {
    SCHEDULER_IPC_STATUS_OK = 0U,
    SCHEDULER_IPC_STATUS_INVALID_ARGUMENT = 1U,
    SCHEDULER_IPC_STATUS_NOT_FOUND = 2U,
    SCHEDULER_IPC_STATUS_UNSUPPORTED = 3U,
    SCHEDULER_IPC_STATUS_BUSY = 4U,
    SCHEDULER_IPC_STATUS_INTERNAL = 5U
} scheduler_ipc_status_t;
typedef struct {
    scheduler_ipc_service_id_t id;
    uint32_t capabilities;
    uint32_t queued_messages;
    uint32_t queue_capacity;
    char name[SCHEDULER_IPC_SERVICE_NAME_SIZE];
} scheduler_ipc_service_info_t;
typedef struct {
    uint32_t type;
    uint32_t length;
    uint32_t protocol_version;
    scheduler_ipc_message_kind_t kind;
    scheduler_ipc_status_t status;
    scheduler_ipc_service_id_t source_service_id;
    uint32_t request_id;
    uint8_t payload[SCHEDULER_IPC_PAYLOAD_SIZE];
} scheduler_ipc_message_t;
typedef struct {
    scheduler_ipc_message_t messages[SCHEDULER_IPC_CAPACITY];
    uint32_t head;
    uint32_t tail;
    uint32_t count;
    scheduler_wait_queue_t readers;
    scheduler_wait_queue_t writers;
} scheduler_ipc_channel_t;
typedef struct {
    volatile uint32_t count;
    uint32_t max_count;
    scheduler_wait_queue_t waiters;
} scheduler_semaphore_t;
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
bool scheduler_semaphore_init(scheduler_semaphore_t *semaphore, uint32_t initial_count, uint32_t max_count);
bool scheduler_semaphore_wait(scheduler_semaphore_t *semaphore);
bool scheduler_semaphore_post(scheduler_semaphore_t *semaphore);
void scheduler_ipc_channel_init(scheduler_ipc_channel_t *channel);
bool scheduler_ipc_send(scheduler_ipc_channel_t *channel, const scheduler_ipc_message_t *message);
bool scheduler_ipc_receive(scheduler_ipc_channel_t *channel, scheduler_ipc_message_t *message);
bool scheduler_semaphore_ipc_self_test(void);
bool scheduler_ipc_service_register(const char *name, scheduler_ipc_service_id_t *service_id);
bool scheduler_ipc_service_register_ex(const char *name, uint32_t capabilities,
                                       scheduler_ipc_service_id_t *service_id);
bool scheduler_ipc_service_lookup(const char *name, scheduler_ipc_service_id_t *service_id);
bool scheduler_ipc_service_enumerate(uint32_t active_index, scheduler_ipc_service_info_t *info);
bool scheduler_ipc_service_get_info(scheduler_ipc_service_id_t service_id,
                                    scheduler_ipc_service_info_t *info);
bool scheduler_ipc_service_unregister(scheduler_ipc_service_id_t service_id);
bool scheduler_ipc_service_send(scheduler_ipc_service_id_t source_service_id,
                                scheduler_ipc_service_id_t destination_service_id,
                                uint32_t request_id,
                                const scheduler_ipc_message_t *message);
bool scheduler_ipc_service_receive(scheduler_ipc_service_id_t service_id,
                                   scheduler_ipc_message_t *message);
bool scheduler_ipc_service_send_request(scheduler_ipc_service_id_t source_service_id,
                                        scheduler_ipc_service_id_t destination_service_id,
                                        uint32_t request_id,
                                        const scheduler_ipc_message_t *request);
bool scheduler_ipc_service_reply(scheduler_ipc_service_id_t responder_service_id,
                                 const scheduler_ipc_message_t *request,
                                 const scheduler_ipc_message_t *response,
                                 scheduler_ipc_status_t status);
bool scheduler_ipc_message_is_response_for(const scheduler_ipc_message_t *message,
                                           uint32_t request_id);
uint32_t scheduler_ipc_service_count(void);
bool scheduler_ipc_service_call(scheduler_ipc_service_id_t source_service_id,
                                scheduler_ipc_service_id_t destination_service_id,
                                const scheduler_ipc_message_t *request,
                                uint64_t timeout_ticks,
                                scheduler_ipc_message_t *response,
                                volatile uint32_t *request_id_out);
bool scheduler_ipc_call_cancel(scheduler_ipc_service_id_t source_service_id,
                               uint32_t request_id);
uint32_t scheduler_ipc_pending_call_count(void);
bool scheduler_ipc_runtime_self_test(void);

bool scheduler_ipc_service_self_test(void);
bool scheduler_ipc_request_response_discovery_self_test(void);
bool scheduler_blocking_sync_self_test(void);
void *scheduler_on_timer_interrupt(void *interrupt_frame);
bool scheduler_preemptive_self_test(void);
uint64_t scheduler_preemptive_switch_count(void);
uint64_t scheduler_preemptive_counter_a(void);
uint64_t scheduler_preemptive_counter_b(void);
#endif

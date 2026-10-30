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
#define SCHEDULER_IPC_PRESSURE_SAMPLE_WINDOW 4U
#define SCHEDULER_IPC_PAYLOAD_SIZE 24U
#define SCHEDULER_IPC_MAX_SERVICES 8U
#define SCHEDULER_IPC_MAX_PENDING_CALLS 8U
#define SCHEDULER_IPC_MAX_TRACKED_DELIVERIES 8U
#define SCHEDULER_IPC_DELIVERY_HISTORY 16U
#define SCHEDULER_IPC_DEAD_LETTER_CAPACITY 8U
#define SCHEDULER_IPC_TRACE_CAPACITY 32U
#define SCHEDULER_IPC_DEFAULT_CIRCUIT_FAILURE_THRESHOLD 3U
#define SCHEDULER_IPC_PRIORITY_MIN 1U
#define SCHEDULER_IPC_PRIORITY_MAX 8U
#define SCHEDULER_IPC_RATE_LIMIT_MAX_BURST 64U
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
    SCHEDULER_IPC_KIND_EVENT = 4U,
    SCHEDULER_IPC_KIND_ACK = 5U
} scheduler_ipc_message_kind_t;
typedef enum {
    SCHEDULER_IPC_STATUS_OK = 0U,
    SCHEDULER_IPC_STATUS_INVALID_ARGUMENT = 1U,
    SCHEDULER_IPC_STATUS_NOT_FOUND = 2U,
    SCHEDULER_IPC_STATUS_UNSUPPORTED = 3U,
    SCHEDULER_IPC_STATUS_BUSY = 4U,
    SCHEDULER_IPC_STATUS_INTERNAL = 5U
} scheduler_ipc_status_t;
typedef enum {
    SCHEDULER_IPC_QOS_BEST_EFFORT = 1U,
    SCHEDULER_IPC_QOS_NORMAL = 2U,
    SCHEDULER_IPC_QOS_LATENCY_SENSITIVE = 3U,
    SCHEDULER_IPC_QOS_DEADLINE_CRITICAL = 4U
} scheduler_ipc_qos_profile_t;
typedef enum {
    SCHEDULER_IPC_QUEUE_BLOCK = 0U,
    SCHEDULER_IPC_QUEUE_REJECT = 1U,
    SCHEDULER_IPC_QUEUE_DROP_NEWEST = 2U,
    SCHEDULER_IPC_QUEUE_DROP_OLDEST = 3U
} scheduler_ipc_queue_policy_t;
typedef enum {
    SCHEDULER_IPC_DELIVERY_DELIVERED = 0U,
    SCHEDULER_IPC_DELIVERY_DELIVERED_DROPPED_OLDEST = 1U,
    SCHEDULER_IPC_DELIVERY_BACKPRESSURE = 2U,
    SCHEDULER_IPC_DELIVERY_DROPPED_NEWEST = 3U,
    SCHEDULER_IPC_DELIVERY_SERVICE_NOT_FOUND = 4U,
    SCHEDULER_IPC_DELIVERY_SERVICE_CLOSED = 5U,
    SCHEDULER_IPC_DELIVERY_INVALID_ARGUMENT = 6U,
    SCHEDULER_IPC_DELIVERY_INTERNAL_ERROR = 7U,
    SCHEDULER_IPC_DELIVERY_DUPLICATE_SUPPRESSED = 8U,
    SCHEDULER_IPC_DELIVERY_RETRY_EXHAUSTED = 9U,
    SCHEDULER_IPC_DELIVERY_ALREADY_ACKNOWLEDGED = 10U,
    SCHEDULER_IPC_DELIVERY_TRACKING_CAPACITY = 11U,
    SCHEDULER_IPC_DELIVERY_ACKNOWLEDGED = 12U,
    SCHEDULER_IPC_DELIVERY_DEADLINE_EXPIRED = 13U,
    SCHEDULER_IPC_DELIVERY_CIRCUIT_OPEN = 14U,
    SCHEDULER_IPC_DELIVERY_RATE_LIMITED = 15U,
    SCHEDULER_IPC_DELIVERY_SOURCE_QUOTA = 16U,
    SCHEDULER_IPC_DELIVERY_PRIORITY_PREEMPTED = 17U
} scheduler_ipc_delivery_result_t;
typedef enum {
    SCHEDULER_IPC_DELIVERY_STATE_FREE = 0U,
    SCHEDULER_IPC_DELIVERY_STATE_PENDING = 1U,
    SCHEDULER_IPC_DELIVERY_STATE_ACKNOWLEDGED = 2U,
    SCHEDULER_IPC_DELIVERY_STATE_EXHAUSTED = 3U,
    SCHEDULER_IPC_DELIVERY_STATE_FAILED = 4U,
    SCHEDULER_IPC_DELIVERY_STATE_EXPIRED = 5U
} scheduler_ipc_delivery_state_t;
typedef struct {
    uint32_t delivery_id;
    scheduler_ipc_delivery_state_t state;
    uint32_t attempts;
    uint32_t max_attempts;
    scheduler_ipc_service_id_t source_service_id;
    scheduler_ipc_service_id_t destination_service_id;
    uint32_t request_id;
    scheduler_ipc_delivery_result_t last_result;
    uint64_t deadline_tick;
    uint64_t retry_interval_ticks;
    uint64_t max_retry_interval_ticks;
    uint64_t next_retry_tick;
} scheduler_ipc_delivery_receipt_t;
typedef enum {
    SCHEDULER_IPC_CIRCUIT_CLOSED = 0U,
    SCHEDULER_IPC_CIRCUIT_OPEN = 1U,
    SCHEDULER_IPC_CIRCUIT_HALF_OPEN = 2U
} scheduler_ipc_circuit_state_t;
typedef enum {
    SCHEDULER_IPC_HEALTHY = 0U,
    SCHEDULER_IPC_HEALTH_DEGRADED = 1U,
    SCHEDULER_IPC_HEALTH_UNAVAILABLE = 2U
} scheduler_ipc_health_state_t;
typedef struct {
    scheduler_ipc_service_id_t service_id;
    scheduler_ipc_circuit_state_t circuit_state;
    scheduler_ipc_health_state_t health_state;
    uint32_t failure_threshold;
    uint32_t consecutive_failures;
    uint64_t reset_timeout_ticks;
    uint64_t open_until_tick;
    uint64_t total_successes;
    uint64_t total_failures;
    uint64_t last_success_tick;
    uint64_t last_failure_tick;
} scheduler_ipc_service_health_t;
typedef enum {
    SCHEDULER_IPC_PRESSURE_STABLE = 0U,
    SCHEDULER_IPC_PRESSURE_RISING = 1U,
    SCHEDULER_IPC_PRESSURE_FALLING = 2U
} scheduler_ipc_pressure_trend_t;
typedef struct {
    scheduler_ipc_service_id_t id;
    uint32_t capabilities;
    uint32_t queued_messages;
    uint32_t queue_capacity;
    uint32_t queue_free_slots;
    uint32_t queue_utilization_percent;
    uint64_t queue_compactions;
    uint64_t queue_compaction_moves;
    uint64_t backpressure_recoveries;
    bool adaptive_admission_enabled;
    uint32_t adaptive_min_high_watermark;
    uint32_t adaptive_max_high_watermark;
    uint32_t congestion_high_watermark;
    uint32_t congestion_low_watermark;
    uint64_t adaptive_admission_adjustments;
    uint64_t adaptive_pressure_events;
    bool predictive_admission_enabled;
    uint32_t predictive_min_budget;
    uint32_t predictive_max_budget;
    uint32_t predictive_current_budget;
    uint32_t predictive_sample_count;
    scheduler_ipc_pressure_trend_t predictive_trend;
    uint64_t predictive_samples;
    uint64_t predictive_rising_predictions;
    uint64_t predictive_falling_predictions;
    uint64_t predictive_budget_adjustments;
    uint64_t predictive_budget_rejections;
    scheduler_ipc_queue_policy_t queue_policy;
    uint32_t queue_high_watermark;
    uint32_t priority;
    bool congested;
    uint64_t messages_enqueued;
    uint64_t messages_received;
    uint64_t backpressure_events;
    uint64_t messages_dropped_newest;
    uint64_t messages_dropped_oldest;
    uint64_t messages_rejected_full;
    bool rate_limit_enabled;
    uint32_t max_messages_per_source;
    bool priority_admission_enabled;
    uint64_t source_quota_rejections;
    uint64_t priority_preemptions;
    uint32_t rate_limit_tokens;
    uint32_t rate_limit_capacity;
    uint64_t rate_limited_messages;
    scheduler_ipc_qos_profile_t qos_profile;
    uint64_t default_deadline_budget_ticks;
    uint64_t starvation_threshold_ticks;
    uint64_t deadline_aware_selections;
    uint64_t starvation_preventions;
    uint64_t qos_profile_selections;
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
    uint32_t delivery_id;
    uint64_t enqueue_tick;
    uint64_t deadline_tick;
    uint64_t expiry_tick;
    scheduler_ipc_qos_profile_t qos_profile;
    uint8_t payload[SCHEDULER_IPC_PAYLOAD_SIZE];
} scheduler_ipc_message_t;
typedef struct {
    scheduler_ipc_delivery_receipt_t receipt;
    scheduler_ipc_message_t message;
    scheduler_ipc_delivery_result_t reason;
    uint64_t recorded_tick;
} scheduler_ipc_dead_letter_t;
typedef enum { SCHEDULER_IPC_TRACE_SEND=1U, SCHEDULER_IPC_TRACE_RECEIVE=2U, SCHEDULER_IPC_TRACE_ACK=3U, SCHEDULER_IPC_TRACE_RETRY=4U, SCHEDULER_IPC_TRACE_DEADLINE=5U, SCHEDULER_IPC_TRACE_CIRCUIT=6U, SCHEDULER_IPC_TRACE_BACKPRESSURE=7U, SCHEDULER_IPC_TRACE_DEAD_LETTER=8U } scheduler_ipc_trace_kind_t;
typedef struct { uint64_t sequence, tick; scheduler_ipc_trace_kind_t kind; scheduler_ipc_service_id_t source_service_id, destination_service_id; uint32_t request_id, delivery_id; scheduler_ipc_delivery_result_t result; uint32_t queue_depth; } scheduler_ipc_trace_event_t;
typedef struct {
    uint64_t calls_started;
    uint64_t calls_completed;
    uint64_t calls_timed_out;
    uint64_t calls_cancelled;
    uint64_t calls_service_unavailable;
    uint64_t calls_send_failed;
    uint64_t replies_dropped;
    uint64_t services_closed;
    uint64_t messages_enqueued;
    uint64_t messages_received;
    uint64_t backpressure_events;
    uint64_t messages_dropped_newest;
    uint64_t messages_dropped_oldest;
    uint64_t messages_rejected_full;
    uint64_t delivery_failures;
    uint64_t deliveries_started;
    uint64_t deliveries_acknowledged;
    uint64_t delivery_retry_attempts;
    uint64_t delivery_retries_exhausted;
    uint64_t duplicate_deliveries_suppressed;
    uint64_t automatic_retry_attempts;
    uint64_t delivery_deadlines_expired;
    uint64_t messages_expired;
    uint64_t queue_compactions;
    uint64_t queue_compaction_moves;
    uint64_t backpressure_recoveries;
    uint64_t adaptive_admission_adjustments;
    uint64_t adaptive_pressure_events;
    uint64_t predictive_samples;
    uint64_t predictive_rising_predictions;
    uint64_t predictive_falling_predictions;
    uint64_t predictive_budget_adjustments;
    uint64_t predictive_budget_rejections;
    uint64_t dead_letters_enqueued;
    uint64_t dead_letters_dropped;
    uint64_t service_health_successes;
    uint64_t service_health_failures;
    uint64_t circuit_breaker_trips;
    uint64_t circuit_breaker_recoveries;
    uint64_t circuit_open_rejections;
    uint64_t trace_events_recorded, trace_events_overwritten;
    uint64_t rate_limited_messages;
    uint64_t source_quota_rejections;
    uint64_t priority_preemptions;
    uint64_t fair_receive_selections;
    uint64_t deadline_aware_selections;
    uint64_t starvation_preventions;
    uint64_t qos_profile_selections;
    uint32_t trace_events_pending;
    uint32_t dead_letters_pending;
    uint32_t pending_deliveries;
    uint32_t max_queue_depth;
    uint32_t pending_calls;
    uint32_t active_services;
} scheduler_ipc_diagnostics_t;
typedef struct {
    scheduler_ipc_message_t messages[SCHEDULER_IPC_CAPACITY];
    uint32_t head;
    uint32_t tail;
    uint32_t count;
    scheduler_wait_queue_t readers;
    scheduler_wait_queue_t writers;
    scheduler_ipc_service_id_t last_served_source_service_id;
    uint64_t starvation_threshold_ticks;
    uint64_t deadline_aware_selections;
    uint64_t starvation_preventions;
    uint64_t qos_profile_selections;
    uint64_t queue_compactions;
    uint64_t queue_compaction_moves;
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
bool scheduler_ipc_service_set_queue_policy(scheduler_ipc_service_id_t service_id,
                                             scheduler_ipc_queue_policy_t policy);
bool scheduler_ipc_service_set_priority(scheduler_ipc_service_id_t service_id, uint32_t priority);
bool scheduler_ipc_service_set_qos_profile(scheduler_ipc_service_id_t service_id,
                                           scheduler_ipc_qos_profile_t profile,
                                           uint64_t default_deadline_budget_ticks,
                                           uint64_t starvation_threshold_ticks);
bool scheduler_ipc_deadline_qos_self_test(void);
bool scheduler_ipc_service_set_default_ttl(scheduler_ipc_service_id_t service_id, uint64_t ttl_ticks);
bool scheduler_ipc_expiration_self_test(void);
bool scheduler_ipc_queue_telemetry_self_test(void);
bool scheduler_ipc_service_set_rate_limit(scheduler_ipc_service_id_t service_id,
                                          uint32_t burst_capacity,
                                          uint32_t refill_tokens,
                                          uint64_t refill_interval_ticks);
bool scheduler_ipc_service_set_source_quota(scheduler_ipc_service_id_t service_id,
                                            uint32_t max_messages_per_source);
bool scheduler_ipc_service_set_priority_admission(scheduler_ipc_service_id_t service_id,
                                                   bool enabled);
bool scheduler_ipc_fair_admission_self_test(void);
bool scheduler_ipc_rate_limit_self_test(void);
bool scheduler_ipc_service_set_congestion_control(scheduler_ipc_service_id_t service_id,
                                                    uint32_t high_watermark,
                                                    uint32_t low_watermark);
bool scheduler_ipc_service_set_adaptive_admission(scheduler_ipc_service_id_t service_id,
                                                   uint32_t min_high_watermark,
                                                   uint32_t max_high_watermark);
bool scheduler_ipc_adaptive_admission_self_test(void);
bool scheduler_ipc_service_set_predictive_admission(scheduler_ipc_service_id_t service_id,
                                                      uint32_t min_budget,
                                                      uint32_t max_budget);
bool scheduler_ipc_predictive_pressure_self_test(void);
bool scheduler_ipc_service_select_adaptive(const scheduler_ipc_service_id_t *candidates,
                                           uint32_t candidate_count,
                                           scheduler_ipc_service_id_t *selected_service_id);
bool scheduler_ipc_adaptive_load_self_test(void);
bool scheduler_ipc_service_set_circuit_breaker(scheduler_ipc_service_id_t service_id,
                                                uint32_t failure_threshold,
                                                uint64_t reset_timeout_ticks);
bool scheduler_ipc_service_get_health(scheduler_ipc_service_id_t service_id,
                                      scheduler_ipc_service_health_t *health);
bool scheduler_ipc_service_unregister(scheduler_ipc_service_id_t service_id);
scheduler_ipc_delivery_result_t scheduler_ipc_service_send_ex(
    scheduler_ipc_service_id_t source_service_id,
    scheduler_ipc_service_id_t destination_service_id,
    uint32_t request_id,
    const scheduler_ipc_message_t *message);
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
scheduler_ipc_delivery_result_t scheduler_ipc_service_send_request_ex(
    scheduler_ipc_service_id_t source_service_id,
    scheduler_ipc_service_id_t destination_service_id,
    uint32_t request_id,
    const scheduler_ipc_message_t *request);
scheduler_ipc_delivery_result_t scheduler_ipc_service_send_reliable(
    scheduler_ipc_service_id_t source_service_id,
    scheduler_ipc_service_id_t destination_service_id,
    uint32_t delivery_id, uint32_t request_id,
    const scheduler_ipc_message_t *message, uint32_t max_attempts);
scheduler_ipc_delivery_result_t scheduler_ipc_service_send_reliable_timed(
    scheduler_ipc_service_id_t source_service_id,
    scheduler_ipc_service_id_t destination_service_id,
    uint32_t delivery_id, uint32_t request_id,
    const scheduler_ipc_message_t *message, uint32_t max_attempts,
    uint64_t retry_interval_ticks, uint64_t deadline_ticks);
scheduler_ipc_delivery_result_t scheduler_ipc_service_send_reliable_backoff(
    scheduler_ipc_service_id_t source_service_id,
    scheduler_ipc_service_id_t destination_service_id,
    uint32_t delivery_id, uint32_t request_id,
    const scheduler_ipc_message_t *message, uint32_t max_attempts,
    uint64_t base_retry_interval_ticks, uint64_t max_retry_interval_ticks,
    uint64_t deadline_ticks);
uint32_t scheduler_ipc_dead_letter_count(void);
bool scheduler_ipc_dead_letter_receive(scheduler_ipc_dead_letter_t *entry);
bool scheduler_ipc_timer_retry_dead_letter_self_test(void);
bool scheduler_ipc_circuit_breaker_self_test(void);
bool scheduler_ipc_service_ack_delivery(scheduler_ipc_service_id_t destination_service_id,
                                        uint32_t delivery_id);
scheduler_ipc_delivery_result_t scheduler_ipc_delivery_retry(uint32_t delivery_id);
bool scheduler_ipc_delivery_get_receipt(uint32_t delivery_id,
                                        scheduler_ipc_delivery_receipt_t *receipt);
uint32_t scheduler_ipc_delivery_pending_count(void);
bool scheduler_ipc_receipt_retry_self_test(void);
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
uint32_t scheduler_ipc_trace_count(void);
bool scheduler_ipc_trace_read(uint32_t oldest_first_offset, scheduler_ipc_trace_event_t *event);
bool scheduler_ipc_trace_self_test(void);
void scheduler_ipc_get_diagnostics(scheduler_ipc_diagnostics_t *diagnostics);
bool scheduler_ipc_runtime_self_test(void);
bool scheduler_ipc_lifecycle_self_test(void);
bool scheduler_ipc_backpressure_self_test(void);

bool scheduler_ipc_service_self_test(void);
bool scheduler_ipc_request_response_discovery_self_test(void);
bool scheduler_blocking_sync_self_test(void);
void *scheduler_on_timer_interrupt(void *interrupt_frame);
bool scheduler_preemptive_self_test(void);
uint64_t scheduler_preemptive_switch_count(void);
uint64_t scheduler_preemptive_counter_a(void);
uint64_t scheduler_preemptive_counter_b(void);
#endif

# HYBRID OS v0.8.0 — Semaphores & IPC Foundations

## Scope

Add a bounded counting semaphore and a fixed-size message channel to the current single-core, preemptive Ring 0 kernel.

## Semaphore API

```c
bool scheduler_semaphore_init(scheduler_semaphore_t *semaphore,
                              uint32_t initial_count, uint32_t max_count);
bool scheduler_semaphore_wait(scheduler_semaphore_t *semaphore);
bool scheduler_semaphore_post(scheduler_semaphore_t *semaphore);
```

- Initialization rejects a null pointer, a zero maximum, or an initial count larger than the maximum.
- `wait` decrements an available count. When the count is zero, a running preemptive task blocks on the semaphore wait queue.
- `post` hands a token directly to one waiter where possible; otherwise it increments the count, up to `max_count`. Posting while at the maximum returns false.
- Semaphore operations use single-core interrupt exclusion; they are not SMP locks.

## IPC channel

```c
#define SCHEDULER_IPC_CAPACITY 4U
#define SCHEDULER_IPC_PAYLOAD_SIZE 24U

typedef struct {
    uint32_t type;
    uint32_t length;
    uint8_t payload[SCHEDULER_IPC_PAYLOAD_SIZE];
} scheduler_ipc_message_t;

void scheduler_ipc_channel_init(scheduler_ipc_channel_t *channel);
bool scheduler_ipc_send(scheduler_ipc_channel_t *channel,
                        const scheduler_ipc_message_t *message);
bool scheduler_ipc_receive(scheduler_ipc_channel_t *channel,
                           scheduler_ipc_message_t *message);
```

- Every channel has a fixed ring of four messages, each with a 32-bit type and length plus at most 24 payload bytes.
- Sending to a full channel blocks the current preemptive task on the writers queue.
- Receiving from an empty channel blocks the current preemptive task on the readers queue.
- Enqueue/dequeue wakes one opposite-side waiter when present.
- Message lengths beyond 24 bytes, null channel/message pointers, and invalid ring state are rejected.
- Message ordering is FIFO per channel. This foundation has no endpoint security, dynamic allocation, channel close/deletion, timeout parameters or cross-process isolation.

## Validation

Run locally on Ubuntu/Debian:

```sh
sudo apt update
sudo apt install -y build-essential binutils git curl xorriso qemu-system-x86
make clean
make test
```

Required serial markers:

- `HYBRID: BLOCKING_SYNC_SELFTEST_OK`
- `HYBRID: TIMER_TIMEOUT_CANCEL_SELFTEST_OK`
- `HYBRID: SEMAPHORE_IPC_SELFTEST_OK`
- `HYBRID: BOOT_OK`

The v0.8.0 self-test checks semaphore token handoff and count bounds, blocked-reader wakeup, a sender blocked by a full ring, FIFO ordering/payload integrity, no residual waiters, and task cleanup.

## Limitations

- Single-core x86-64, Ring 0 tasks.
- Interrupt disabling is not sufficient for SMP. Do not use this implementation as an SMP synchronization primitive.
- No user-mode endpoints, permissions/capabilities, server process model, channel lifetime management, backpressure policy beyond blocking, priority inheritance or IPC timeouts.
- QEMU pass covers only the configured emulator profile; it does not certify physical hardware.

## Release rule

Status remains **CI validation pending** until the latest source revision passes compilation and QEMU smoke tests, including `HYBRID: SEMAPHORE_IPC_SELFTEST_OK` and `HYBRID: BOOT_OK`.

# HYBRID OS v0.11.0 — IPC Runtime, Pending Calls & Concurrent Reply Demultiplexing

## Scope

Add a kernel-managed pending-call runtime on top of the v0.10.0 service protocol. Concurrent calls may target the same service; responses/errors must be delivered to the matching waiting task by requester service, responder service and request ID.

## Public API

```c
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
```

## Semantics

- The runtime has eight pending slots and generates non-zero, monotonically increasing 32-bit request IDs. When the ID counter wraps to zero, new calls fail rather than recycling IDs.
- Calls require a running preemptive Ring 0 task and request/response capability on both endpoints. A non-zero finite timeout is mandatory.
- The caller registers a pending record before sending the request. Timeout begins when the request has been sent and the caller registers its response wait; the send itself can still block behind a full destination queue until space opens or the call is explicitly cancelled.
- A reply matches the tuple `(source service ID, responder/destination service ID, request ID)`. It is copied directly into that pending record and wakes only that caller. A RESPONSE with status OK or an ERROR with a non-OK status is returned as a matched result; callers inspect `kind` and `status`.
- If a source/destination pair has active pending calls, a response with an unknown correlation ID is rejected instead of being enqueued in the ordinary shared service queue.
- Timeout and cancellation invalidate the pending record and wake the caller. A bounded recent-expiration history drops late replies after the active slot is released. The history is intentionally bounded; this is not a durable replay-protection mechanism.
- `scheduler_ipc_call_cancel()` cancels only a still-pending call. Cancellation too late (already completed/expired) returns false. Running task code is not force-killed; cancellation unwinds the blocked IPC call to its caller.
- Legacy `scheduler_ipc_service_send_request()` + `scheduler_ipc_service_receive()` + `scheduler_ipc_service_reply()` remains supported if no runtime call matches.

## QEMU validation

Run:

```sh
make clean
make test
```

Required markers:
- `HYBRID: IPC_REQUEST_RESPONSE_SELFTEST_OK`
- `HYBRID: IPC_RUNTIME_SELFTEST_OK`
- `HYBRID: BOOT_OK`

The runtime self-test runs two clients at once, injects a bad request ID, returns replies in reverse order (OK for one call, typed ERROR for the other), times out a third call, explicitly cancels a fourth and verifies that late replies are dropped, no pending slots remain and service endpoints can be unregistered after cleanup.

## Limitations

- Single-core x86-64 kernel; interrupt exclusion is not an SMP lock.
- Ring 0 only; no process isolation or endpoint ACL enforcement.
- No retry policy, idempotency keys, server crash recovery, priority arbitration, per-client quotas or distributed RPC guarantees.
- A timeout covers the response wait after a request has been sent; a request blocked on a full service queue is released by explicit cancellation, but does not have a separate send-queue timeout.
- CI PASS certifies only the tested QEMU profile.

## Release rule

Status remains **CI validation pending** until the latest commit emits `IPC_RUNTIME_SELFTEST_OK` and `BOOT_OK` in a successful QEMU CI run.
# HYBRID OS v0.10.0 — IPC Request/Response Protocol & Service Discovery

## Goal

Introduce a small, versioned request/response protocol on the v0.9 named-service registry and bounded FIFO endpoints. Provide explicit response/error status and service enumeration without claiming a full RPC runtime.

## Protocol v1

Each `scheduler_ipc_message_t` includes:
- `protocol_version`: currently `SCHEDULER_IPC_PROTOCOL_VERSION` (1).
- `kind`: DATA, REQUEST, RESPONSE, ERROR or EVENT.
- `status`: OK, INVALID_ARGUMENT, NOT_FOUND, UNSUPPORTED, BUSY or INTERNAL.
- `type` and `length` with up to 24 payload bytes.
- `source_service_id` and `request_id` for origin and correlation.

## APIs

```c
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
bool scheduler_ipc_service_register_ex(const char *name, uint32_t capabilities,
                                       scheduler_ipc_service_id_t *service_id);
bool scheduler_ipc_service_enumerate(uint32_t active_index,
                                     scheduler_ipc_service_info_t *info);
bool scheduler_ipc_service_get_info(scheduler_ipc_service_id_t service_id,
                                    scheduler_ipc_service_info_t *info);
```

## Semantics

- `send_request` requires request ID != 0, payload length <= 24 and REQUEST_RESPONSE capability on both endpoints. It stamps protocol version 1, kind REQUEST and status OK.
- `reply` accepts only a protocol-v1 REQUEST with non-zero request ID and valid source service. It routes the response back to the request source and preserves request ID. Status OK yields kind RESPONSE; any defined nonzero status yields kind ERROR.
- `message_is_response_for` returns true for RESPONSE or ERROR only when protocol version and request ID match. A client is responsible for allocating unique in-flight request IDs and reading its service queue; the kernel does not yet keep a pending-request table or demultiplex concurrent RPC replies.
- Service registration capabilities are one-way, request/response, and events. Unknown capability bits are rejected. Existing `scheduler_ipc_service_register()` remains compatible and enables all currently defined capabilities.
- Service discovery enumerates active services in registry-slot order. `get_info` returns a snapshot of ID, name, capabilities, queued message count and queue capacity; that snapshot may change immediately after the call.
- Service IDs remain monotonic and are not recycled. Registry remains limited to eight active services.

## Validation

```sh
make clean
make test
```

Required serial markers:
- `HYBRID: IPC_SERVICES_SELFTEST_OK`
- `HYBRID: IPC_REQUEST_RESPONSE_SELFTEST_OK`
- `HYBRID: BOOT_OK`

The QEMU self-test covers service enumeration and lookup, capabilities, successful response, typed UNSUPPORTED error response, request-ID matching/mismatch, malformed kind/version, invalid request IDs, and service/queue cleanup.

## Limitations

- Single-core x86-64 Ring 0 only; IRQ exclusion is not SMP synchronization.
- No user-mode ACL/capabilities enforcement, Ring 3 endpoint isolation, pending-request demultiplexer, protocol negotiation, request deadlines, cancellation/retry semantics, quotas or transport across machines.
- A client must serialize or otherwise coordinate reads on its endpoint when multiple concurrent request/reply flows are active.
- QEMU validates only the configured emulator profile.

## Release gate

Status remains CI validation pending until the latest source revision builds and emits `HYBRID: IPC_REQUEST_RESPONSE_SELFTEST_OK` and `HYBRID: BOOT_OK` in CI.
# HYBRID OS v0.18.0 — IPC Event Trace, Runtime Observability & Regression Diagnostics

- `scheduler_ipc_trace_count()` zwraca liczbę aktualnych wpisów.
- `scheduler_ipc_trace_read(offset, &event)` odczytuje od najstarszego wpisu.
- Każdy rekord zawiera sequence, tick PIT, typ, endpointy, request ID, delivery ID, wynik i głębokość kolejki; payload nie jest zapisywany.
- Pojemność wynosi 32; po przepełnieniu nadpisywany jest najstarszy wpis.
- Wymagany marker QEMU: `HYBRID: IPC_TRACE_SELFTEST_OK`.
- Trace jest ulotny i nie zapewnia trwałego audytu ani SMP safety.

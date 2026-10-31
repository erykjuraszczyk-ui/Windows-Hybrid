# HYBRID OS v0.29.0 — IPC Worker Runtime, Worker Lifecycle & Lease Recovery

## Cel

Przejście od logicznych slotów dispatch z v0.28.0 do ograniczonego runtime workerów IPC powiązanego z istniejącym Scheduler Core.

## Implementacja

- Stała pula maksymalnie czterech deskryptorów workerów.
- `scheduler_ipc_worker_start()` tworzy worker jako kooperatywne zadanie `scheduler_create_task()`.
- Worker rezerwuje pojemność usługi przez dispatch acquire, pobiera wiadomość i wywołuje jawny handler.
- Wiadomość pozostaje w deskryptorze podczas ograniczonych ponowień.
- Lease ma termin w tickach PIT; po wygaśnięciu worker przechodzi przez ścieżkę recovery.
- `scheduler_ipc_worker_stop()`, `scheduler_ipc_worker_get_info()` i `scheduler_ipc_worker_maintenance()` obsługują lifecycle i odzyskiwanie zasobów.

## Walidacja

`make test` musi zakończyć się powodzeniem, a log QEMU musi zawierać:

- `HYBRID: IPC_CAPACITY_DISPATCH_SELFTEST_OK`
- `HYBRID: IPC_WORKER_RUNTIME_SELFTEST_OK`
- `HYBRID: BOOT_OK`

## Ograniczenia

To kooperatywny worker runtime w jednordzeniowym Ring 0. Handler jest synchroniczny. Lease recovery nie może bezpiecznie przerwać callbacku aktualnie wykonywanego na CPU. Nie ma SMP, Ring 3, izolacji procesów ani twardych gwarancji czasu rzeczywistego. Pula jest stała; brak dynamicznej alokacji deskryptorów.

# HYBRID OS v0.30.0 — IPC Worker Pool Scaling, Cooperative Dispatch & Fault Isolation

## Cel

Rozszerzenie pojedynczego workera z v0.29.0 do zarządzanej puli workerów przypisanej do usługi IPC.

## API

- `scheduler_ipc_worker_pool_set_size(service_id, desired_workers, handler, context, lease_timeout_ticks)` — ustawia docelową liczbę workerów (0–4).
- `scheduler_ipc_worker_pool_get_info(service_id, &info)` — odczytuje stany puli, maksimum obserwowane, liczbę awarii i zdarzenia skalowania.
- `scheduler_ipc_worker_pool_self_test()` — testuje dwóch workerów, niezależny postęp obsługi wiadomości i odizolowanie workera, który wyczerpał retry.

## Zachowanie

- Skalowanie w górę tworzy kolejne zadania przez istniejący Scheduler Core.
- Skalowanie w dół oznacza przejście wybranych workerów do STOPPING; pamięć i task są odzyskiwane przez maintenance po zakończeniu zadania.
- Po wyczerpaniu limitu retry worker przechodzi w FAILED. Inne workery pozostają aktywne.
- Telemetria raportuje liczbę workerów w poszczególnych stanach, maksimum, scale-up/down i izolowane awarie.

## Walidacja QEMU

Wymagane markery:

- `HYBRID: IPC_WORKER_RUNTIME_SELFTEST_OK`
- `HYBRID: IPC_WORKER_POOL_SELFTEST_OK`
- `HYBRID: BOOT_OK`

## Ograniczenia

Pula ma stałą pojemność czterech workerów. „Parallel dispatch” oznacza współbieżny postęp zadań kooperatywnych w jednym schedulerze, nie prawdziwą równoległość SMP. Callbacki są synchroniczne i wykonują się w Ring 0; brak izolacji pamięci i siłowego przerywania handlera.

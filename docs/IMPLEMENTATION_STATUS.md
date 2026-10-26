# Status implementacji i ograniczenia

## v0.1.0–v0.4.0

- v0.1.0: minimalny kernel i QEMU boot smoke test.
- v0.2.0: Memory Core, IDT, test int3 i PIT w skonfigurowanym QEMU.
- v0.3.0: kooperatywny round-robin Scheduler Core.
- v0.4.0: ograniczony preemptive context-switch test w QEMU; `PREEMPTIVE_SELFTEST_OK` oraz `BOOT_OK`.

## HYBRID OS v0.5.0 — Task Lifecycle, TSS/IST

**CI PASS w skonfigurowanym QEMU.**

Potwierdzający przebieg:
https://github.com/erykjuraszczyk-ui/Windows-Hybrid/actions/runs/40307342604

Końcowe markery:
- `HYBRID: TSS_OK`
- `HYBRID: IDT_OK`
- `HYBRID: IST_OK`
- `HYBRID: EXCEPTION_SELFTEST_OK`
- `HYBRID: TIMER_OK ticks=0x0000000000000005`
- `HYBRID: SCHEDULER_SELFTEST_OK dispatches=0x000000000000000B`
- `HYBRID: TASK_LIFECYCLE_SELFTEST_OK`
- `HYBRID: PREEMPTIVE_SELFTEST_OK switches=0x0000000000000001 task_a=0x000000000000C350 task_b=0x000000000000C350`
- `HYBRID: BOOT_OK`

Implementacja:
- własna GDT z selektorami Ring 0 oraz deskryptorem TSS 64-bit;
- TSS.RSP0 i cztery dedykowane stosy IST (każdy w osobnej stronie Memory Core);
- IST1 dla #DF, IST2 dla #GP, IST3 dla #PF, IST4 dla NMI; samotest potwierdza deskryptor TR i przypisania w IDT;
- `scheduler_task_create` i `scheduler_task_reap`, monotoniczne identyfikatory zadań, stany EXITED/FAILED, możliwość ponownego użycia zwolnionego slotu;
- test dwóch skończonych zadań potwierdza 50 000 iteracji dla każdego z nich, przełączenie kontekstu i odzyskanie stron stosów.

Weryfikacja IST obejmuje konfigurację GDT/TSS/IDT oraz kontrolę indeksów IST. Nie wykonuje jeszcze wymuszonego double fault/page fault jako testu fault-injection.

## Ograniczenia

- Jednordzeniowy kernel, zadania Ring 0. TSS/IST nie jest izolacją bezpieczeństwa procesów.
- Brak Ring 3, syscall ABI, SMP, FPU/SSE/AVX context-save/restore, blokowania/wybudzania i ogólnego systemu synchronizacji.
- Wyniki QEMU potwierdzają wyłącznie testowaną konfigurację emulatora, nie fizyczny sprzęt.

Funkcje poza zakresem zaliczonych testów pozostają SPECIFIED / NOT VERIFIED.


## HYBRID OS v0.6.0 — Task Blocking, Wakeup & Synchronization Core

**Status: CI PASS in QEMU.**

- Added the `BLOCKED` task state and guarded `scheduler_task_block()` / `scheduler_task_wake()` transitions for ready step-based tasks.
- Added preemptive-task wait queues with wake-one and wake-all operations. A waiting task records its queue and is made READY by a wake operation; the timer scheduler does not silently turn BLOCKED tasks back into READY.
- Added a non-recursive mutex with owner checking and a wait queue. These primitives are designed for the current single-core Ring 0 kernel only.
- Added a QEMU self-test for wait/wakeup and mutex contention, with the marker `HYBRID: BLOCKING_SYNC_SELFTEST_OK`.

The implementation is not considered validated until `make test` passes in CI and the marker is present in the serial log. This does not provide SMP-safe locking, priority inheritance, timeouts, cancellation, or process isolation.


## HYBRID OS v0.7.0 — Timers, Timeouts & Cancellation

**Status: CI PASS w QEMU.**

Potwierdzający przebieg: https://github.com/erykjuraszczyk-ui/Windows-Hybrid/actions/runs/40338105255

- `scheduler_sleep_ticks(ticks)`: block the current preemptive task until a relative PIT tick deadline expires.
- `scheduler_wait_queue_wait_timeout(queue, timeout_ticks)`: wait for a queue wakeup or return false after its deadline.
- Tick-expiry processing runs from the PIT-driven scheduler path and removes the task from its wait queue before marking it READY.
- `scheduler_task_cancel(id)` cancels READY/BLOCKED tasks only, unlinks pending waiters, clears deadlines and marks the task CANCELLED. Running tasks are deliberately not force-terminated.
- Reaping accepts CANCELLED tasks only after the scheduler has stopped and the task is no longer linked to a wait queue.
- Regression test covers timeout after 4 ticks, sleep completion after 6 ticks and cancellation of an indefinitely blocked task.

Potwierdzone markery QEMU: `HYBRID: TIMER_TIMEOUT_CANCEL_SELFTEST_OK` i `HYBRID: BOOT_OK`. Current implementation remains single-core Ring 0 and is not SMP-safe.


## HYBRID OS v0.8.0 — Semaphores & IPC Foundations

**Status: CI PASS w QEMU po scaleniu.**

Potwierdzający przebieg: https://github.com/erykjuraszczyk-ui/Windows-Hybrid/actions/runs/40345738501

- `scheduler_semaphore_t` implements a bounded counting semaphore with block-on-zero wait and direct token handoff to a selected waiter. `scheduler_semaphore_init()` rejects zero maximums and initial counts greater than the maximum.
- `scheduler_ipc_channel_t` implements a fixed ring buffer of four messages per channel. Each message has a 32-bit type, a 32-bit payload length, and up to 24 payload bytes.
- `scheduler_ipc_send()` blocks the calling preemptive task when the channel is full and wakes one reader after enqueue. `scheduler_ipc_receive()` blocks when empty and wakes one writer after dequeue.
- Wait registration and channel/semaphore state changes are protected by disabling interrupts. This is intended for the current single-core Ring 0 kernel only; it does not provide SMP synchronization, endpoint permissions, process isolation, channel destruction, or message timeouts.
- QEMU regression test validates null/invalid inputs, semaphore count bounds and handoff, receiver wakeup on an empty channel, sender blocking when the ring is full, message ordering/content, and empty wait queues after completion.

Required QEMU markers: `HYBRID: SEMAPHORE_IPC_SELFTEST_OK` and `HYBRID: BOOT_OK`. Do not mark v0.8.0 PASS until the current code revision builds and the test marker is present in CI logs.


## HYBRID OS v0.9.0 — IPC Services & Kernel Communication Layer

**Status: CI PASS po scaleniu.**

Potwierdzający przebieg: https://github.com/erykjuraszczyk-ui/Windows-Hybrid/actions/runs/40350916069

- `scheduler_ipc_service_register/lookup/unregister` zapewniają rejestrację, wyszukiwanie i kontrolowane wyrejestrowanie do 8 usług; nazwy są case-sensitive, do 15 bajtów plus NUL.
- IDs są niezerowe, monotoniczne i nie są ponownie używane; rejestracja zostaje zablokowana po wyczerpaniu przestrzeni ID.
- `scheduler_ipc_service_send(source, destination, request_id, message)` kieruje wiadomości FIFO i dopisuje source service ID oraz request/correlation ID. `scheduler_ipc_service_receive(service_id, message)` odbiera z kolejki wskazanej usługi.
- Wyrejestrowanie jest odrzucane, gdy istnieją wiadomości, waiterzy lub aktywne wywołania API. Aktywne referencje chronią endpoint podczas blokującego send/receive.
- Test QEMU sprawdza niepoprawne nazwy, duplikaty, lookup, routing i metadane, odbiorcę blokującego się na pustym kanale, ochronę endpointu z wiadomością oraz czyste wyrejestrowanie.

Potwierdzone markery: `HYBRID: IPC_SERVICES_SELFTEST_OK` i `HYBRID: BOOT_OK`. Funkcja pozostaje prototypem jednordzeniowego Ring 0; brak autoryzacji usług, izolacji procesów, SMP, limitów ruchu i dynamicznego zamykania kanałów z oczekującymi klientami.


## HYBRID OS v0.10.0 — IPC Request/Response Protocol & Service Discovery

**Status: implementation committed on a feature branch; QEMU CI validation pending.**

- Protokół wiadomości v1 zawiera `protocol_version`, `kind`, `status`, `source_service_id` i `request_id` oraz typ i payload. Typy: DATA, REQUEST, RESPONSE, ERROR i EVENT.
- `scheduler_ipc_service_send_request()` wymaga niezerowego request ID oraz uprawnień capability REQUEST_RESPONSE u źródła i celu. `scheduler_ipc_service_reply()` sprawdza, czy wejście jest żądaniem protokołu v1, zachowuje request ID i zwraca RESPONSE dla OK lub ERROR dla statusu niezerowego.
- `scheduler_ipc_message_is_response_for()` weryfikuje wersję protokołu, rodzaj odpowiedzi i zgodność identyfikatora korelacyjnego.
- Discovery udostępnia `scheduler_ipc_service_register_ex()`, `scheduler_ipc_service_enumerate()` oraz `scheduler_ipc_service_get_info()`, w tym capabilities i bieżącą głębokość kolejki.
- Rejestracja nadal ograniczona do ośmiu usług; identyfikatory nie są recyklingowane. Capability mask ma walidację. Usługa domyślna zachowuje dotychczasową kompatybilność API.
- Test QEMU pokrywa odpowiedź OK, odpowiedź błędną UNSUPPORTED, zgodność/niezgodność request ID, błędną wersję i rodzaj wiadomości, enumerację, duplicate/case-sensitive lookup oraz czyszczenie usług.

Wymagane markery: `HYBRID: IPC_REQUEST_RESPONSE_SELFTEST_OK` i `HYBRID: BOOT_OK`. CI musi przejść dla najnowszej wersji źródeł przed zatwierdzeniem. Prototyp pozostaje single-core Ring 0, bez ACL, Ring 3, SMP, timeoutów request/reply i tablicy pending-call zarządzanej przez kernel.

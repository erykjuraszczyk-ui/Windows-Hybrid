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

**Status: CI PASS w QEMU.**

Potwierdzający przebieg: https://github.com/erykjuraszczyk-ui/Windows-Hybrid/actions/runs/40354869320

- Protokół wiadomości v1 zawiera `protocol_version`, `kind`, `status`, `source_service_id` i `request_id` oraz typ i payload. Typy: DATA, REQUEST, RESPONSE, ERROR i EVENT.
- `scheduler_ipc_service_send_request()` wymaga niezerowego request ID oraz uprawnień capability REQUEST_RESPONSE u źródła i celu. `scheduler_ipc_service_reply()` sprawdza, czy wejście jest żądaniem protokołu v1, zachowuje request ID i zwraca RESPONSE dla OK lub ERROR dla statusu niezerowego.
- `scheduler_ipc_message_is_response_for()` weryfikuje wersję protokołu, rodzaj odpowiedzi i zgodność identyfikatora korelacyjnego.
- Discovery udostępnia `scheduler_ipc_service_register_ex()`, `scheduler_ipc_service_enumerate()` oraz `scheduler_ipc_service_get_info()`, w tym capabilities i bieżącą głębokość kolejki.
- Rejestracja nadal ograniczona do ośmiu usług; identyfikatory nie są recyklingowane. Capability mask ma walidację. Usługa domyślna zachowuje dotychczasową kompatybilność API.
- Test QEMU pokrywa odpowiedź OK, odpowiedź błędną UNSUPPORTED, zgodność/niezgodność request ID, błędną wersję i rodzaj wiadomości, enumerację, duplicate/case-sensitive lookup oraz czyszczenie usług.

Wymagane markery: `HYBRID: IPC_REQUEST_RESPONSE_SELFTEST_OK` i `HYBRID: BOOT_OK`. CI musi przejść dla najnowszej wersji źródeł przed zatwierdzeniem. Prototyp pozostaje single-core Ring 0, bez ACL, Ring 3, SMP, timeoutów request/reply i tablicy pending-call zarządzanej przez kernel.


## HYBRID OS v0.11.0 — IPC Runtime, Pending Calls & Concurrent Reply Demultiplexing

**Status: implementation committed on a feature branch; QEMU CI validation pending.**

- Adds a bounded table of eight pending calls. The kernel generates monotonically increasing, non-zero request IDs and stops allocating IDs if the 32-bit counter exhausts rather than reusing identifiers.
- `scheduler_ipc_service_call()` atomically reserves a pending entry, pins requester/responder service references, sends a version-1 request and blocks the calling preemptive task until a reply, timeout or cancellation.
- `scheduler_ipc_service_reply()` routes a matching response/error to the task waiting on that exact requester/responder/request-ID tuple. Concurrent replies can arrive out of order without being mixed into the shared service receive queue.
- Timeouts/cancellation mark the call stale, unblock the owner and record a bounded recent-expiration history. Late replies are dropped, not enqueued for subsequent calls. Wrong IDs are rejected while a matching service pair has pending calls.
- Existing low-level send-request/service-receive APIs remain available for legacy callers without a registered runtime pending call.
- QEMU regression test launches two concurrent clients, replies in reverse order with one OK response and one typed error, injects a wrong correlation ID, then exercises a timed-out call and an explicitly cancelled call whose late reply must be dropped.

Required markers: `HYBRID: IPC_RUNTIME_SELFTEST_OK` and `HYBRID: BOOT_OK`. Status must remain pending until the latest source revision passes the QEMU smoke test.

Limitations: single-core Ring 0 prototype; no SMP safety, Ring 3 isolation, server-level authentication, call priorities or retry policy. The expiration history is bounded; this is an internal runtime primitive, not a distributed RPC guarantee.


## HYBRID OS v0.12.0 — IPC Reliability & Service Lifecycle Core

**Status: CI validation pending.**

- Usługa przechodzi w stan `closing`: nie przyjmuje nowych operacji i znika z discovery, ale istniejące referencje chronią jej slot do zakończenia aktywnego użycia.
- Wyrejestrowanie odrzuca zakolejkowane wiadomości, wybudza zadania zablokowane na kanale i kończy aktywne wywołania request/response typowanym błędem `SCHEDULER_IPC_STATUS_NOT_FOUND`.
- Slot usługi jest zwalniany dopiero po wyzerowaniu referencji aktywnych operacji; ID usługi nie jest ponownie używane.
- `scheduler_ipc_get_diagnostics()` udostępnia liczniki wywołań, timeoutów, anulowań, brakujących usług, błędów wysyłki, odrzuconych odpowiedzi oraz zamknięć usług.
- QEMU self-test zamyka serwer po odebraniu żądania, weryfikuje przebudzenie klienta z `ERROR/NOT_FOUND` oraz brak pozostałych pending calls i usług.

Ograniczenia: jednordzeniowy Ring 0; wyłączenie przerwań nie zapewnia synchronizacji SMP. Zamknięcie jest logicznie natychmiastowe, ale slot odzyskiwany jest po zwolnieniu ostatniej referencji. Brak izolacji procesów i autoryzacji usług.

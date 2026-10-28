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


## HYBRID OS v0.13.0 — IPC Backpressure, Queue Policies & Delivery Guarantees

**Status: CI validation pending.**

- Każda usługa ma konfigurowalną politykę kolejki: `BLOCK`, `REJECT`, `DROP_NEWEST`, `DROP_OLDEST`. Domyślnie pozostaje `BLOCK`, zachowując dotychczasową semantykę zgodności API.
- `scheduler_ipc_service_send_ex()` wykonuje próbę nieblokującą i odróżnia `DELIVERED`, `DELIVERED_DROPPED_OLDEST`, `BACKPRESSURE`, `DROPPED_NEWEST`, `SERVICE_NOT_FOUND`, `SERVICE_CLOSED` i błędne argumenty.
- `scheduler_ipc_service_send_request_ex()` dostarcza tak samo jawny wynik dla żądań request/response.
- Per-service info i globalne diagnostics raportują liczbę przyjętych/odebranych wiadomości, backpressure, odrzucenia, drops i największą obserwowaną głębokość kolejki.
- QEMU test weryfikuje kolejki o stałej pojemności, wynik przepełnienia i właściwe ID wiadomości zachowane dla wszystkich czterech polityk.

**Kontrakt dostarczenia:** `DELIVERED` oznacza, że wiadomość została atomowo przyjęta do kolejki. Nie stanowi potwierdzenia odebrania, przetworzenia ani trwałego zapisu; polityki DROP jawnie pozwalają na utratę wiadomości.

Ograniczenia: jednordzeniowy Ring 0; sekcje krytyczne nie są blokadami SMP. Pojemność kanału nadal jest stała na poziomie czterech wiadomości.


## HYBRID OS v0.14.0 — IPC Delivery Receipts, Acknowledgements & Retry Core

**Status: CI PASS po scaleniu.** Przebieg [GitHub Actions #40509463072](https://github.com/erykjuraszczyk-ui/Windows-Hybrid/actions/runs/40509463072) zbudował ISO, uruchomił testy w QEMU i potwierdził markery `IPC_RECEIPTS_RETRIES_SELFTEST_OK`, `IPC_DIAGNOSTICS_OK` oraz `BOOT_OK`.

- `scheduler_ipc_service_send_reliable()` rozpoczyna śledzoną dostawę z niezerowym `delivery_id` oraz limitem prób.
- `scheduler_ipc_delivery_get_receipt()` udostępnia stan, numer próby, limit, endpointy, request ID i ostatni wynik.
- `scheduler_ipc_service_ack_delivery()` przyjmuje ACK dopiero po odczytaniu wiadomości przez wskazaną usługę.
- `scheduler_ipc_delivery_retry()` wykonuje pojedynczą próbę na żądanie; ponowna próba zostaje stłumiona, jeśli ta sama dostawa jest już w kolejce lub znajduje się w ograniczonej historii odbiorcy.
- Po osiągnięciu `max_attempts` stan przechodzi do `EXHAUSTED`. Stała tablica utrzymuje maksymalnie 8 śledzonych dostaw; wpisy terminalne mogą zostać zastąpione przez nową dostawę.
- Test QEMU pokrywa dostarczenie, ACK, suppress duplikatu, przepełnienie kolejki i wyczerpanie limitu prób.

**Granice:** to niezawodność in-memory dla bieżącego jednordzeniowego kernela Ring 0. Retry uruchamia klient przez wywołanie API, nie timer ani automatyczna pętla. ACK jest obecnie prymitywem kernelowym, nie osobną wiadomością routowaną. Brak trwałości po restarcie, gwarancji exactly-once end-to-end i ochrony przed zawieszeniem/awarią odbiorcy po wykonaniu efektu ubocznego, a przed ACK.


## HYBRID OS v0.15.0 — IPC Timer-Driven Retry, Delivery Deadlines & Dead-Letter Queue

**Status: CI validation pending.** Nie oznaczaj wersji jako PASS, dopóki najnowszy przebieg GitHub Actions nie potwierdzi kompilacji ISO oraz markera QEMU.

- `scheduler_ipc_service_send_reliable_timed()` pozwala podać interwał ponowień i deadline w tickach PIT; poprzednie `scheduler_ipc_service_send_reliable()` zachowuje kompatybilność i pozostaje ręcznie sterowane.
- Ścieżka `scheduler_on_timer_interrupt()` obsługuje terminy także wtedy, gdy scheduler preempcji nie jest aktywny. Każdy tick skanuje ograniczoną tabelę dostaw; nie jest wymagana dynamiczna alokacja pamięci.
- Deadline ma pierwszeństwo przed kolejną próbą. Po wygaśnięciu dostawa przechodzi do stanu `EXPIRED` i trafia do bounded dead-letter queue z receipt, oryginalną wiadomością, powodem oraz tickiem rejestracji.
- Kolejka dead-letter ma stałą pojemność `SCHEDULER_IPC_DEAD_LETTER_CAPACITY`; w razie przepełnienia nadpisuje najstarszy wpis i zwiększa licznik `dead_letters_dropped`.
- Diagnostyka raportuje automatyczne retry, wygasłe deadline’y, wpisy dead-letter oraz liczbę wpisów aktualnie oczekujących. Test QEMU sprawdza retry timera z tłumieniem duplikatów, ACK przed deadline’em, terminalne wygaśnięcie i odczyt dead-letter.

**Semantyka:** ticki PIT są jednostką czasu kernela, nie zegarem czasu rzeczywistego. To mechanizm in-memory; DLQ i receipt giną po resecie. ACK nadal oznacza potwierdzenie przez usługę, a nie trwałość efektu ubocznego ani exactly-once.


## HYBRID OS v0.16.0 — IPC Retry Backoff, Circuit Breakers & Service Health Monitoring

**Status: CI PASS po scaleniu.** Przebieg [GitHub Actions #40522608835](https://github.com/erykjuraszczyk-ui/Windows-Hybrid/actions/runs/40522608835) zbudował ISO i zaliczył testy QEMU.

- `scheduler_ipc_service_send_reliable_backoff()` wprowadza wykładnicze odstępy retry z wartością początkową i maksymalnym limitem; stare `scheduler_ipc_service_send_reliable_timed()` zachowuje stały interwał.
- Circuit breaker jest konfigurowany per usługa (`failure_threshold`, `reset_timeout_ticks`). Stan `CLOSED` przepuszcza operacje, `OPEN` blokuje próby do końca cooldownu, a `HALF_OPEN` dopuszcza jedną próbę kontrolną.
- `scheduler_ipc_service_get_health()` raportuje stan zdrowia i obwodu, liczbę kolejnych awarii, łączne sukcesy/awarie oraz ticki ostatnich zdarzeń. Wyniki nieblokującego `scheduler_ipc_service_send_ex()` aktualizują te metryki automatycznie.
- Diagnostyka globalna liczy awarie i sukcesy zdrowia usług, otwarcia obwodu, odzyskania po udanej sondzie i odrzucone próby w stanie OPEN.
- Test QEMU zapełnia kolejkę celu, wywołuje próg awarii, sprawdza odrzucenie w OPEN, opróżnia kolejkę i potwierdza odzyskanie po cooldownie oraz ACK po udanej sondzie.

**Ograniczenia:** progi oraz cooldown są oparte na tickach PIT, a nie czasie rzeczywistym. Circuit breaker działa na ścieżce nieblokującego IPC, nie jest zabezpieczeniem SMP i nie gwarantuje exactly-once. Domyślnie jest wyłączony, aby nie zmieniać zachowania dotychczasowych klientów.


## HYBRID OS v0.17.0 — IPC Adaptive Load Balancing, Service Priorities & Congestion Control

**Status: implementacja na gałęzi funkcji; walidacja QEMU oczekuje na CI.**

- Usługi otrzymują priorytet 1–8; wartość domyślna 1 zachowuje kompatybilność. Priorytet wpływa na ważony wynik zajętości kolejki.
- `scheduler_ipc_service_select_adaptive()` wybiera najmniej obciążoną usługę z listy kandydatów, pomijając usługi zamknięte, przeciążone i z otwartym circuit breakerem. Remisy rozstrzygane są rotacyjnie.
- `scheduler_ipc_service_set_congestion_control()` ustawia próg high/low watermark. Po osiągnięciu progu wysokiego usługa odrzuca nowe nieblokujące wysyłki jako BACKPRESSURE; stan przeciążenia znika dopiero po zejściu do progu niskiego.
- QEMU self-test sprawdza priorytety, selekcję adaptacyjną, odrzucenie przy przeciążeniu i zwolnienie stanu po opróżnieniu kolejki.

Ograniczenia: dobór celu odbywa się wyłącznie w podanej grupie usług; to nie jest globalny scheduler. Metryka bazuje na głębokości kolejki, nie na czasie CPU ani latencji end-to-end. Kernel pozostaje jednordzeniowym Ring 0.


## HYBRID OS v0.18.0 — IPC Token-Bucket Rate Limiting & Admission Control

**Status: CI pending.** Wersję można oznaczyć jako PASS dopiero po przejściu builda ISO oraz testu QEMU dla aktualnego commita.

- `scheduler_ipc_service_set_rate_limit(service_id, burst_capacity, refill_tokens, refill_interval_ticks)` konfiguruje ogranicznik token-bucket po stronie odbiorcy. Koszt każdej zaakceptowanej wiadomości wynosi jeden token; bucket startuje pełny.
- `scheduler_ipc_service_send_ex()` uzupełnia tokeny na podstawie upływu ticków PIT, ogranicza ich liczbę do burst capacity i zwraca `SCHEDULER_IPC_DELIVERY_RATE_LIMITED`, gdy bucket jest pusty. Odrzucenie następuje przed zapisem do kolejki, więc wiadomość nie jest częściowo przyjmowana.
- Konfiguracja jest wyłączona domyślnie, zachowując kompatybilność istniejących usług. Limit ma stałą pamięć, nie wymaga dynamicznej alokacji i działa w sekcji krytycznej chronionej przez wyłączenie przerwań dla obecnego single-core kernela.
- `scheduler_ipc_service_info_t` i `scheduler_ipc_diagnostics_t` raportują stan tokenów oraz liczbę odrzuceń przez limiter.
- QEMU self-test sprawdza walidację parametrów, zużycie burstu, odrzucenie bez tokenów, odnowienie tokenów po tickach PIT, kolejność wiadomości i diagnostykę.

**Ograniczenia:** refill opiera się na tickach PIT, nie na zegarze czasu rzeczywistego. To admission control per odbiorca, a nie limitowanie per nadawca; wszystkie źródła współdzielą bucket celu. Nadal brak SMP safety, izolacji Ring 3 i trwałych liczników po restarcie.

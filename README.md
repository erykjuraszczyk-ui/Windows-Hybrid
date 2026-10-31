# Windows-Hybrid — HYBRID OS

**Projekt:** natywny system operacyjny x86-64.  
**Aktualny etap:** v0.29.0 — IPC Worker Runtime, Worker Lifecycle & Lease Recovery; implementacja na gałęzi funkcji, walidacja CI w toku.

## Historia statusu

- **v0.27.0 — walidacja w toku:** bounded IPC Deficit Round Robin, QoS-weighted credits, starvation aging i telemetria.

- **v0.1.0 — CI PASS:** minimalny kernel, framebuffer i diagnostyka COM1.
- **v0.2.0 — CI PASS w skonfigurowanym QEMU:** Memory Core, IDT, wyjątek i PIT.
- **v0.3.0 — CI PASS w QEMU:** kooperatywny scheduler round-robin.
- **v0.4.0 — CI PASS w QEMU:** test przełączania kontekstu dwóch zadań Ring 0 przez IRQ0.
- **v0.5.0 — CI PASS w QEMU:** GDT/TSS/IST i cykl życia zadań.
- **v0.6.0 — CI PASS w QEMU:** blokowanie, wybudzanie, kolejki oczekujących i mutex.
- **v0.7.0 — CI PASS w QEMU:** sleep, timeouty i anulowanie zadań oczekujących ([udany przebieg](https://github.com/erykjuraszczyk-ui/Windows-Hybrid/actions/runs/40338105255)).
- **v0.8.0 — CI PASS po scaleniu:** semafory i ograniczone kanały IPC ([udany przebieg](https://github.com/erykjuraszczyk-ui/Windows-Hybrid/actions/runs/40348058541)).
- **v0.9.0 — CI PASS po scaleniu:** rejestr usług IPC i routing wiadomości ([udany przebieg](https://github.com/erykjuraszczyk-ui/Windows-Hybrid/actions/runs/40350916069)).
- **v0.10.0 — CI PASS po scaleniu:** wersjonowany request/response i service discovery.
- **v0.11.0 — CI PASS po scaleniu:** pending-call runtime, timeouty, anulowanie i demultipleksowanie odpowiedzi.
- **v0.13.0 — CI PASS na gałęzi funkcji:** polityki backpressure i diagnostyka kolejek ([przebieg](https://github.com/erykjuraszczyk-ui/Windows-Hybrid/actions/runs/40489874343)).
- **v0.14.0 — CI PASS po scaleniu:** potwierdzenia dostarczenia, jawny ACK, ograniczone retry i tłumienie duplikatów ([QEMU regression](https://github.com/erykjuraszczyk-ui/Windows-Hybrid/actions/runs/40509463072)).
- **v0.15.0 — CI PASS po scaleniu:** retry sterowane PIT, terminy dostaw i dead-letter queue.
- **v0.16.0 — CI PASS po scaleniu:** wykładniczy backoff, circuit breakers i diagnostyka zdrowia usług ([QEMU regression](https://github.com/erykjuraszczyk-ui/Windows-Hybrid/actions/runs/40522608835)).
- **v0.17.0 — CI PASS po scaleniu:** adaptacyjne wybieranie celu IPC, priorytety usług i histereza przeciążenia.
- **v0.18.0 — walidacja oczekuje:** bounded event trace IPC i korelacja zdarzeń.
- **v0.19.0 — CI PASS po scaleniu:** token bucket, limity burst/refill, kontrola dopuszczenia wiadomości i diagnostyka odrzuceń ([QEMU](https://github.com/erykjuraszczyk-ui/Windows-Hybrid/actions/runs/40667570897)).
- **v0.20.0 — CI PASS po scaleniu:** fair scheduling per źródło, limity kolejkowania per źródło i admission zależne od priorytetu ([QEMU](https://github.com/erykjuraszczyk-ui/Windows-Hybrid/actions/runs/40680724904)).
- **v0.21.0 — CI PASS po scaleniu:** deadline-aware scheduling, starvation prevention i profile QoS ([QEMU](https://github.com/erykjuraszczyk-ui/Windows-Hybrid/actions/runs/40687229337)).
- **v0.12.0 — CI PASS po scaleniu:** zamykanie usług, kończenie powiązanych wywołań i liczniki diagnostyczne.

## Potwierdzenie v0.5.0

[Udany przebieg GitHub Actions #40307342604](https://github.com/erykjuraszczyk-ui/Windows-Hybrid/actions/runs/40307342604) potwierdził:
- `HYBRID: TSS_OK`, `IDT_OK` i `IST_OK`;
- `HYBRID: EXCEPTION_SELFTEST_OK` oraz `TIMER_OK ticks=5`;
- `HYBRID: TASK_LIFECYCLE_SELFTEST_OK`;
- `HYBRID: PREEMPTIVE_SELFTEST_OK switches=1 task_a=50000 task_b=50000`;
- `HYBRID: BOOT_OK`.

Artefakt CI `hybrid-os-v0.5.0` zawiera kernel ELF, obraz ISO oraz logi QEMU. Test uruchomiono w konfiguracji `qemu-system-x86_64 -machine q35 -cpu qemu64,-apic`.

## Zakres v0.5.0

- Instalacja własnej tablicy GDT z selektorami Ring 0 i deskryptorem TSS 64-bit.
- TSS zawiera osobny stos wejścia Ring 0 oraz cztery niezależne stosy IST.
- IDT przypisuje IST1 do #DF, IST2 do #GP, IST3 do #PF i IST4 do NMI.
- Task API nadaje niezerowe, monotoniczne identyfikatory, prowadzi stany READY/RUNNING/EXITED/FAILED i odrzuca próbę odzyskania stosu zadania, które jeszcze wykonuje się na tym stosie.
- Reaping jest odroczony do chwili, gdy scheduler wróci ze stosu zadania; potem zwalniana jest strona stosu i slot może zostać ponownie użyty bez ponownego użycia identyfikatora.

**Granice bezpieczeństwa:** wszystkie zadania nadal działają w Ring 0. TSS/IST zapewnia osobne stosy obsługi wejść i wyjątków, ale nie tworzy izolacji procesów ani sandboxa. Nie ma jeszcze Ring 3, SMP, zapisu FPU/SSE/AVX, blokowania/wybudzania ani kontrolowanego testu #DF przez fault-injection. Wynik QEMU nie jest certyfikacją fizycznego sprzętu.

## Uruchomienie

Na Ubuntu/Debian:

    sudo apt update
    sudo apt install -y build-essential binutils git curl xorriso qemu-system-x86
    make clean
    make test

Wymagane markery: `TSS_OK`, `IDT_OK`, `IST_OK`, `EXCEPTION_SELFTEST_OK`, `TASK_LIFECYCLE_SELFTEST_OK`, `PREEMPTIVE_SELFTEST_OK` oraz `BOOT_OK`.

## Dokumentacja

- [Indeks dokumentów](docs/INDEX.md)
- [Mapa drogowa](docs/ROADMAP.md)
- [Architektura](docs/ARCHITECTURE.md)
- [Model bezpieczeństwa](docs/SECURITY_MODEL.md)
- [Testowanie](docs/TESTING.md)
- [Proces wydań](docs/RELEASE_PROCESS.md)
- [Status implementacji](docs/IMPLEMENTATION_STATUS.md)
- [HYBRID OS v0.4.0 — Preemptive Scheduler](docs/BOOTSTRAP_v0.4.0.md)
- [HYBRID OS v0.5.0 — Task Lifecycle, TSS/IST](docs/BOOTSTRAP_v0.5.0.md)
- [HYBRID OS v0.14.0 — IPC Delivery Receipts, ACK & Retry](docs/BOOTSTRAP_v0.14.0.md)
- [HYBRID OS v0.15.0 — Timer Retry, Deadlines & Dead-Letter Queue](docs/BOOTSTRAP_v0.15.0.md)
- [HYBRID OS v0.16.0 — Retry Backoff, Circuit Breakers & Service Health](docs/BOOTSTRAP_v0.16.0.md)
- [HYBRID OS v0.18.0 — IPC Event Trace & Runtime Observability](docs/BOOTSTRAP_v0.18.0.md)
- [HYBRID OS v0.19.0 — Token-Bucket Rate Limiting & Admission Control](docs/BOOTSTRAP_v0.19.0.md)
- [HYBRID OS v0.27.0 — IPC Deficit Round Robin & Fair Admission Credits](docs/BOOTSTRAP_v0.27.0.md)


## HYBRID OS v0.7.0 — Timers, Timeouts & Cancellation

**Status: CI PASS w QEMU** ([przebieg](https://github.com/erykjuraszczyk-ui/Windows-Hybrid/actions/runs/40338105255)).

- Tick-based `scheduler_sleep_ticks()` for preemptive Ring 0 tasks.
- Timed wait queues via `scheduler_wait_queue_wait_timeout()` with explicit signaled vs timed-out results.
- Cancellation for READY/BLOCKED tasks. Running tasks cannot be force-cancelled; they must cooperate.
- Timer expiry removes wait-queue membership before returning a task to READY.
- QEMU regression test covers deadline expiry, sleep completion and cancellation of a blocked waiter.

See [HYBRID OS v0.7.0 guide](docs/BOOTSTRAP_v0.7.0.md). Confirmed markers: `HYBRID: TIMER_TIMEOUT_CANCEL_SELFTEST_OK` and `HYBRID: BOOT_OK`.


## HYBRID OS v0.8.0 — Semaphores & IPC Foundations

**Status: CI PASS w QEMU** ([CI po scaleniu](https://github.com/erykjuraszczyk-ui/Windows-Hybrid/actions/runs/40345738501)).

- Bounded counting semaphores with blocking wait and direct token handoff to one waiter.
- Fixed-capacity IPC message ring: 4 messages per channel, each with a 24-byte payload and a 32-bit type/length header.
- Blocking send on a full channel and blocking receive on an empty channel. Wait-queue transitions are made with interrupts disabled to avoid lost wakeups on the current single-core design.
- Regression test exercises semaphore handoff/overflow and IPC reader wakeup, full-channel writer blocking, message order and payload validation.

See [HYBRID OS v0.8.0 guide](docs/BOOTSTRAP_v0.8.0.md). Confirmed markers: `HYBRID: SEMAPHORE_IPC_SELFTEST_OK` and `HYBRID: BOOT_OK`.


## HYBRID OS v0.9.0 — IPC Services & Kernel Communication Layer

**Status: CI PASS po scaleniu** ([udany przebieg](https://github.com/erykjuraszczyk-ui/Windows-Hybrid/actions/runs/40350916069)).

- Fixed-capacity registry of up to 8 named kernel IPC services (names up to 15 characters, case-sensitive) with monotonic 32-bit service IDs.
- Service lookup and routed FIFO messaging. Messages carry source service ID and request/correlation ID in addition to type, length and payload.
- Unregistration is refused while messages, waiters or in-flight API calls remain; service IDs are not recycled.
- QEMU regression test checks registration/lookup, duplicate/invalid names, receiver wakeup, source/request metadata, queued-message protection and clean unregister.

See [HYBRID OS v0.9.0 guide](docs/BOOTSTRAP_v0.9.0.md). Confirmed markers: `HYBRID: IPC_SERVICES_SELFTEST_OK` and `HYBRID: BOOT_OK`.


## HYBRID OS v0.10.0 — IPC Request/Response Protocol & Service Discovery

**Status: implementation committed on a feature branch; QEMU CI validation pending.**

- Versioned IPC message metadata: protocol version, kind (`DATA`, `REQUEST`, `RESPONSE`, `ERROR`, `EVENT`) and typed status code.
- Request sends require a non-zero correlation ID. Replies reuse that ID and identify the responder; responses and typed errors are matched by `scheduler_ipc_message_is_response_for()`.
- Service discovery adds capability-aware registration, enumeration of active services and per-service info (ID, name, capabilities, queue depth and capacity).
- QEMU test exercises OK and UNSUPPORTED replies, request correlation, bad IDs/versions/kinds, duplicate/case-sensitive names and service discovery/cleanup.

See [HYBRID OS v0.10.0 guide](docs/BOOTSTRAP_v0.10.0.md). Required markers before PASS: `HYBRID: IPC_REQUEST_RESPONSE_SELFTEST_OK` and `HYBRID: BOOT_OK`.


## HYBRID OS v0.11.0 — IPC Runtime, Pending Calls & Concurrent Reply Demultiplexing

**Status: implementation committed on a feature branch; QEMU CI validation pending.**

- Adds a bounded kernel-side pending-call table with generated non-zero request IDs.
- Adds `scheduler_ipc_service_call()`: register the pending request, send it, block the caller until a matching response/error, or return false on timeout/cancellation.
- Routes replies by the tuple (requester service ID, responder service ID, request ID) to the original caller instead of enqueueing them into the shared service receive queue.
- Serializes reply delivery, timeout and cancellation with interrupt exclusion; late and mismatched replies are dropped instead of being delivered to a later call.
- Preserves legacy send-request/receive/reply APIs when no runtime pending call matches.
- QEMU regression test exercises simultaneous clients, out-of-order response/error replies, bad correlation IDs, timeout, explicit cancellation and dropped late replies.

See [HYBRID OS v0.11.0 guide](docs/BOOTSTRAP_v0.11.0.md). Do not mark PASS until `HYBRID: IPC_RUNTIME_SELFTEST_OK` and `HYBRID: BOOT_OK` appear in the latest CI log.


## HYBRID OS v0.13.0 — IPC Backpressure, Queue Policies & Delivery Guarantees

**Status: CI validation pending.**

- Dodaje polityki kolejek usług: `BLOCK` (dotychczasowe API czeka na miejsce), `REJECT`, `DROP_NEWEST` i `DROP_OLDEST`.
- Nowe `scheduler_ipc_service_send_ex()` i `scheduler_ipc_service_send_request_ex()` są nieblokujące i zwracają jawny wynik dostarczenia, przeciążenia, odrzucenia albo niedostępności usługi.
- Usługi raportują bieżącą głębokość kolejki, high-water mark oraz liczniki dostarczonych, odebranych i utraconych wiadomości.
- QEMU test weryfikuje pełną kolejkę dla wszystkich polityk oraz dokładną sekwencję odebranych identyfikatorów wiadomości.

Wynik `DELIVERED` oznacza przyjęcie wiadomości do kolejki, nie potwierdzenie jej obsłużenia przez odbiorcę ani trwałego zapisu. Szczegóły: [przewodnik v0.13.0](docs/BOOTSTRAP_v0.13.0.md).


## HYBRID OS v0.18.0 — IPC Event Trace, Runtime Observability & Regression Diagnostics

**Status: CI PASS w QEMU.** GitHub Actions [#41180355402](https://github.com/erykjuraszczyk-ui/Windows-Hybrid/actions/runs/41180355402) potwierdził kompilację, `IPC_DRR_FAIR_ADMISSION_SELFTEST_OK`, `IPC_QOS_ARBITRATION_SELFTEST_OK` i `BOOT_OK`. Dodano bounded trace ring (32 wpisy) dla zdarzeń SEND, RECEIVE, ACK, CIRCUIT, DEADLINE i DEAD_LETTER, publiczny odczyt śladu oraz liczniki zdarzeń i nadpisań. Test QEMU sprawdza kolejność sekwencji i korelację SEND → RECEIVE → ACK. Trace jest ulotny, nie zapisuje payloadów i nie stanowi trwałego audytu ani gwarancji SMP.


## Następny etap — HYBRID OS v0.22.0

**Status: specyfikacja na gałęzi funkcji; implementacja i walidacja QEMU jeszcze nie rozpoczęte.** Zakres: osobny TTL wiadomości IPC, domyślna polityka per usługa, usuwanie przeterminowanych wpisów przed odbiorem, integracja z receipt/DLQ/trace i regresja QEMU. Szczegóły: [specyfikacja v0.22.0](docs/BOOTSTRAP_v0.22.0.md).


## HYBRID OS v0.23.0 — IPC Queue Compaction & Backpressure Recovery

**Status: CI PASS w QEMU na gałęzi funkcji.** [GitHub Actions #41107058942](https://github.com/erykjuraszczyk-ui/Windows-Hybrid/actions/runs/41107058942) potwierdził kompilację, test QEMU i markery `IPC_QUEUE_TELEMETRY_SELFTEST_OK` oraz `BOOT_OK`. Kolejki raportują wolne sloty, procent wykorzystania, liczbę kompaktowań/przesunięć i odzyskań backpressure. Histereza wykorzystuje high/low watermark; wysyłanie wraca dopiero po zejściu kolejki do low watermark. Szczegóły: [specyfikacja v0.23.0](docs/BOOTSTRAP_v0.23.0.md).


## HYBRID OS v0.24.0 — IPC Dynamic Admission Control

**Status: implementation committed on feature branch; CI validation pending.** Nowy kontroler dynamicznie obniża próg high po kolejnych sygnałach przeciążenia i podnosi go po odzyskaniu kolejki. Zakres jest ograniczony do skonfigurowanych wartości min/max; rozmiar kolejki pozostaje stały. Szczegóły: [specyfikacja v0.24.0](docs/BOOTSTRAP_v0.24.0.md).


## HYBRID OS v0.25.0 — IPC Predictive Queue Pressure & Admission Budgeting

**Status: CI PASS na gałęzi funkcji.** GitHub Actions [#41119034625](https://github.com/erykjuraszczyk-ui/Windows-Hybrid/actions/runs/41119034625) potwierdził kompilację, QEMU, `IPC_PREDICTIVE_PRESSURE_SELFTEST_OK` i `BOOT_OK`. Dodano czteropróbkowe okno trendu kolejki, deterministyczną klasyfikację RISING/STABLE/FALLING i ograniczony budżet przyjęć per usługa. Pojemność kanału pozostaje stała (4); predykcja nie jest ML ani gwarancją przyszłego obciążenia.

## HYBRID OS v0.26.0 — IPC Congestion-Aware Scheduling & QoS Arbitration

**Status: CI PASS na gałęzi funkcji; PR draft oczekuje na v0.25.0.** GitHub Actions [#41120081706](https://github.com/erykjuraszczyk-ui/Windows-Hybrid/actions/runs/41120081706) potwierdził kompilację, QEMU, `IPC_QOS_ARBITRATION_SELFTEST_OK` i `BOOT_OK`. Selektor usług uwzględnia teraz obciążenie kolejki, priorytet, profil QoS, trend predykcyjny i pozostały budżet admission. Liczniki arbitrażu są dostępne per usługa i globalnie. Remisy rozstrzyga rotacja, aby ograniczać trwałe faworyzowanie jednego kandydata.

**Ograniczenia:** arbitraż wybiera usługę docelową, nie planuje zadań CPU. QoS jest preferencją wyboru, nie twardą gwarancją deadline’u. Brak dynamicznej alokacji, zmiany pojemności kolejki i gwarancji SMP.


## HYBRID OS v0.28.0 — IPC Multi-Queue Load Balancing & Adaptive Worker Dispatch

**Status: CI PASS na gałęzi funkcji.** GitHub Actions [#41180895270](https://github.com/erykjuraszczyk-ui/Windows-Hybrid/actions/runs/41180895270) potwierdził kompilację, `IPC_CAPACITY_DISPATCH_SELFTEST_OK` i `BOOT_OK`. Dodano model pojemności dispatch per usługa (1–4 logiczne sloty), adaptacyjne skalowanie w górę przy wysokim obciążeniu kolejki i w dół po jej opróżnieniu oraz atomowy acquire/release slotu. Selektor równoważy usługi na podstawie znormalizowanego obciążenia kolejki i aktywnych rezerwacji. Szczegóły: [specyfikacja v0.28.0](docs/BOOTSTRAP_v0.28.0.md).

**Ograniczenia:** sloty są logicznym limitem admission, nie tworzą wątków ani zadań CPU. Rzeczywisty worker runtime jest osobnym etapem. Brak dynamicznej alokacji i SMP safety.


## HYBRID OS v0.29.0 — IPC Worker Runtime

- Stała pula workerów IPC, powiązanych z rzeczywistymi zadaniami kooperatywnego Scheduler Core.
- Deskryptory workerów, stany READY/IDLE/RUNNING/STOPPING/FAILED i identyfikatory tasków.
- Pobieranie wiadomości, rezerwacja slotu dispatch i synchroniczny callback usługi.
- Dzierżawy w tickach PIT, ograniczone ponowienia handlera i ścieżka odzyskiwania wygasłej dzierżawy.
- Ograniczenie: jednordzeniowy Ring 0, callback synchroniczny, brak SMP, Ring 3 i gwarancji czasu rzeczywistego.

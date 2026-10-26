# Windows-Hybrid — HYBRID OS

**Projekt:** natywny system operacyjny x86-64.  
**Aktualny etap:** v0.8.0 — Semaphores & IPC Foundations; **CI validation pending**.

## Historia statusu

- **v0.1.0 — CI PASS:** minimalny kernel, framebuffer i diagnostyka COM1.
- **v0.2.0 — CI PASS w skonfigurowanym QEMU:** Memory Core, IDT, wyjątek i PIT.
- **v0.3.0 — CI PASS w QEMU:** kooperatywny scheduler round-robin.
- **v0.4.0 — CI PASS w QEMU:** test przełączania kontekstu dwóch zadań Ring 0 przez IRQ0.
- **v0.5.0 — CI PASS w QEMU:** GDT/TSS/IST i cykl życia zadań.
- **v0.6.0 — CI PASS w QEMU:** blokowanie, wybudzanie, kolejki oczekujących i mutex.
- **v0.7.0 — CI PASS w QEMU:** sleep, timeouty i anulowanie zadań oczekujących ([udany przebieg](https://github.com/erykjuraszczyk-ui/Windows-Hybrid/actions/runs/40338105255)).

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

**Status: implementation committed on a feature branch; QEMU CI validation pending.**

- Fixed-capacity registry of up to 8 named kernel IPC services (names up to 15 characters, case-sensitive) with monotonic 32-bit service IDs.
- Service lookup and routed FIFO messaging. Messages carry source service ID and request/correlation ID in addition to type, length and payload.
- Unregistration is refused while messages, waiters or in-flight API calls remain; service IDs are not recycled.
- QEMU regression test checks registration/lookup, duplicate/invalid names, receiver wakeup, source/request metadata, queued-message protection and clean unregister.

See [HYBRID OS v0.9.0 guide](docs/BOOTSTRAP_v0.9.0.md). Required marker before PASS: `HYBRID: IPC_SERVICES_SELFTEST_OK` followed by `HYBRID: BOOT_OK`.

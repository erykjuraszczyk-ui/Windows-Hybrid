# Indeks dokumentacji HYBRID OS

## Dokumenty główne

- [BOOTSTRAP v0.1.0](BOOTSTRAP_v0.1.0.md) — kompilacja kernela, ISO i pierwszy test QEMU.
- [HYBRID OS v0.2.0](BOOTSTRAP_v0.2.0.md) — Memory Core, IDT, wyjątki i PIT.
- [HYBRID OS v0.3.0](BOOTSTRAP_v0.3.0.md) — kooperatywny Scheduler Core i test round-robin.
- [HYBRID OS v0.4.0](BOOTSTRAP_v0.4.0.md) — preemptive scheduler, ramki kontekstu i osobne stosy zadań.
- [HYBRID OS v0.5.0](BOOTSTRAP_v0.5.0.md) — Task Lifecycle, GDT/TSS i stosy IST.
- [HYBRID OS v0.6.0](BOOTSTRAP_v0.6.0.md) — blokowanie, wybudzanie i mutexy.
- [HYBRID OS v0.7.0](BOOTSTRAP_v0.7.0.md) — sleep, timeouty i anulowanie zadań.
- [HYBRID OS v0.8.0](BOOTSTRAP_v0.8.0.md) — semafory i podstawy IPC.
- [HYBRID OS v0.9.0](BOOTSTRAP_v0.9.0.md) — rejestr usług IPC i routing.
- [HYBRID OS v0.10.0](BOOTSTRAP_v0.10.0.md) — protokół request/response i service discovery.
- [HYBRID OS v0.11.0](BOOTSTRAP_v0.11.0.md) — runtime pending calls, timeouty i demultipleksowanie odpowiedzi.
- [HYBRID OS v0.23.0](BOOTSTRAP_v0.23.0.md) — telemetryka kolejek IPC, kompaktowanie i odzyskiwanie po backpressure.
- [HYBRID OS v0.18.0](BOOTSTRAP_v0.18.0.md) — bounded IPC event trace i runtime observability.
- [HYBRID OS v0.19.0](BOOTSTRAP_v0.19.0.md) — token-bucket rate limiting i admission control.
- [ROADMAP.md](ROADMAP.md) — wersje oraz zależności pomiędzy etapami.
- [ARCHITECTURE.md](ARCHITECTURE.md) — warstwy systemu i granice odpowiedzialności.
- [SECURITY_MODEL.md](SECURITY_MODEL.md) — capabilities, sandbox, tożsamość, polityki i rozruch.
- [TESTING.md](TESTING.md) — testy hostowe, QEMU, fuzzing i kryteria akceptacji.
- [RELEASE_PROCESS.md](RELEASE_PROCESS.md) — CI, buildy powtarzalne, artefakty, podpisy i aktualizacje.
- [IMPLEMENTATION_STATUS.md](IMPLEMENTATION_STATUS.md) — rozdzielenie projektu od zweryfikowanego kodu.
- [Zbiorcze specyfikacje v0.1.0–v3.0.0](versions/HYBRID_OS_v0.1.0-v3.0.0.md).

## Główne etapy

| Wersja | Temat |
|---|---|
| v0.1.0 | Minimalny kernel i boot smoke test |
| v0.2.0 | Alokator stron, IDT, wyjątki, PIT |
| v0.3.0 | Kooperatywny Scheduler Core, tablica zadań, round-robin |
| v0.4.0 | Preempcja w Ring 0, przełączanie ramek IRQ i stosy zadań |
| v0.5.0 | Cykl życia zadań, bezpieczne odzyskiwanie stosów, GDT/TSS/IST |
| v0.6.0 | Blocking/wakeup, kolejki oczekujących i mutexy |
| v0.7.0 | Timery, timeouty oczekiwania i anulowanie zadań |
| v0.8.0 | Semafory zliczające i kanał IPC o stałym rozmiarze |
| v0.9.0 | Nazwane usługi IPC, routing wiadomości i cykl życia endpointów |
| v0.10.0 | Wersjonowany request/response, statusy odpowiedzi i enumeracja usług |
| v0.11.0 | Tablica pending calls, timeout/anulowanie wywołań i routing odpowiedzi do konkretnego klienta |
| v0.3.0+ | Rozszerzanie pamięci, przerwań i usług kernela |
| v1.5.0 | Surface API i usługi pulpitu |
| v1.6.0 | Shared memory i double buffering |
| v1.7.0 | Frame Scheduler i planowanie prezentacji |
| v1.8.0 | Display Backend API i VirtIO-GPU 2D |
| v1.9.0 | Odzyskiwanie sterowników i bezpieczeństwo DMA |
| v2.0.0–v3.0.0 | Stabilizacja ABI, SDK, sandbox, pakiety, CI, Verified Boot |

Każda funkcja wymaga kodu, testów i wyników przed oznaczeniem jako gotowa.

| v0.22.0 | IPC Message Expiration, TTL i usuwanie przeterminowanych wiadomości (CI PASS) |
| v0.23.0 | IPC Queue Compaction, Capacity Telemetry i Backpressure Recovery (implementacja; CI pending) |

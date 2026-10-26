# Windows-Hybrid — HYBRID OS

**Projekt:** natywny system operacyjny x86-64.  
**Aktualny etap:** v0.4.0 — timer-driven Preemptive Scheduler, CI PASS w skonfigurowanym QEMU.

## Status

- **v0.1.0 — CI PASS:** minimalny kernel, framebuffer i diagnostyka COM1.
- **v0.2.0 — CI PASS w skonfigurowanym QEMU:** Memory Core, podstawowa IDT, test wyjątku i PIT.
- **v0.3.0 — CI PASS w QEMU:** kooperatywny planista round-robin typu step/callback.
- **v0.4.0 — CI PASS w QEMU:** ograniczony test wywłaszczania dwóch zadań Ring 0 przez IRQ0, każde z własnym stosem 4 KiB i pełną ramką kontekstu long mode.

## Co potwierdza v0.4.0

Przebieg CI [#40299062307](https://github.com/erykjuraszczyk-ui/Windows-Hybrid/actions/runs/40299062307) zakończył się sukcesem. Log zawiera:
- `HYBRID: TIMER_OK ticks=5`
- `HYBRID: SCHEDULER_SELFTEST_OK dispatches=11`
- `HYBRID: PREEMPTIVE_SELFTEST_OK switches=10` z dodatnimi licznikami obu zadań
- `HYBRID: BOOT_OK`

Test używa PIC/PIT w QEMU z `-cpu qemu64,-apic`. Pełna ramka kontekstu obejmuje rejestry, vector/error code, RIP/CS/RFLAGS oraz RSP/SS, a po 12 tyknięciach scheduler wraca do zapisanego kontekstu bootstrapu.

**Ograniczenia:** to eksperymentalny, jednordzeniowy prototyp wywłaszczania zadań jądra Ring 0. Nie ma przejść do Ring 3, TSS/IST, SMP, zapisu stanu FPU/SSE/AVX ani ogólnego API blokowania/wybudzania. CI PASS potwierdza ten test w QEMU, nie zgodność z fizycznym sprzętem ani gotowość produkcyjną.

## Uruchomienie

Na Ubuntu/Debian:

    sudo apt update
    sudo apt install -y build-essential binutils git curl xorriso qemu-system-x86
    make clean
    make test

Test wymaga markerów `TIMER_OK`, `SCHEDULER_SELFTEST_OK`, `PREEMPTIVE_SELFTEST_OK` i `BOOT_OK`.

## Dokumentacja

- [Indeks dokumentów](docs/INDEX.md)
- [Mapa drogowa](docs/ROADMAP.md)
- [Architektura](docs/ARCHITECTURE.md)
- [Model bezpieczeństwa](docs/SECURITY_MODEL.md)
- [Testowanie](docs/TESTING.md)
- [Proces wydań](docs/RELEASE_PROCESS.md)
- [Status implementacji](docs/IMPLEMENTATION_STATUS.md)
- [HYBRID OS v0.3.0 — Scheduler Core](docs/BOOTSTRAP_v0.3.0.md)
- [HYBRID OS v0.4.0 — Preemptive Scheduler](docs/BOOTSTRAP_v0.4.0.md)

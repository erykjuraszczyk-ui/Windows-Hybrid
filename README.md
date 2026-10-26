# Windows-Hybrid — HYBRID OS

**Projekt:** natywny system operacyjny x86-64.  
**Aktualny etap:** v0.3.0 — kooperatywny Scheduler Core, CI PASS w QEMU.

## Status

- **v0.1.0 — CI PASS:** minimalny kernel, framebuffer i diagnostyka COM1.
- **v0.2.0 — CI PASS w skonfigurowanym QEMU:** Memory Core, podstawowa IDT, test wyjątku i PIT.
- **v0.3.0 — CI PASS w QEMU:** kooperatywny planista round-robin oparty na krótkich krokach zadań.

Scheduler v0.3.0 zarządza stałą tablicą maksymalnie 8 zadań, ich stanami oraz przydziałem w cyklu round-robin. Samotest sprawdza kolejność wykonywania, ukończenie zadań, pustą kolejkę i odmowę przekroczenia pojemności.

**Ograniczenie:** to planista kooperatywny typu step/callback. Nie przełącza stosów ani rejestrów CPU i nie zapewnia jeszcze preempcji, procesów Ring 3 ani SMP. Nie jest to kompletny system operacyjny ani zamiennik Windows/Linux.

## Uruchomienie

Na Ubuntu/Debian:

    sudo apt update
    sudo apt install -y build-essential binutils git curl xorriso qemu-system-x86
    make clean
    make test

Test CI v0.3.0 potwierdził markery `TIMER_OK ticks=5`, `SCHEDULER_SELFTEST_OK dispatches=11` oraz `BOOT_OK`.

## Dokumentacja projektu

- [Indeks dokumentów](docs/INDEX.md)
- [Mapa drogowa](docs/ROADMAP.md)
- [Architektura](docs/ARCHITECTURE.md)
- [Model bezpieczeństwa](docs/SECURITY_MODEL.md)
- [Testowanie i kryteria akceptacji](docs/TESTING.md)
- [Proces kompilacji i wydań](docs/RELEASE_PROCESS.md)
- [Status implementacji](docs/IMPLEMENTATION_STATUS.md)
- [HYBRID OS v0.3.0 — Scheduler Core](docs/BOOTSTRAP_v0.3.0.md)
- [Zbiorcze specyfikacje v0.1.0–v3.0.0](docs/versions/HYBRID_OS_v0.1.0-v3.0.0.md)

# Windows-Hybrid — HYBRID OS

**Projekt:** natywny system operacyjny x86-64.  
**Aktualny etap:** v0.4.0 — timer-driven preemptive Scheduler Core, weryfikacja przez QEMU.

## Status

- **v0.1.0 — CI PASS:** minimalny kernel, framebuffer i diagnostyka COM1.
- **v0.2.0 — CI PASS w skonfigurowanym QEMU:** Memory Core, podstawowa IDT, test wyjątku i PIT.
- **v0.3.0 — CI PASS w QEMU:** kooperatywny planista round-robin typu step/callback.
- **v0.4.0 — w trakcie walidacji:** przełączanie zapisanych ramek przerwań między dwoma zadaniami kernela wywoływane przez IRQ0; każde zadanie ma własną stronę stosu.

## Co testuje v0.4.0

Test tworzy dwa zadania Ring 0 z oddzielnymi stosami 4 KiB, buduje początkowe ramki zgodne z ISR, a następnie pozwala timerowi PIT przełączać wykonanie między nimi przez ograniczone 12 tyknięć. Po zakończeniu test wraca na zapisany stos bootstrapu, sprawdza progres obu zadań oraz minimalną liczbę przełączeń i zwalnia przydzielone strony.

**Ograniczenia:** to eksperymentalny, jednowątkowy prototyp wywłaszczania zadań jądra na x86-64. Nie ma przejść do Ring 3, TSS/IST, SMP, zapisu stanu FPU/SSE/AVX ani ogólnego API blokowania/wybudzania. Nie jest to kompletny system operacyjny ani zamiennik Windows/Linux. Do czasu przejścia CI v0.4.0 nie jest oznaczona jako zweryfikowana.

## Uruchomienie

Na Ubuntu/Debian:

    sudo apt update
    sudo apt install -y build-essential binutils git curl xorriso qemu-system-x86
    make clean
    make test

Log poprawnego testu powinien zawierać `TIMER_OK`, `SCHEDULER_SELFTEST_OK`, `PREEMPTIVE_SELFTEST_OK` oraz `BOOT_OK`.

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

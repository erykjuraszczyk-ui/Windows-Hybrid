# Status implementacji i ograniczenia

## v0.1.0–v0.3.0

- v0.1.0: CI potwierdziło kompilację ELF64, obraz ISO i minimalny rozruch QEMU.
- v0.2.0: Memory Core, podstawowa IDT, test int3 i PIT przeszły testy CI w konfiguracji QEMU z `-cpu qemu64,-apic`.
- v0.3.0: kooperatywny Scheduler Core przeszedł test round-robin; CI potwierdziło `TIMER_OK`, `SCHEDULER_SELFTEST_OK` i `BOOT_OK`.

## HYBRID OS v0.4.0 — Preemptive Scheduler

**Status: oczekuje na CI.**

Gałąź `feat/hybrid-os-v0.4.0-preemptive` dodaje:
- początkową ramkę CPU zgodną z układem rejestrów zapisanym przez ISR;
- dwa zadania kernel-mode z niezależnymi stosami przydzielonymi przez Memory Core;
- przełączenie RSP do ramki zwracanej przez dispatcher IRQ0;
- bounded preemption test przez 12 tyknięć PIT, powrót do zapisanego stosu bootstrapu i sprawdzenie progresu obu zadań;
- zwolnienie stron stosów po wyłączeniu preempcji;
- wymagany marker `HYBRID: PREEMPTIVE_SELFTEST_OK`.

## Ograniczenia i ryzyka

- Scheduler jest jednordzeniowy, działa wyłącznie w Ring 0 i jest włączany tylko na czas ograniczonego testu.
- To nie jest pełny scheduler produkcyjny: brak Ring 3, TSS/IST, SMP, FPU/SSE/AVX save/restore, blokowania/wybudzania, idle task i pełnego lifecycle API.
- Zmiana wymaga dokładnej zgodności ramki C ze stosem zapisanym przez `interrupt_stubs.S`; test QEMU ma odrzucić implementację, jeżeli wykonanie obu zadań nie zostanie potwierdzone.
- Sukces QEMU nie potwierdza poprawności na fizycznym sprzęcie.

Funkcje poza zakresem zaliczonych testów pozostają SPECIFIED / NOT VERIFIED.

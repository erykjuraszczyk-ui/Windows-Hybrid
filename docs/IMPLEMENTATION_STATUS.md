# Status implementacji i ograniczenia

## v0.1.0–v0.3.0

- v0.1.0: CI potwierdziło kompilację ELF64, obraz ISO i minimalny rozruch QEMU.
- v0.2.0: Memory Core, podstawowa IDT, test int3 i PIT przeszły testy CI w konfiguracji QEMU z `-cpu qemu64,-apic`.
- v0.3.0: kooperatywny Scheduler Core przeszedł test round-robin; CI potwierdziło `TIMER_OK`, `SCHEDULER_SELFTEST_OK` i `BOOT_OK`.

## HYBRID OS v0.4.0 — Preemptive Scheduler

**CI PASS w skonfigurowanym QEMU.**

Potwierdzający przebieg:
https://github.com/erykjuraszczyk-ui/Windows-Hybrid/actions/runs/40299062307

Log końcowy:
- `HYBRID: TIMER_OK ticks=0x0000000000000005`
- `HYBRID: SCHEDULER_SELFTEST_OK dispatches=0x000000000000000B`
- `HYBRID: PREEMPTIVE_SELFTEST_OK switches=0x000000000000000A` z dodatnim postępem `task_a` oraz `task_b`
- `HYBRID: BOOT_OK`

Implementacja:
- pełna ramka kontekstu long mode z 15 rejestrami ogólnymi, vector/error code i RIP/CS/RFLAGS/RSP/SS;
- dwa zadania kernel-mode z niezależnymi stosami 4 KiB przydzielonymi przez Memory Core;
- przełączenie RSP na ramkę wybraną przez dispatcher IRQ0;
- ograniczony test 12 tyknięć PIT, sprawdzenie progresu obu zadań i powrót do zapisanego stosu bootstrapu;
- zwolnienie stron stosów po zatrzymaniu testu.

Wynik dotyczy określonej konfiguracji QEMU z `-cpu qemu64,-apic`. Nie stanowi certyfikacji pracy na fizycznym sprzęcie.

## Ograniczenia / ryzyka

- Scheduler jest jednordzeniowy, działa wyłącznie w Ring 0 i jest włączany tylko na czas ograniczonego testu.
- To nie jest pełny scheduler produkcyjny: brak Ring 3, TSS/IST, SMP, FPU/SSE/AVX save/restore, blokowania/wybudzania, idle task i pełnego lifecycle API.
- Testy QEMU nie dowodzą poprawności na fizycznym sprzęcie.

Funkcje poza zakresem zaliczonych testów pozostają SPECIFIED / NOT VERIFIED.

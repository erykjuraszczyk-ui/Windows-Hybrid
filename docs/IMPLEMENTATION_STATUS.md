# Status implementacji i ograniczenia

## v0.1.0–v0.5.0

- v0.1.0: minimalny kernel i QEMU boot smoke test.
- v0.2.0: Memory Core, IDT, test int3 i PIT.
- v0.3.0: kooperatywny round-robin Scheduler Core.
- v0.4.0: ograniczony preemptive context-switch test w QEMU.
- v0.5.0: GDT/TSS/IST, task lifecycle, bezpieczne odzyskiwanie stosów; CI PASS w QEMU.

## HYBRID OS v0.6.0 — Task Blocking, Wakeup & Synchronization Core

**CI PASS w skonfigurowanym QEMU.** Potwierdzający przebieg: https://github.com/erykjuraszczyk-ui/Windows-Hybrid/actions/runs/40311257137.

Dodano:
- stan `SCHEDULER_TASK_BLOCKED` i wynik kroku `SCHEDULER_STEP_BLOCK`;
- `scheduler_task_wake` i licznik zablokowanych zadań;
- kolejki oczekiwania FIFO, `wake_one` i `wake_all`;
- nie-rekurencyjny mutex z kontrolą właściciela i przekazaniem własności oczekującemu;
- zachowanie poprzedniego IF w sekcjach krytycznych i wykonanie callbacków poza sekcją krytyczną;
- test dwóch zadań blokowanych na kolejce, wybudzania, rywalizacji o mutex, handoff i odzyskiwania zasobów.

Wymagane markery CI: `TSS_OK`, `IDT_OK`, `IST_OK`, `TASK_LIFECYCLE_SELFTEST_OK`, `SYNC_SELFTEST_OK`, `PREEMPTIVE_SELFTEST_OK` i `BOOT_OK`.

## Ograniczenia

- Blocking/wakeup i mutex są aktualnie dostępne dla kooperatywnych funkcji `step`; nie wstrzymują samodzielnie dowolnej funkcji preemptive `entry`.
- Mechanizmy `cli` są poprawne dla prototypu jednordzeniowego, ale nie są blokadami SMP.
- Brak Ring 3, SMP, zapisu i odtwarzania FPU/SSE/AVX, blokad z dziedziczeniem priorytetów oraz pełnego schedulera produkcyjnego.
- TSS/IST sprawdzane jest strukturalnie; wymuszone testy fault-injection #DF/#PF pozostają poza zakresem.

Status v0.6.0 dotyczy źródła testowanego na commicie `a449e6e7c6d077ddef0d19f4f23685aa0aa8ad7a`. Dokumentacja została zaktualizowana po tym przebiegu; PR uruchamia ponowne CI dla aktualnego nagłówka.

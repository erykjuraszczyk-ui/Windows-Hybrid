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

**Status: implementation committed on a feature branch; CI validation pending.**

- `scheduler_sleep_ticks(ticks)`: block the current preemptive task until a relative PIT tick deadline expires.
- `scheduler_wait_queue_wait_timeout(queue, timeout_ticks)`: wait for a queue wakeup or return false after its deadline.
- Tick-expiry processing runs from the PIT-driven scheduler path and removes the task from its wait queue before marking it READY.
- `scheduler_task_cancel(id)` cancels READY/BLOCKED tasks only, unlinks pending waiters, clears deadlines and marks the task CANCELLED. Running tasks are deliberately not force-terminated.
- Reaping accepts CANCELLED tasks only after the scheduler has stopped and the task is no longer linked to a wait queue.
- Regression test covers timeout after 4 ticks, sleep completion after 6 ticks and cancellation of an indefinitely blocked task.

Required QEMU markers: `HYBRID: TIMER_TIMEOUT_CANCEL_SELFTEST_OK` and `HYBRID: BOOT_OK`. The feature is not considered validated until the latest CI run passes. Current implementation remains single-core Ring 0 and is not SMP-safe.

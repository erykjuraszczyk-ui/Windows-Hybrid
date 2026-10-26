# Status implementacji i ograniczenia

## v0.1.0–v0.4.0

- v0.1.0: minimalny kernel i QEMU boot smoke test.
- v0.2.0: Memory Core, IDT, test int3 i PIT w skonfigurowanym QEMU.
- v0.3.0: kooperatywny round-robin Scheduler Core.
- v0.4.0: ograniczony preemptive context-switch test w QEMU; log potwierdza `PREEMPTIVE_SELFTEST_OK` i `BOOT_OK`.

## HYBRID OS v0.5.0 — Task Lifecycle, TSS/IST

**Status: oczekuje na wynik CI.**

Implementacja na gałęzi `feat/hybrid-os-v0.5.0-task-lifecycle-tss-ist` dodaje:
- własną GDT z kodem/danymi Ring 0 oraz deskryptorem TSS;
- TSS.RSP0 i pięć stron stosów: jeden stos RSP0 plus cztery IST;
- IST1 dla #DF, IST2 dla #GP, IST3 dla #PF i IST4 dla NMI;
- sprawdzenie selektora TR, GDT i przypisań IST w załadowanej IDT;
- API `scheduler_task_create` i `scheduler_task_reap`;
- stany EXITED/FAILED i ochronę przed zwolnieniem stosu aktywnego zadania;
- monotoniczne identyfikatory, czyszczenie slotu po reaping i możliwość ponownego użycia slotu;
- skończone zadania testowe, które kończą się naturalnie, po czym ich stosy są zwalniane;
- markery CI `TSS_OK`, `IST_OK`, `TASK_LIFECYCLE_SELFTEST_OK`, `PREEMPTIVE_SELFTEST_OK` i `BOOT_OK`.

Weryfikacja IST obejmuje konfigurację GDT/TSS/IDT oraz kontrolę indeksów IST. Nie wykonuje jeszcze wymuszonego double fault/page fault jako testu fault-injection.

## Ograniczenia

- Jednordzeniowy kernel, zadania Ring 0. TSS/IST nie jest izolacją bezpieczeństwa procesów.
- Brak Ring 3, syscall ABI, SMP, FPU/SSE/AVX context-save/restore, blokowania/wybudzania i ogólnego systemu synchronizacji.
- Wyniki QEMU potwierdzają wyłącznie testowaną konfigurację emulatora, nie fizyczny sprzęt.
- Wydanie v0.5.0 nie może być oznaczone CI PASS przed zaliczeniem aktualnego przebiegu QEMU.

Funkcje poza zakresem zaliczonych testów pozostają SPECIFIED / NOT VERIFIED.

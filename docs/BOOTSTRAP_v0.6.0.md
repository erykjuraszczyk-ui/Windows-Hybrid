# HYBRID OS v0.6.0 — Task Blocking, Wakeup & Synchronization Core

## Zakres

v0.6.0 rozszerza kooperatywny scheduler o blokowanie i wybudzanie oraz wprowadza prymityw synchronizacji z kolejką oczekujących. Zachowuje istniejący preemptive smoke test v0.4/v0.5 bez rozszerzania blokowania na dowolne funkcje preemptive.

## Stany i API

- `SCHEDULER_TASK_BLOCKED` oznacza zadanie, które nie może zostać wybrane przez `scheduler_run_once`.
- Callback, który nie może kontynuować, powinien zapisać się przez `scheduler_wait_queue_wait_current(queue)` i natychmiast zwrócić `SCHEDULER_STEP_BLOCK`.
- `scheduler_task_wake(id)` przełącza wyłącznie zadanie BLOCKED do READY.
- `scheduler_wait_queue_wake_one(queue)` budzi pierwszego poprawnego oczekującego; `scheduler_wait_queue_wake_all(queue)` budzi wszystkich.
- Identyfikatory są monotoniczne; stare identyfikatory nie wskazują ponownie użytych slotów.

## Mutex

- `scheduler_mutex_init`, `scheduler_mutex_lock` oraz `scheduler_mutex_unlock`.
- Mutex jest nie-rekurencyjny i odrzuca odblokowanie przez nie-właściciela.
- `scheduler_mutex_lock` zwraca `SCHEDULER_SYNC_ACQUIRED`, `SCHEDULER_SYNC_WAIT` lub `SCHEDULER_SYNC_ERROR`.
- Jeśli lock zwraca WAIT, callback musi natychmiast zwrócić `SCHEDULER_STEP_BLOCK`.
- `scheduler_mutex_unlock` przekazuje własność następnemu zablokowanemu oczekującemu. Obudzony task kończy protokół handoff przez kolejne wywołanie `scheduler_mutex_lock`.

## Krytyczne sekcje

Scheduler zachowuje pierwotny stan IF; przywraca przerwania tylko wtedy, gdy były włączone przed sekcją krytyczną. Callback taska wykonuje się poza sekcją krytyczną. To nie jest blokada SMP: kernel nadal jest jednordzeniowym prototypem.

## Testy

Uruchomienie:

    make clean
    make test

Wymagane markery:
- `HYBRID: TSS_OK`
- `HYBRID: IDT_OK`
- `HYBRID: IST_OK`
- `HYBRID: TASK_LIFECYCLE_SELFTEST_OK`
- `HYBRID: SYNC_SELFTEST_OK`
- `HYBRID: PREEMPTIVE_SELFTEST_OK`
- `HYBRID: BOOT_OK`

Test sync sprawdza dwie zablokowane funkcje step, wake-all, mutex contention, handoff, końcowe stany zadań i odzyskiwanie stron.

## Ograniczenia

Brak blokowania dowolnych preemptive entry, SMP, Ring 3, FPU/SSE/AVX context-save/restore i mechanizmów dziedziczenia priorytetów. Testy QEMU nie potwierdzają działania na fizycznym sprzęcie.

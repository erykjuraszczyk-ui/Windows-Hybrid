# HYBRID OS v0.7.0 — Timers, Timeouts & Cancellation Core

## Cel

Rozszerzyć mechanizmy v0.6.0 o taktowanie deadline’ów, sleep dla zadań, timeouty oczekiwania oraz bezpieczne anulowanie zadań oczekujących.

## API

```c
bool scheduler_sleep_ticks(uint64_t ticks);
bool scheduler_wait_queue_wait_timeout(scheduler_wait_queue_t *queue, uint64_t timeout_ticks);
bool scheduler_task_cancel(scheduler_task_id_t task_id);
uint64_t scheduler_timer_expiration_count(void);
bool scheduler_timer_timeout_cancel_self_test(void);
```

## Semantyka

- Tick źródłowy to PIT 100 Hz, czyli nominalnie około 10 ms na tick w obecnej konfiguracji QEMU.
- `scheduler_sleep_ticks(0)` kończy się natychmiast. Niezerowy sleep zwraca true, kiedy deadline upłynie, i blokuje wywołujące zadanie preemptive.
- `scheduler_wait_queue_wait_timeout(queue, 0)` zwraca false natychmiast. Dodatni timeout zwraca true wyłącznie po sygnale z kolejki; upływ terminu zwraca false.
- Deadline jest względny względem `interrupts_ticks()` i porównywany z uwzględnieniem przepełnienia licznika dla okresów krótszych niż połowa zakresu `uint64_t`.
- Anulowanie jest dozwolone tylko dla zadań READY lub BLOCKED. Usuwa je z wait queue, usuwa deadline i oznacza stan CANCELLED. Nie wolno siłowo zabijać zadania RUNNING z dowolnego kontekstu.
- Zadanie CANCELLED można odzyskać przez reaping dopiero wtedy, gdy scheduler zakończył przełączanie kontekstu, a zadanie nie jest już związane z kolejką oczekujących.
- W przypadku wybudzenia i timeoutu w tym samym ticku pierwszeństwo ma sekwencja obsługi przerwania: zdarzenia są serializowane przez wyłączenie przerwań; pierwszy poprawny wynik podjęty przez scheduler wygrywa.

## Testy i kryteria akceptacji

Uruchom:

```sh
make clean
make test
```

Wymagane markery:
- `HYBRID: BLOCKING_SYNC_SELFTEST_OK`
- `HYBRID: TIMER_TIMEOUT_CANCEL_SELFTEST_OK`
- `HYBRID: BOOT_OK`

Test powinien potwierdzić timeout kolejki po co najmniej 4 tickach, zakończenie sleep po co najmniej 6 tickach, anulowanie zablokowanego zadania bez jego wznowienia, brak oczekujących bitów w obu kolejkach i brak zadań FAILED.

## Ograniczenia

- Jednordzeniowy scheduler, zadania Ring 0. Mechanizm nie jest SMP-safe.
- Tick PIT ma rozdzielczość nominalnie 10 ms; nie jest to zegar czasu rzeczywistego ani deadline scheduler.
- Brak anulowania działającego kodu RUNNING, timeoutu mutex API, priorytetowego dziedziczenia, semaforów, IPC, SMP i izolacji Ring 3.
- QEMU weryfikuje tylko skonfigurowane środowisko emulatora.
- Wydanie nie może być oznaczone PASS przed potwierdzeniem wszystkich wymaganych markerów przez CI.

## Status

Implementacja: `feat/hybrid-os-v0.7.0-timers-timeouts-cancel`. Walidacja CI oczekuje na wynik.
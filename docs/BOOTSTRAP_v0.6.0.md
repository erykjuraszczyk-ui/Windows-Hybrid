# HYBRID OS v0.6.0 — Task Blocking, Wakeup & Synchronization Core

## Cel

Dodać kontrolowane blokowanie i wybudzanie zadań oraz podstawowe mechanizmy synchronizacji dla istniejącego, jednordzeniowego kernela x86-64 Ring 0.

## Zakres implementacji

- Nowy stan `SCHEDULER_TASK_BLOCKED`.
- `scheduler_task_block()` i `scheduler_task_wake()` dla gotowych zadań kooperacyjnego schedulera.
- `scheduler_wait_queue_t` z operacjami inicjalizacji, oczekiwania, wybudzania jednego zadania i wybudzania wszystkich oczekujących.
- `scheduler_mutex_t`: mutex nierekurencyjny z kontrolą właściciela i kolejką oczekujących.
- Przełącznik PIT nie może automatycznie zmienić stanu BLOCKED na READY.
- Reaping nie może zwolnić zadania, które nadal należy do kolejki oczekujących.
- Test QEMU sprawdzający wait/wakeup oraz współzawodnictwo o mutex.

## API

```c
bool scheduler_task_block(scheduler_task_id_t task_id);
bool scheduler_task_wake(scheduler_task_id_t task_id);
void scheduler_wait_queue_init(scheduler_wait_queue_t *queue);
bool scheduler_wait_queue_wait(scheduler_wait_queue_t *queue);
bool scheduler_wait_queue_wake_one(scheduler_wait_queue_t *queue);
uint32_t scheduler_wait_queue_wake_all(scheduler_wait_queue_t *queue);
void scheduler_mutex_init(scheduler_mutex_t *mutex);
bool scheduler_mutex_lock(scheduler_mutex_t *mutex);
bool scheduler_mutex_unlock(scheduler_mutex_t *mutex);
```

`wait_queue_wait` i operacje mutex są przeznaczone do wywoływania z działającego zadania preemptive. Funkcje zwracają `false` dla nieprawidłowego kontekstu lub niepoprawnego przejścia stanu. Mutex nie jest rekurencyjny; tylko właściciel może go zwolnić.

## Testy i kryteria akceptacji

Uruchom:

```sh
make clean
make test
```

Wymagane markery:

- `HYBRID: TASK_LIFECYCLE_SELFTEST_OK`
- `HYBRID: PREEMPTIVE_SELFTEST_OK`
- `HYBRID: BLOCKING_SYNC_SELFTEST_OK`
- `HYBRID: BOOT_OK`

Test blokowania/synchronizacji musi potwierdzić, że oczekujące zadanie nie jest uruchamiane przed wybudzeniem, wake-one budzi właściwe oczekiwanie, oba zadania kończą pracę, mutex zostaje zwolniony, licznik sekcji krytycznej ma oczekiwaną wartość, a kolejki nie zawierają osieroconych waiterów.

## Ograniczenia

- Jedno CPU, Ring 0; brak SMP i bezpieczeństwa między procesami.
- Brak timeoutów, anulowania, priorytetowego dziedziczenia i sprawiedliwości gwarantowanej przez scheduler.
- API kolejki oczekujących nie zastępuje pętli sprawdzającej warunek po wybudzeniu.
- Test QEMU weryfikuje wyłącznie skonfigurowany emulator.
- Wydanie nie może być oznaczone PASS przed zakończeniem CI na tej gałęzi.

## Status

Implementacja umieszczona na gałęzi `feat/hybrid-os-v0.6.0-blocking-sync`. CI i test runtime pozostają wymaganymi kryteriami przed scaleniem.
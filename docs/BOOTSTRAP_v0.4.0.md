# HYBRID OS v0.4.0 — Preemptive Scheduler

## Cel

Dodać eksperymentalne przełączanie wykonania między zadaniami jądra x86-64 inicjowane przerwaniem zegara. Wersja wykorzystuje PIC/PIT i wspólny układ ramki rejestrowej z procedurami ISR.

## Implementacja

- `interrupt_dispatch` zwraca wskaźnik do ramki, z której assembly ma odtworzyć rejestry.
- `interrupt_stubs.S` przełącza RSP na ramkę zwróconą przez dispatcher przed odtworzeniem rejestrów i `iretq`.
- Scheduler tworzy początkowe ramki zgodne z układem 15 rejestrów ogólnych + vector/error code + RIP/CS/RFLAGS.
- Każde zadanie testowe otrzymuje osobną stronę 4 KiB ze stosu przez Memory Core.
- IRQ0 zapisuje ramkę przerwanego zadania i wybiera kolejne READY zadanie w round-robin.
- Ograniczony test przełącza przez 12 tyknięć PIT, wraca do stosu bootstrapu i weryfikuje progres obu zadań oraz liczbę przełączeń.
- Po zatrzymaniu testu wyłącza preempcję i zwalnia strony stosów.
- CI wymaga markera `HYBRID: PREEMPTIVE_SELFTEST_OK` przed `BOOT_OK`.

## Testowanie

    make clean
    make test

Wymagane znaczniki:
- `HYBRID: TIMER_OK`
- `HYBRID: SCHEDULER_SELFTEST_OK`
- `HYBRID: PREEMPTIVE_SELFTEST_OK`
- `HYBRID: BOOT_OK`

## Ograniczenia

To testowy prototyp jednordzeniowy, wyłącznie Ring 0. Nie implementuje przejść Ring 3, TSS/IST, SMP, FPU/SSE/AVX state save/restore, pełnego API task lifecycle, blokowania/wybudzania ani bezpiecznego scheduler tick dla dowolnego kodu produkcyjnego. Brak wyniku CI oznacza brak potwierdzenia działania.

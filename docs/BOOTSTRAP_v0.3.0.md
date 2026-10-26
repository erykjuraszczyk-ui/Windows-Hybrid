# HYBRID OS v0.3.0 — Scheduler Core

## Cel

Dodać kooperatywne planowanie zadań do kernela v0.2.0. Wersja demonstruje kontrakt zadania, stany zadań, sprawiedliwy przydział w cyklu round-robin i obsługę wyczerpania limitu zasobów.

## Implementowane elementy

- Statyczna tablica o pojemności 8 zadań; brak dynamicznej alokacji pamięci.
- Identyfikator zadania, wskaźnik callbacku i kontekst kernela.
- Stany: UNUSED, READY, RUNNING, EXITED, FAILED.
- `scheduler_run_once()` wybiera następne READY od kursora round-robin i uruchamia dokładnie jeden ograniczony krok.
- Callback zwraca YIELD, aby pozostać READY, lub COMPLETE, aby zakończyć zadanie. Nieznany wynik oznacza FAILED.
- Planista odrzuca ponowne wejście oraz dodawanie zadań w trakcie callbacku.
- Test QEMU sprawdza kolejność A, B, C, A, B, B, liczniki, ukończenie zadań i odmowę przydziału dziewiątego zadania.

## Budowanie i testowanie

Na Ubuntu/Debian:

    sudo apt update
    sudo apt install -y build-essential binutils git curl xorriso qemu-system-x86
    make clean
    make test

Poprawny log powinien zawierać:
- `HYBRID: TIMER_OK`
- `HYBRID: SCHEDULER_SELFTEST_OK`
- `HYBRID: BOOT_OK`

## Istotne ograniczenie architektoniczne

**v0.3.0 nie przełącza stosów ani rejestrów CPU.** To kooperatywny scheduler typu step/callback: każde zadanie oddaje sterowanie po jednym ograniczonym kroku. Brak preempcji, stosów zadań, przełączania kontekstu assemblerem, procesów Ring 3 i SMP. Te funkcje będą osobnym etapem po dodaniu właściwego kontraktu kontekstu CPU i bezpiecznego stosu dla każdego zadania.

# HYBRID OS v0.28.0 — IPC Multi-Queue Load Balancing, Service Capacity Modeling & Adaptive Worker Dispatch

## Cel
Wprowadzić jawny, ograniczony model pojemności wykonawczej usług IPC oraz atomowy przydział slotów dispatch. Mechanizm równoważy kandydatów na podstawie znormalizowanego obciążenia kolejki i zajętych slotów, a następnie rezerwuje slot w tej samej sekcji krytycznej.

## Zakres
- Konfigurowalne minimum i maksimum slotów dispatch per usługa (zakres 1..4).
- Bieżąca pojemność adaptacyjna, liczba aktywnych rezerwacji i telemetryka grantów/odrzuceń/zwolnień.
- Adaptacja pojemności na podstawie presji kolejki: zwiększanie przy wysokim obciążeniu, zmniejszanie przy niskim; limity min/max i histereza.
- Selekcja znormalizowanego obciążenia, deterministyczny tie-breaker i pomijanie usług zamkniętych, przeciążonych, z otwartym circuit breakerem lub bez wolnych slotów.
- Atomowe acquire/release z walidacją identyfikatora i ochroną przed underflow/overflow.
- QEMU test: rozkład przydziałów, limity slotów, skalowanie w górę/w dół, pomijanie niedostępnych usług i pełna telemetria.

## Kontrakt
To logiczne sloty dispatch dla usług IPC. Nie tworzą wątków ani zadań CPU; rzeczywisty worker runtime pozostaje osobnym etapem. Rezerwacja musi zostać zwolniona przez właściciela po zakończeniu pracy. Brak dynamicznej alokacji; stałe tablice; single-core Ring 0.

## Bezpieczeństwo i deterministyczność
- Pojemność min/max ograniczona do 1..4.
- Wszystkie mutacje i rezerwacje chronione przez IRQ save/restore.
- Selektor pomija usługi z otwartym circuit breakerem, przeciążone, zamknięte, z wyczerpanym predictive budget lub pełną pulą slotów.
- Wynik jest deterministyczny dla tego samego stanu wejściowego; tie-breaker jest rotacyjny.
- Liczniki są monotoniczne i saturujące tam, gdzie przyrost może przekroczyć zakres.

## Kryteria akceptacji
- Konfiguracja odrzuca zero, min>max i max>4.
- Nie można zarezerwować ponad effective capacity ani zwolnić pustej rezerwacji.
- Wzrost i spadek pojemności respektują min/max.
- Selektor preferuje mniejsze znormalizowane obciążenie i rotuje remisy.
- QEMU marker: `IPC_CAPACITY_DISPATCH_SELFTEST_OK`; końcowy `BOOT_OK`.

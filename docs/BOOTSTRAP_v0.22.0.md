# HYBRID OS v0.22.0 — IPC Message Expiration, Stale-Queue Pruning & TTL Policy

## Cel etapu

Rozszerzyć IPC v0.21.0 o osobny czas życia wiadomości (TTL) i deterministyczne usuwanie wiadomości, których termin ważności minął, zanim odbiorca je pobrał. Nie utożsamiać TTL z deadline'em QoS: deadline służy do planowania kolejności, TTL określa, czy wiadomość nadal może zostać dostarczona.

## Kontrakt projektowy

- Dodać do `scheduler_ipc_message_t` pole `expiry_tick`; wartość zero oznacza brak wygaśnięcia i zachowuje zgodność dotychczasowych nadawców.
- Dodać API konfiguracji domyślnego TTL per usługa źródłowa. Jawne `expiry_tick` wiadomości ma pierwszeństwo przed wartością domyślną.
- Obliczać terminy na bazie monotonicznych ticków PIT, z kontrolą przepełnienia arytmetyki.
- Przed wyborem wiadomości w `scheduler_ipc_service_receive()` usuwać wygasłe wpisy z ograniczonej kolejki, zachowując spójność head/tail/count i metadanych fair scheduling.
- Wiadomość wygasła nie może być zwrócona odbiorcy ani potwierdzona ACK jako dostarczona.
- Dla śledzonej dostawy powiązanej z wygasłą wiadomością zakończyć receipt stanem terminalnym i zarejestrować powód w Dead-Letter Queue; nie wykonywać kolejnej próby po wygaśnięciu.
- Rozszerzyć trace i diagnostykę o liczbę wygasłych wiadomości, usunięć z kolejki i dostaw zakończonych TTL.
- Zachować dotychczasowe API oraz zachowanie wiadomości bez TTL.

## Reguły bezpieczeństwa i ograniczenia

- Wszystkie operacje na kolejce pozostają ograniczone czasowo i bez dynamicznej alokacji pamięci.
- Mechanizm jest przeznaczony dla obecnego jednordzeniowego kernela Ring 0; nie deklaruje SMP safety.
- Tick PIT nie jest zegarem czasu rzeczywistego. TTL jest budżetem w tickach kernela, nie gwarancją czasu ściennego.
- TTL nie daje exactly-once ani trwałości po restarcie.
- Usunięcie wygasłej wiadomości musi zwalniać miejsce w kolejce i aktualizować liczniki drop/expiry bez fałszywego zwiększania liczby odebranych wiadomości.

## Plan implementacji

1. Rozszerzyć typy i API w `kernel/src/scheduler.h`.
2. Dodać walidację i ustawianie TTL w ścieżkach wysyłania w `kernel/src/scheduler.c`.
3. Dodać bounded stale-message pruning przed selekcją deadline/QoS/fairness.
4. Zintegrować TTL z receipt, retry, DLQ i trace bez podwójnego terminalizowania dostawy.
5. Dodać self-testy: TTL wyłączony, TTL domyślny, TTL jawny, wygaśnięcie przed odbiorem, zachowanie kolejności pozostałych wiadomości, odzyskanie miejsca, brak ACK dla wygasłej dostawy, DLQ/trace/diagnostyka.
6. Dodać marker `HYBRID: IPC_EXPIRATION_SELFTEST_OK` do kernela i smoke testu.
7. Zaktualizować README, roadmapę, status implementacji i workflow CI.
8. Uruchomić `make test`; status PASS nadać wyłącznie po potwierdzeniu markera i `HYBRID: BOOT_OK` w QEMU.

## Kryteria akceptacji

- Stare wiadomości bez TTL zachowują dotychczasowe zachowanie.
- Wiadomości po `expiry_tick` nigdy nie są zwracane przez receive.
- Usunięcie wpisu nie uszkadza FIFO per źródło, fair scheduling, deadline ordering ani QoS.
- Wygasła dostawa nie jest ponawiana i ma jednoznaczny terminalny receipt/DLQ.
- Wszystkie dotychczasowe markery regresji v0.1.0–v0.21.0 pozostają PASS.

# HYBRID OS v0.13.0 — IPC Backpressure, Queue Policies & Delivery Guarantees

## Cel

Dodać kontrolowane zachowanie przy pełnych kolejkach i oddzielić przyjęcie wiadomości od jej faktycznego przetworzenia.

## Polityki kolejek

| Polityka | Zachowanie przy pełnej kolejce |
|---|---|
| `BLOCK` | Dotychczasowe API `scheduler_ipc_service_send()` czeka na wolne miejsce; nieblokujące API `send_ex` zwraca `BACKPRESSURE`. |
| `REJECT` | Nie przyjmuje nowej wiadomości; zwraca `BACKPRESSURE`. |
| `DROP_NEWEST` | Odrzuca nową wiadomość i zwraca `DROPPED_NEWEST`. |
| `DROP_OLDEST` | Usuwa najstarszą wiadomość, przyjmuje nową i zwraca `DELIVERED_DROPPED_OLDEST`. |

Domyślna polityka pozostaje `BLOCK` dla zachowania kompatybilności.

## API

- `scheduler_ipc_service_set_queue_policy(id, policy)` ustawia politykę dla usługi.
- `scheduler_ipc_service_send_ex(source, destination, request_id, message)` wykonuje nieblokującą próbę dostarczenia.
- `scheduler_ipc_service_send_request_ex(...)` zapewnia jawny wynik dla request/response.
- `scheduler_ipc_service_get_info()` zawiera politykę, głębokość i high-water mark kolejki oraz liczniki dostarczonych, odebranych, odrzuconych i utraconych wiadomości.
- `scheduler_ipc_get_diagnostics()` udostępnia globalne liczniki backpressure, drops i maksymalnej głębokości kolejki.

## Gwarancje

`DELIVERED` oznacza atomowe przyjęcie wiadomości do ograniczonej kolejki. Nie oznacza potwierdzenia od odbiorcy, przetworzenia ani trwałości po restarcie. `DELIVERED_DROPPED_OLDEST` oznacza, że nowa wiadomość zajęła miejsce poprzedniej najstarszej. Odrzucenie/utrata są jednoznacznie widoczne przez enum wyniku i liczniki diagnostyczne.

## Walidacja QEMU

```sh
make clean
make test
```

Wymagane markery:
- `HYBRID: IPC_BACKPRESSURE_SELFTEST_OK`
- `HYBRID: IPC_DIAGNOSTICS_OK`
- `HYBRID: BOOT_OK`

Test zapełnia kolejkę o pojemności czterech wiadomości i dla każdej polityki sprawdza wynik oraz sekwencję identyfikatorów przy odbiorze.

## Granice

Jednordzeniowy prototyp x86-64 Ring 0. Pojemność kanału to nadal cztery wiadomości; brak SMP, persistent delivery, ACK/retransmisji i gwarancji exactly-once.

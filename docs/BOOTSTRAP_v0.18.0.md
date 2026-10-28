# HYBRID OS v0.18.0 — IPC Token-Bucket Rate Limiting & Admission Control

## Cel

Wprowadzić ograniczanie tempa przyjmowania wiadomości przez usługę odbiorczą bez dynamicznej alokacji pamięci i bez zmiany domyślnego zachowania istniejących usług.

## API

`scheduler_ipc_service_set_rate_limit(service_id, burst_capacity, refill_tokens, refill_interval_ticks)` konfiguruje token bucket:

- `burst_capacity`: maksymalna liczba tokenów, od 1 do `SCHEDULER_IPC_RATE_LIMIT_MAX_BURST` (64).
- `refill_tokens`: liczba tokenów przywracanych po każdym okresie, od 1 do `burst_capacity`.
- `refill_interval_ticks`: dodatnia liczba ticków PIT na okres uzupełniania.
- Bucket startuje pełny. Każda zaakceptowana wiadomość zużywa jeden token.
- Gdy tokenów brak, `scheduler_ipc_service_send_ex()` zwraca `SCHEDULER_IPC_DELIVERY_RATE_LIMITED`; wiadomość nie trafia do kolejki.

`scheduler_ipc_service_get_info()` ujawnia włączony limiter, liczbę dostępnych tokenów, pojemność i liczbę odrzuceń. Globalne diagnostyki zawierają `rate_limited_messages`.

## Test QEMU

Wymagany marker: `HYBRID: IPC_RATE_LIMIT_SELFTEST_OK`.

Test używa burstu 2 tokenów i jednego tokena co 4 ticki. Potwierdza dwa zaakceptowane wysłania, trzecie odrzucone bez tokena, odnowienie po upływie czasu, zachowanie kolejności i prawidłowe liczniki.

## Ograniczenia

- Limiter jest konfigurowany per usługa odbiorcza, nie osobno dla każdego nadawcy.
- Ticki PIT są jednostką czasu kernela, a nie precyzyjnym zegarem rzeczywistym.
- Liczniki i token bucket są ulotne; obecny kernel jest single-core Ring 0 i nie zapewnia synchronizacji SMP.

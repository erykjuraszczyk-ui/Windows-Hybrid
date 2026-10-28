# HYBRID OS v0.19.0 — IPC Token-Bucket Rate Limiting & Admission Control

## Cel

Ograniczanie tempa przyjmowania wiadomości przez usługę odbiorczą, bez dynamicznej alokacji i bez zmiany domyślnego zachowania istniejących usług.

## API

`scheduler_ipc_service_set_rate_limit(service_id, burst_capacity, refill_tokens, refill_interval_ticks)` konfiguruje token bucket:

- `burst_capacity`: 1–`SCHEDULER_IPC_RATE_LIMIT_MAX_BURST` (64).
- `refill_tokens`: dodatnia liczba tokenów przywracanych w każdym okresie, nie większa niż burst capacity.
- `refill_interval_ticks`: dodatnia liczba ticków PIT na okres refill.
- Bucket startuje pełny. Każda zaakceptowana wiadomość zużywa jeden token.
- Przy pustym bucket `scheduler_ipc_service_send_ex()` zwraca `SCHEDULER_IPC_DELIVERY_RATE_LIMITED`, a wiadomość nie jest dodawana do kolejki.

`scheduler_ipc_service_get_info()` zwraca stan limitera, dostępne tokeny, pojemność i liczbę odrzuceń. `scheduler_ipc_get_diagnostics()` raportuje globalny `rate_limited_messages`.

## Test QEMU

Wymagany marker: `HYBRID: IPC_RATE_LIMIT_SELFTEST_OK`. Test sprawdza burst dwóch wiadomości, odrzucenie trzeciej, refill po tickach PIT, kolejność zaakceptowanych wiadomości i diagnostykę.

## Ograniczenia

Limiter jest wspólny dla wszystkich źródeł danej usługi odbiorczej; nie jest per-source quota. Ticki PIT nie są czasem rzeczywistym. Kernel pozostaje single-core Ring 0; brak SMP safety i trwałości po restarcie.

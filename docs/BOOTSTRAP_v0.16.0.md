# HYBRID OS v0.16.0 — IPC Retry Backoff, Circuit Breakers & Service Health Monitoring

## API

- `scheduler_ipc_service_send_reliable_backoff(source, destination, delivery_id, request_id, message, max_attempts, base_retry_interval_ticks, max_retry_interval_ticks, deadline_ticks)` stosuje wykładnicze zwiększanie interwału retry aż do podanego limitu. Deadline nadal ma pierwszeństwo przed ponowieniem.
- `scheduler_ipc_service_set_circuit_breaker(service_id, failure_threshold, reset_timeout_ticks)` konfiguruje breaker; wartość zerowa dla obu parametrów wyłącza go.
- `scheduler_ipc_service_get_health(service_id, &health)` odczytuje stan usługi, liczniki i ticki ostatniego sukcesu/awarii.
- Stany breaker: `CLOSED`, `OPEN`, `HALF_OPEN`. Po cooldownie wykonywana jest jedna sonda; sukces zamyka breaker, a awaria otwiera go ponownie.

Starsze `scheduler_ipc_service_send_reliable_timed()` zachowuje stały interwał retry i jest źródłowo kompatybilne. Konfiguracja breakerów domyślnie pozostaje wyłączona.

## Diagnostyka i test

Wymagany marker QEMU: `HYBRID: IPC_CIRCUIT_BREAKER_SELFTEST_OK`.

Test wypełnia kolejkę usługi, generuje dwa kolejne backpressure events, weryfikuje stan OPEN i odrzucenie żądania, opróżnia kolejkę, czeka na cooldown, obserwuje udaną sondę HALF_OPEN i potwierdza ACK.

## Semantyka i ograniczenia

- Interwał bazowy jest podwajany po każdym timerowym retry do limitu (z saturacją, bez overflow); deadline wygrywa, jeśli oba terminy się spotkają.
- „Health” mierzy wyniki ścieżki nieblokującej IPC. Sukces oznacza przyjęcie do kolejki, nie dowód, że aplikacja wykonała efekt uboczny.
- Ticki PIT nie są zegarem czasu rzeczywistego; statystyki są ulotne i lokalne dla jednordzeniowego kernela Ring 0.

## Potwierdzenie CI

Scalony commit v0.16.0 przeszedł GitHub Actions: [przebieg #40522608835](https://github.com/erykjuraszczyk-ui/Windows-Hybrid/actions/runs/40522608835). Log zawiera `IPC_CIRCUIT_BREAKER_SELFTEST_OK`, `IPC_DIAGNOSTICS_OK`, `BOOT_OK` i końcowy `PASS`.

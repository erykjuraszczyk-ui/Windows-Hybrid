# HYBRID OS v0.20.0 — IPC Fair Scheduling, Per-Source Quotas & Priority-Aware Admission

## API

- `scheduler_ipc_service_set_source_quota(destination, max_messages_per_source)` — limit wiadomości jednego źródła oczekujących w kolejce docelowej; `0` wyłącza limit.
- `scheduler_ipc_service_set_priority_admission(destination, enabled)` — przy pełnej kolejce dopuszcza wyższą klasę priorytetu przez wywłaszczenie najstarszej wiadomości o najniższym priorytecie.
- `scheduler_ipc_service_set_priority(source, 1..8)` — priorytet źródła używany przez admission.

## Fair scheduling

Odbiór w usługowym kanale IPC wybiera kolejno źródła round-robin według identyfikatora usługi, zachowując kolejność FIFO dla wiadomości jednego źródła. Kanały ogólne z zerowym ID źródła zachowują tradycyjne FIFO.

## Wyniki

- `SCHEDULER_IPC_DELIVERY_SOURCE_QUOTA`: źródło osiągnęło skonfigurowany limit oczekujących wiadomości.
- `SCHEDULER_IPC_DELIVERY_PRIORITY_PREEMPTED`: wiadomość została przyjęta, zastępując niżej priorytetową wiadomość.
- `SCHEDULER_IPC_DELIVERY_BACKPRESSURE`: kolejka pełna, a priorytet nowej wiadomości nie uzasadnia wywłaszczenia.

## Test QEMU

Marker wymagany: `HYBRID: IPC_FAIR_ADMISSION_SELFTEST_OK`. Test obejmuje quota, odrzucenie przekroczenia, admission high-priority na pełnej kolejce, zachowanie kolejności fair receive i diagnostykę.

## Ograniczenia

Kolejka ma stałą pojemność 4; fair scheduling dotyczy kolejności odbioru, nie czasu CPU. Mechanizm pozostaje ulotny i przeznaczony dla jednordzeniowego kernela Ring 0.

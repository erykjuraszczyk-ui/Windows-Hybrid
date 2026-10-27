# HYBRID OS v0.17.0 — IPC Adaptive Load Balancing, Service Priorities & Congestion Control

## API

- `scheduler_ipc_service_set_priority(service_id, priority)`: ustawia priorytet 1–8; 1 jest domyślny.
- `scheduler_ipc_service_select_adaptive(candidates, count, &selected)`: wybiera dostępny cel o najniższym ważonym obciążeniu kolejki. Priorytet 8 zmniejsza wynik obciążenia względem priorytetu 1.
- `scheduler_ipc_service_set_congestion_control(service_id, high, low)`: aktywuje histerezę przeciążenia. `high` musi należeć do 1..SCHEDULER_IPC_CAPACITY, a `low < high`.
- `scheduler_ipc_service_get_info()` zwraca priorytet i flagę przeciążenia.

## Semantyka

Gdy liczba wiadomości osiąga high watermark, usługa staje się przeciążona i kolejne wywołania nieblokującego `send_ex` zwracają BACKPRESSURE. Flaga przeciążenia zostaje zdjęta po spadku kolejki do low watermark lub niżej. Wybór adaptacyjny pomija przeciążone i OPEN-circuit cele; remisy rozstrzygane są rotacyjnie.

## Testy i ograniczenia

Wymagany marker QEMU: `HYBRID: IPC_ADAPTIVE_LOAD_SELFTEST_OK`. Test obejmuje walidację zakresów, priorytetów, wybór celu, backpressure przy high watermark i odblokowanie po opróżnieniu kolejki.

To heurystyka lokalna dla podanej listy kandydatów i głębokości kolejki, nie pomiar czasu CPU ani gwarancja latencji. Obecny kernel pozostaje single-core Ring 0; synchronizacja SMP nie jest zapewniona.

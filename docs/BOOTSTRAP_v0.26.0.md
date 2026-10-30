# HYBRID OS v0.26.0 — IPC Congestion-Aware Scheduling & QoS Arbitration

## Cel

Połączyć istniejący priorytet usług, profile QoS i predykcję presji kolejki w jeden deterministyczny algorytm wyboru usługi docelowej. Scheduler powinien preferować kandydatów, którzy mają zarówno odpowiednią klasę obsługi, jak i rzeczywistą zdolność przyjęcia kolejnych wiadomości.

## Kontrakt

- Zachować API `scheduler_ipc_service_select_adaptive()`; rozszerzyć jego funkcję oceny kandydatów bez zmiany sygnatury.
- Odrzucać kandydatów zamkniętych, przeciążonych, z otwartym circuit breakerem, pełną kolejką lub wyczerpanym predictive admission budget.
- Wynik oceny uwzględnia: głębokość kolejki, priorytet usługi, profil QoS, trend predykcyjny i pozostały budżet admission.
- Wszystkie współczynniki są stałoprzecinkowe / całkowitoliczbowe, jawnie ograniczone i deterministyczne.
- Profile QoS: BEST_EFFORT otrzymuje niższą preferencję, NORMAL neutralną, LATENCY_SENSITIVE i DEADLINE_CRITICAL wyższą preferencję.
- Rosnący trend i malejący budżet zwiększają koszt kandydata; trend malejący może obniżyć koszt w ograniczonym zakresie.
- Rotacja remisów pozostaje aktywna, aby zapobiegać trwałemu faworyzowaniu pierwszego kandydata.
- Udostępnić liczniki per-usługa i globalne: selekcje, pominięcia z powodu presji oraz zastosowane korekty QoS.
- Dodać test QEMU dla arbitrażu QoS, priorytetu, trendu predykcyjnego, wyczerpanego budżetu i rotacji remisów.

## Ograniczenia

- Arbitraż wybiera usługę docelową; nie jest globalnym schedulerem zadań CPU.
- Nie zmienia pojemności kanału IPC (4 wiadomości), nie alokuje pamięci dynamicznie.
- Nie omija rate limiting, source quotas, TTL, deadline ani circuit breaker.
- QoS wpływa na wybór kandydata, ale nie gwarantuje czasu wykonania ani terminu dostawy.
- Synchronizacja pozostaje single-core Ring 0; brak gwarancji SMP.
- Brak zmiany semantyki istniejących publicznych wyników wysyłania.

## Walidacja

1. Freestanding C17, dotychczasowe ostrzeżenia jako błędy.
2. Test QEMU z markerem `HYBRID: IPC_QOS_ARBITRATION_SELFTEST_OK`.
3. Wszystkie wcześniejsze markery i `HYBRID: BOOT_OK`.
4. GitHub Actions musi zakończyć się `completed/success`; commit nie oznacza PASS.

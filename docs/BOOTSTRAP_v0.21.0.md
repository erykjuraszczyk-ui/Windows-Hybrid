# HYBRID OS v0.21.0 — IPC Deadline-Aware Scheduling, Starvation Prevention & QoS Profiles

## API

`scheduler_ipc_service_set_qos_profile(service_id, profile, default_deadline_budget_ticks, starvation_threshold_ticks)` ustawia profil wysyłanych wiadomości, opcjonalny budżet deadline’u i próg aging dla kolejki usługi.

Profile: `SCHEDULER_IPC_QOS_BEST_EFFORT`, `SCHEDULER_IPC_QOS_NORMAL`, `SCHEDULER_IPC_QOS_LATENCY_SENSITIVE`, `SCHEDULER_IPC_QOS_DEADLINE_CRITICAL`. Profil deadline-critical wymaga dodatniego budżetu deadline’u.

## Algorytm

1. Najstarsza wiadomość przekraczająca próg starvation ma pierwszeństwo.
2. W przeciwnym razie wybierany jest najbliższy deadline.
3. W pozostałych przypadkach wybierana jest najwyższa klasa QoS, z round-robin per źródło w tej klasie.

`deadline_tick` jest absolutnym tickiem PIT, `enqueue_tick` ustawia IPC. Domyślny deadline nadawcy to bieżący tick plus skonfigurowany budżet.

## Testy i ograniczenia

Wymagany marker: `HYBRID: IPC_DEADLINE_QOS_SELFTEST_OK`. Test sprawdza deadline, aging, profil i diagnostykę. Ticki PIT nie są czasem rzeczywistym; QoS to soft-real-time, nie gwarancja wykonania przed deadline’em. Kolejka ma pojemność 4, brak trwałości i SMP safety.

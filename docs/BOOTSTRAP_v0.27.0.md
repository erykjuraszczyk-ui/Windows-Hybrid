# HYBRID OS v0.27.0 — IPC Deficit Round Robin, QoS Starvation Protection & Fair Admission Credits

## Cel

Zapewnienie sprawiedliwego arbitrażu pomiędzy usługami IPC przy zachowaniu wag QoS, ograniczeń admission i deterministycznego zachowania kernela.

## Model DRR

Każda usługa posiada własny, ulotny licznik `arbitration_deficit_credit` ograniczony do 32 jednostek. Przy każdej rundzie kwalifikujący się kandydat otrzymuje quantum:

| Profil QoS | Quantum |
|---|---:|
| BEST_EFFORT | 2 |
| NORMAL | 4 |
| LATENCY_SENSITIVE | 6 |
| DEADLINE_CRITICAL | 8 |

Wybrana usługa zużywa maksymalnie 4 jednostki kredytu. Kredyt wpływa na ocenę selekcji, ale nie zastępuje oceny obciążenia, priorytetu i presji predykcyjnej.

## Ochrona przed zagłodzeniem

Licznik oczekiwania rośnie dla kwalifikujących się kandydatów. Po czterech rundach oczekiwania kandydat uzyskuje ochronę starvation i pierwszeństwo przed zwykłą oceną punktową. Po przyznaniu obsługi licznik oczekiwania jest zerowany. Remisy rozstrzyga obracany kursor kandydatów.

## Integracja z admission

Przed przyznaniem kredytu usługa jest sprawdzana pod kątem:
- aktywności i stanu zamykania;
- otwartego circuit breakera;
- stanu congestion;
- dostępnego predictive admission budget;
- poprawności głębokości kolejki.

Kandydaci zablokowani przez te warunki są pomijani i nie zdobywają kredytu w tej rundzie.

## API i telemetria

Rozszerzono migawkę informacji o usłudze o bieżący kredyt, rundy oczekiwania, sumę zdobytych/zużytych kredytów i liczbę aktywacji ochrony. Diagnostyka globalna raportuje kredyty, wizyty kandydatów i ochronę starvation.

Test `scheduler_ipc_drr_fair_admission_self_test()` jest uruchamiany podczas startu kernela. Smoke test wymaga markera `HYBRID: IPC_DRR_FAIR_ADMISSION_SELFTEST_OK`.

## Ograniczenia

- Arbitraż dotyczy wyboru docelowej usługi IPC; nie jest schedulerem CPU.
- Quantum QoS jest względną wagą, a nie gwarancją deadline'u.
- Kredyty i liczniki są ulotne.
- Kolejki pozostają statyczne i ograniczone.
- Synchronizacja nadal zakłada single-core Ring 0; nie dodano SMP safety.

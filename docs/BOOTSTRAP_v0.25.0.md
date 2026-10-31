# HYBRID OS v0.25.0 — IPC Predictive Queue Pressure & Admission Budgeting

## Cel

Rozszerzyć adaptacyjny admission z v0.24.0 o ograniczone przewidywanie trendu obciążenia kolejki. Kontroler ma rozpoznawać utrzymujący się wzrost lub spadek głębokości kolejki i odpowiednio korygować budżet przyjęć, zanim kolejka osiągnie próg przeciążenia.

## Zakres funkcjonalny

- Deterministyczny estymator oparty na stałym, małym oknie próbek głębokości kolejki.
- Wyłącznie arytmetyka całkowita i jawnie ograniczone wartości; bez alokacji dynamicznej.
- Rozróżnienie trendu rosnącego, stabilnego i malejącego.
- Budżet admission per usługa, ograniczony do zakresu skonfigurowanego przez API i pojemności kolejki.
- Zmiana budżetu z histerezą, aby uniknąć oscylacji przy pojedynczych skokach obciążenia.
- Liczniki próbek, prognoz wzrostu/spadku, korekt budżetu, odrzuceń budżetowych i odzyskania.
- Publiczna migawka diagnostyczna per usługa oraz liczniki globalne.
- Test regresyjny QEMU dla trendu wzrostowego, ograniczenia admission, trendu spadkowego i odbudowy budżetu.

## Kontrakt i ograniczenia

- Przewidywanie jest lokalną estymacją trendu, a nie gwarancją przyszłego obciążenia ani modelem uczenia maszynowego.
- Okno próbek ma stały rozmiar i działa w pamięci statycznej.
- Pojemność kanału IPC pozostaje stała; budżet nie powiększa bufora i nie przydziela pamięci.
- Wyniki estymatora nie mogą omijać istniejących limitów QoS, TTL, rate limiting, quota ani circuit breaker.
- Brak gwarancji SMP; synchronizacja pozostaje zgodna z bieżącym modelem single-core Ring 0.
- Nie zmieniać semantyki istniejących wyników API bez jawnej migracji i testów.

## Plan walidacji

1. Kompilacja freestanding C17 z dotychczasowymi ostrzeżeniami traktowanymi jako błędy.
2. QEMU boot smoke test z markerem `HYBRID: IPC_PREDICTIVE_PRESSURE_SELFTEST_OK`.
3. Potwierdzenie `HYBRID: BOOT_OK` oraz wszystkich wcześniejszych markerów regresyjnych.
4. GitHub Actions musi zakończyć się statusem `completed/success`; sam commit nie oznacza CI PASS.

## Kolejność implementacji

1. Dodać stałe okna próbek i stan predyktora do struktury usługi.
2. Aktualizować próbki przy zdarzeniach enqueue/receive, bez pętli o nieograniczonym czasie.
3. Wyliczać trend i budżet admission w funkcji kontrolera.
4. Włączyć decyzję budżetową w ścieżkę wysyłania po walidacji usługi i przed zapisem do kanału.
5. Dodać migawki diagnostyczne, liczniki i test QEMU.
6. Uzupełnić README, indeks i status implementacji po potwierdzeniu CI.

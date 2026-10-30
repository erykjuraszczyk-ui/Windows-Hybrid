# HYBRID OS v0.24.0 — IPC Dynamic Admission Control & Adaptive Queue Pressure Management

## Cel
Automatycznie dostosowywać próg przyjmowania wiadomości usługi IPC do obserwowanego przeciążenia, zachowując ograniczoną pamięć, przewidywalną histerezę i jawne wyniki admission.

## Kontrakt
- Nowe API `scheduler_ipc_service_set_adaptive_admission(service_id, min_high, max_high)` włącza adaptacyjny admission dla usługi.
- Zakres progów jest ograniczony do `1..SCHEDULER_IPC_CAPACITY`; `min_high <= max_high`.
- Startowy próg high = max_high, low = high - 1.
- Próba wysłania do przeciążonej usługi zwraca `BACKPRESSURE`; adaptacyjny kontroler obniża high o jeden, nie poniżej min_high, i ustawia low = high - 1.
- Po zejściu kolejki do low usługa odzyskuje stan, a kontroler podnosi high o jeden, nie powyżej max_high.
- Liczniki per-usługa i globalne raportują korekty progu i zdarzenia presji.
- QEMU test potwierdza redukcję 3→2 po presji, brak przedwczesnego recovery, wzrost 2→3 po zejściu do low oraz ponowne przyjęcie wiadomości.

## Ograniczenia
Kolejka nadal ma stałą pojemność 4. Adaptacja dotyczy progu admission, nie rozmiaru bufora. Algorytm jest deterministyczny, jednokrokowy i chroniony przez wyłączenie przerwań w aktualnym single-core Ring 0. Nie ma SMP, dynamicznej alokacji ani automatycznego skalowania pamięci.

## Kryteria akceptacji
- Kompilacja C17 z `-Wall -Wextra -Werror`.
- QEMU marker `HYBRID: IPC_ADAPTIVE_ADMISSION_SELFTEST_OK`.
- Wszystkie wcześniejsze markery i `HYBRID: BOOT_OK`.

# HYBRID OS v0.15.0 — IPC Timer-Driven Retry, Delivery Deadlines & Dead-Letter Queue

## Cel

Uzupełnić v0.14.0 o automatyczne retry sterowane tickami PIT, deadline dostawy i ograniczoną dead-letter queue (DLQ), bez dynamicznej alokacji pamięci.

## API

- `scheduler_ipc_service_send_reliable_timed(source, destination, delivery_id, request_id, message, max_attempts, retry_interval_ticks, deadline_ticks)` wysyła pierwszą próbę i uruchamia retry timerowe.
- `scheduler_ipc_service_send_reliable(...)` zachowuje dotychczasowy kontrakt bez timerowego retry.
- `scheduler_ipc_dead_letter_count()` zwraca liczbę wpisów oczekujących w DLQ.
- `scheduler_ipc_dead_letter_receive(&entry)` pobiera najstarszy wpis; zawiera receipt, oryginalną wiadomość, przyczynę i tick rejestracji.

Oba parametry czasowe muszą być niezerowe razem lub zerowe razem. `deadline_ticks` i `retry_interval_ticks` są względnymi odstępami w tickach PIT; receipt przechowuje wynikowy deadline i następny tick retry.

## Zasady

1. Procesor dostaw działa na każdej ścieżce ticka timera także wtedy, gdy preemptive scheduler jest nieaktywny.
2. Najpierw sprawdzany jest deadline; jeśli już upłynął, ponowienie nie zostaje uruchomione.
3. Retry nie wysyła kolejnej kopii, jeżeli ten sam delivery ID nadal czeka w kolejce celu lub znajduje się w jego ograniczonej historii odbioru.
4. Po wygaśnięciu deadline dostawa przechodzi do stanu `EXPIRED`; przyczyna `DEADLINE_EXPIRED` oraz kopia wiadomości trafiają do DLQ.
5. Gdy DLQ jest pełna, najstarszy wpis jest usuwany, a licznik utraty wpisów zwiększany. Kolejka jest bounded i ulotna.

## Test QEMU

Wymagany marker: `HYBRID: IPC_TIMER_RETRY_DEAD_LETTER_SELFTEST_OK`.

Test potwierdza automatyczny retry bez duplikowania wiadomości, ACK zanim wygaśnie deadline, ponawianie przy pełnej kolejce, terminalne przejście po deadline oraz odczyt wiadomości z DLQ.

## Ograniczenia

- Ticki PIT nie są czasem rzeczywistym i zależą od konfiguracji timerów.
- Wysyłka z timer ISR jest ograniczona do stałych tablic i nieblokujących prób IPC.
- Receipt oraz DLQ są przechowywane wyłącznie w RAM i nie przetrwają resetu.
- Mechanizm nie zapewnia dokładnie jednokrotnego wykonania skutków ubocznych ani SMP safety.

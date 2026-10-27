# HYBRID OS v0.14.0 — IPC Delivery Receipts, Acknowledgements & Retry Core

## Cel

Rozróżnić przyjęcie wiadomości do kolejki od potwierdzenia jej odebrania przez odbiorcę. Dodać ograniczoną tabelę dostaw, jawny ACK, ponawianie z limitem i tłumienie duplikatów na poziomie kernela.

## API

- `scheduler_ipc_service_send_reliable(source, destination, delivery_id, request_id, message, max_attempts)` tworzy wpis śledzenia i wykonuje pierwszą próbę. `delivery_id`, `request_id` i `max_attempts` muszą być niezerowe.
- `scheduler_ipc_delivery_get_receipt(delivery_id, &receipt)` odczytuje stan, liczbę prób, limit prób, endpointy, request ID oraz ostatni wynik.
- `scheduler_ipc_service_ack_delivery(destination, delivery_id)` oznacza dostawę jako potwierdzoną. ACK jest akceptowany tylko dla docelowej usługi po odebraniu wiadomości z jej kolejki.
- `scheduler_ipc_delivery_retry(delivery_id)` wykonuje jedną kolejną próbę. Klient steruje polityką czasową i wywołaniem retry; ten etap nie dodaje timera automatycznych ponowień.
- `scheduler_ipc_delivery_pending_count()` i `scheduler_ipc_get_diagnostics()` pokazują liczbę dostaw oczekujących i liczniki rozpoczęć, ACK, retry, wyczerpania oraz stłumionych duplikatów.

## Stany i reguły

| Stan | Znaczenie |
|---|---|
| `PENDING` | Dostawa oczekuje na ACK; może być ponawiana, jeśli nie osiągnęła limitu. |
| `ACKNOWLEDGED` | Odbiorca odebrał wiadomość i jawnie potwierdził dostawę; retry nie wysyła jej ponownie. |
| `EXHAUSTED` | Liczba prób osiągnęła `max_attempts`; kolejne retry zwraca `RETRY_EXHAUSTED`. |
| `FAILED` | Wystąpił błąd nietrwały w tym modelu, np. endpoint nie istnieje lub jest zamknięty. |

Każda śledzona wiadomość otrzymuje niezerowy `delivery_id`. Przed ponowną wysyłką kernel sprawdza, czy ten identyfikator znajduje się już w kolejce celu lub w jego ograniczonej historii odebranych wiadomości. W takim przypadku zwraca `DUPLICATE_SUPPRESSED` i nie umieszcza kopii w kolejce. Odbiorca musi wykonać ACK dopiero po przetworzeniu wiadomości.

Tablica śledzenia ma stałą pojemność `SCHEDULER_IPC_MAX_TRACKED_DELIVERIES`; po zapełnieniu terminalne wpisy mogą zostać zastąpione. Historia deduplikacji odbiorcy ma stałą pojemność `SCHEDULER_IPC_DELIVERY_HISTORY`.

## Walidacja

CI buduje ISO i uruchamia test regresyjny w QEMU. Marker wymagany przez smoke test:

- `HYBRID: IPC_RECEIPTS_RETRIES_SELFTEST_OK`
- `HYBRID: IPC_DIAGNOSTICS_OK`
- `HYBRID: BOOT_OK`

Test sprawdza dostawę i receipt, tłumienie duplikatu przed oraz po odbiorze, ACK i blokadę retry po ACK, a także limit prób w warunkach pełnej kolejki.

## Ograniczenia

- Śledzenie i historia są ulotne; reset kernela usuwa je.
- Retry jest jawnie wywoływane przez API, nie jest jeszcze automatyzowane przez timer/backoff.
- ACK jest kernelowym prymitywem, a nie osobnym routowanym komunikatem sieciowym/IPC.
- To nie jest gwarancja exactly-once dla efektów ubocznych: odbiorca może wykonać efekt i ulec awarii przed ACK.
- Prototyp pozostaje jednordzeniowy i Ring 0; model synchronizacji nie jest gotowy dla SMP.

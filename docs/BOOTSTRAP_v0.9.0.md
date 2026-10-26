# HYBRID OS v0.9.0 — IPC Services & Kernel Communication Layer

## Cel etapu

Dodać nazwany rejestr usług IPC nad kanałami FIFO z v0.8.0. Usługi otrzymują unikalne identyfikatory, mogą być wyszukiwane po nazwie i odbierać wiadomości kierowane bezpośrednio do ich endpointu.

## Publiczne API

```c
bool scheduler_ipc_service_register(const char *name,
                                    scheduler_ipc_service_id_t *service_id);
bool scheduler_ipc_service_lookup(const char *name,
                                  scheduler_ipc_service_id_t *service_id);
bool scheduler_ipc_service_unregister(scheduler_ipc_service_id_t service_id);
bool scheduler_ipc_service_send(scheduler_ipc_service_id_t source_service_id,
                                scheduler_ipc_service_id_t destination_service_id,
                                uint32_t request_id,
                                const scheduler_ipc_message_t *message);
bool scheduler_ipc_service_receive(scheduler_ipc_service_id_t service_id,
                                   scheduler_ipc_message_t *message);
uint32_t scheduler_ipc_service_count(void);
```

## Kontrakt

- Maksymalnie 8 aktywnych usług w rejestrze.
- Nazwa: 1–15 bajtów plus NUL, porównanie case-sensitive; brak dynamicznego przydziału pamięci.
- ID usługi jest niezerowe, monotoniczne i nie jest recyklingowane. Po przepełnieniu licznika rejestracja odmawia utworzenia kolejnej usługi.
- Wiadomości nadal przechodzą przez istniejący, bounded FIFO (4 sloty, payload do 24 bajtów). Struktura wiadomości zawiera teraz `source_service_id` i `request_id`; ID docelowe wynika z kolejki, do której wysłano wiadomość.
- Wysyłający i odbierający aktywują referencję endpointu na czas API-call. `unregister` odmawia, jeśli API-call jest aktywny, kanał zawiera wiadomości lub jakikolwiek writer/reader oczekuje.
- `send` i `receive` mogą blokować tylko wtedy, gdy są wywołane przez aktualnie uruchomione zadanie preemptive; z kontekstu bootstrapowego wysyłka/odbiór działa od razu, jeżeli operacja nie wymaga blokowania.
- Rejestr i licznik aktywnych wywołań chronione są przez krótkie sekcje z wyłączonymi przerwaniami. To nie jest synchronizacja SMP.

## Walidacja

Na Ubuntu/Debian:

```sh
sudo apt update
sudo apt install -y build-essential binutils git curl xorriso qemu-system-x86
make clean
make test
```

Wymagane markery:

- `HYBRID: SEMAPHORE_IPC_SELFTEST_OK`
- `HYBRID: IPC_SERVICES_SELFTEST_OK`
- `HYBRID: BOOT_OK`

Test rejestru sprawdza: błędne i zbyt długie nazwy, duplikaty, lookup, routing, source ID i request ID, odbiorcę blokowanego na pustym kanale, odmowę wyrejestrowania endpointu z wiadomością, poprawny odbiór oraz ponowne wyrejestrowanie pustych endpointów.

## Ograniczenia

- Prototyp x86-64 single-core, wszystkie zadania nadal pracują w Ring 0.
- Brak endpoint ACL/capabilities, Ring 3, izolacji procesów, SMP, ograniczeń per-client, channel close dla oczekujących zadań i timeoutów IPC.
- FIFO ma stały rozmiar, a blokujący nadawca/odbiorca wykorzystuje obecne wait queues i scheduler.
- QEMU potwierdza wyłącznie konfigurację testową, nie fizyczny sprzęt.

## Status wydania

Walidacja pozostaje **CI validation pending** do czasu pomyślnej kompilacji i obecności markerów `HYBRID: IPC_SERVICES_SELFTEST_OK` i `HYBRID: BOOT_OK` w najnowszym przebiegu CI.
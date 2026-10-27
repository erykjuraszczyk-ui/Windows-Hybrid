# HYBRID OS v0.12.0 — IPC Reliability & Service Lifecycle Core

## Cel

Uzupełnić IPC runtime o bezpieczny cykl życia usług po dodaniu pending calls w v0.11.0.

## Semantyka cyklu życia

- `scheduler_ipc_service_unregister(id)` ustawia usługę jako `closing`. Nowe operacje, lookup, enumeration i `get_info` przestają ją udostępniać.
- Wiadomości zakolejkowane w zamykanej usłudze są odrzucane. Zadania zablokowane w `receive` albo oczekujące na miejsce w `send` są wybudzane z wynikiem anulowania.
- Każde aktywne pending call, którego źródłem albo celem jest zamykana usługa, otrzymuje dopasowaną odpowiedź `ERROR` z `SCHEDULER_IPC_STATUS_NOT_FOUND`. Oczekujący klient jest wybudzany, a wpis pending zwalniany przez zwykłą ścieżkę zakończenia wywołania.
- Aktywne referencje chronią slot usługi. Slot jest odzyskiwany dopiero po zakończeniu operacji i opróżnieniu kolejek oczekujących; identyfikatory pozostają monotoniczne.
- Powtórne wywołanie `unregister` podczas zamykania zwraca `true`, jeśli zamknięcie zostało wcześniej przyjęte. Po odzyskaniu slotu ID jest nieznane i wywołanie zwraca `false`.

## Diagnostyka

`scheduler_ipc_get_diagnostics()` zwraca kumulatywne liczniki startów, odpowiedzi, timeoutów, anulowań, niedostępnych usług, błędów wysyłki, odrzuconych odpowiedzi oraz zamknięć. Zwraca również liczbę aktywnych pending calls i usług dostępnych w discovery.

## Walidacja

```sh
make clean
make test
```

Smoke test wymaga markerów:
- `HYBRID: IPC_RUNTIME_SELFTEST_OK`
- `HYBRID: IPC_LIFECYCLE_SELFTEST_OK`
- `HYBRID: IPC_DIAGNOSTICS_OK`
- `HYBRID: BOOT_OK`

Test v0.12.0 zamyka serwer z jednym aktywnym wywołaniem, sprawdza błąd `NOT_FOUND` u klienta i brak wycieków pending calls.

## Granice

Prototyp dotyczy jednordzeniowego kernela x86-64 w Ring 0. Sekcje krytyczne bazują na wyłączaniu przerwań i nie zapewniają synchronizacji SMP. QEMU sprawdza wyłącznie testowaną konfigurację emulatora.

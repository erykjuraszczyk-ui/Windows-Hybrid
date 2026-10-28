# Mapa drogowa HYBRID OS

## Profil docelowy

- Architektura: x86-64.
- Rozruch: UEFI; docelowo kontrolowany łańcuch zaufania.
- Język kernela i podstawowych usług: C17 oraz Assembly.
- Aplikacje: Ring 3, wersjonowane ABI i IPC.
- Grafika: framebuffer, Surface API, compositor, shared buffers i wymienne backendy.
- Bezpieczeństwo: capabilities, sandbox, UID/GID, Policy Engine i audyt.
- Dostarczanie: CI, powtarzalne kompilacje, podpisy i zweryfikowane aktualizacje.

## Historia wersji

| Wersja | Cel |
|---|---|
| v0.1.0 | x86-64, UEFI, C17, system natywny; grafika odłożona; nowe repozytorium |
| v0.2.0 | Alokator stron, IDT, wyjątki i PIT; CI w QEMU |
| v0.3.0 | Kooperatywny step-based Scheduler Core; tablica zadań i round-robin |
| v0.4.0 | Eksperymentalna preempcja zadań Ring 0 przez IRQ0 i osobne stosy; CI QEMU |
| v0.5.0 | Task Lifecycle, bezpieczny reaping, GDT/TSS i stosy IST; walidacja w QEMU |
| v0.6.0 | Task Blocking, Wakeup & Synchronization Core; wait queues i mutex w jednordzeniowym Ring 0; CI PASS |
| v0.7.0 | Sleep/Timer Core, timed wait queues, bezpieczne anulowanie READY/BLOCKED; CI PASS w QEMU |
| v0.8.0 | Semafory zliczające i ograniczony kanał IPC; CI PASS w QEMU |
| v0.9.0 | Rejestr nazwanych usług, routing wiadomości, request IDs i bezpieczne wyrejestrowanie; CI PASS po scaleniu |
| v0.10.0 | Wersjonowany request/response, typowane błędy i service discovery; CI PASS po scaleniu |
| v0.11.0 | IPC runtime, pending-call table, timeout/anulowanie oraz concurrent reply demultiplexing; CI PASS po scaleniu |
| v0.12.0 | IPC reliability: zamykanie usług, anulowanie powiązanych wywołań i diagnostyka lifecycle/timeoutów/dropów; CI PASS po scaleniu |
| v0.13.0 | IPC backpressure: BLOCK/REJECT/DROP_NEWEST/DROP_OLDEST, nieblokujące wyniki dostarczania, liczniki i QEMU regression test; CI PASS na gałęzi funkcji |
| v0.14.0 | IPC delivery receipts, jawne ACK, limitowane retry, ochrona przed duplikatami i diagnostyka; CI PASS po scaleniu |
| v0.15.0 | Retry sterowane tickami PIT, terminy dostaw i ograniczona dead-letter queue; CI PASS po scaleniu |
| v0.16.0 | Wykładniczy backoff z limitem, circuit breakers CLOSED/OPEN/HALF_OPEN, zdrowie usług i test odzyskiwania; CI PASS po scaleniu ([przebieg](https://github.com/erykjuraszczyk-ui/Windows-Hybrid/actions/runs/40522608835)) |
| v0.17.0 | Adaptacyjne równoważenie IPC po głębokości kolejki i priorytecie, progi przeciążenia high/low watermark oraz test QEMU; CI PASS po scaleniu |
| v0.18.0 | Token-bucket rate limiting per usługa, burst/refill w tickach PIT, admission control i diagnostyka; CI PASS na gałęzi funkcji ([QEMU](https://github.com/erykjuraszczyk-ui/Windows-Hybrid/actions/runs/40666400100)) |
| v1.0.0–v1.4.0 | Dojrzewanie podstaw systemu i wejścia/pulpitu; szczegóły wymagają weryfikacji historycznych materiałów |
| v1.5.0 | Surface API i Desktop Services |
| v1.6.0 | Pamięć współdzielona i dwa bufory |
| v1.7.0 | Harmonogram klatek i programowe planowanie prezentacji |
| v1.8.0 | Abstrakcja backendu i VirtIO-GPU 2D |
| v1.9.0 | Reset sterownika, odzyskiwanie i kwarantanna DMA |
| v2.0.0 | Stabilizacja pulpitu i zamrożenie ABI |
| v2.0.1 | Zgodne wstecz poprawki stabilności |
| v2.1.0 | Diagnostyka, logi, trace i raporty awarii |
| v2.2.0 | SDK C17, runtime, szablony i narzędzia aplikacyjne |
| v2.3.0 | Pakiety .happ, zależności i Package Manager |
| v2.4.0 | Capabilities, sandbox i limity |
| v2.5.0 | Konta, grupy, sesje i prawa VFS |
| v2.6.0 | Policy Engine i administracja politykami |
| v2.7.0 | Fuzzing, fault injection i testy zagrożeń |
| v2.8.0 | CI i powtarzalne buildy |
| v2.9.0 | Kanały wydań, metadane i aktualizator |
| v3.0.0 | Secure Boot, manifesty slotów i zweryfikowane aktualizacje |

## Zależności architektoniczne

1. Rozruch, pamięć i przerwania poprzedzają usługi wyższego poziomu.
2. VMM i IPC muszą być stabilne przed shared memory i sandboxingiem.
3. Surface API poprzedza podwójne buforowanie i Frame Scheduler.
4. VirtIO-GPU wymaga działających PCI, virtqueue i zarządzania DMA.
5. SDK wymaga loadera ELF oraz stabilnego kontraktu ABI.
6. Package Manager wymaga VFS i bezpiecznej walidacji pakietów.
7. Tożsamości, capabilities i Policy Engine muszą być egzekwowane w punktach wykonania operacji.
8. CI i powtarzalne buildy poprzedzają zaufane wydania.
9. Verified Boot wymaga jawnego łańcucha zaufania, ochrony kluczy i procedury odzyskiwania.

## Reguła wydania

Kolejny numer nie zamyka poprzedniego etapu automatycznie. Funkcja jest gotowa dopiero po implementacji, kompilacji i zaliczeniu właściwych testów.

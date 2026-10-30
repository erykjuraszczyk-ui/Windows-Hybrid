# HYBRID OS v0.23.0 — IPC Queue Compaction, Capacity Telemetry & Backpressure Recovery

## Cel
Rozszerzyć ograniczone kolejki IPC o mierzalną kondycję bufora i deterministyczne odzyskiwanie po przeciążeniu, bez dynamicznej alokacji i bez zmiany domyślnej semantyki wysyłania.

## Kontrakt implementacji
- Zachować stałą pojemność SCHEDULER_IPC_CAPACITY i kolejność FIFO względną dla wiadomości, które pozostają w kolejce.
- Zliczać operacje kompaktowania i liczbę przeniesionych wpisów przy usuwaniu elementów ze środka kolejki, pruning TTL i admission przez priorytet.
- Udostępnić w scheduler_ipc_service_info_t: wolne sloty, procent wykorzystania, liczbę kompaktowań, liczbę przesuniętych wiadomości i liczbę odzyskanych stanów backpressure.
- Udostępnić odpowiadające liczniki globalne w scheduler_ipc_diagnostics_t.
- Utrzymać histerezę: wejście w stan congested przy count >= high_watermark, wyjście dopiero przy count <= low_watermark.
- Zliczać każde przejście congested → recovered dokładnie raz.
- Test QEMU sprawdza 75/50/25% wykorzystania, wolne sloty, blokadę wysyłki przy wysokim watermark, brak przedwczesnego odblokowania oraz odzyskanie po osiągnięciu low watermark.

## Ograniczenia
Pojemność pozostaje stała (4), metryki są ulotne i chronione wyłączeniem przerwań dla obecnego single-core Ring 0. Procent wykorzystania jest migawką obliczaną podczas odczytu. Nie dodajemy SMP, trwałego telemetry store ani automatycznego skalowania pojemności.

## Kryteria akceptacji
- make test przechodzi w CI.
- Serial log zawiera HYBRID: IPC_QUEUE_TELEMETRY_SELFTEST_OK i HYBRID: BOOT_OK.
- Wszystkie wcześniejsze markery regresji pozostają obecne.

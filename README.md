# Windows-Hybrid — HYBRID OS

**Projekt:** natywny system operacyjny x86-64.  
**Aktualny etap:** v0.5.0 — Task Lifecycle oraz GDT/TSS/IST; walidacja przez CI/QEMU w toku.

## Historia statusu

- **v0.1.0 — CI PASS:** minimalny kernel, framebuffer i diagnostyka COM1.
- **v0.2.0 — CI PASS w skonfigurowanym QEMU:** Memory Core, IDT, wyjątek i PIT.
- **v0.3.0 — CI PASS w QEMU:** kooperatywny scheduler round-robin.
- **v0.4.0 — CI PASS w QEMU:** test przełączania kontekstu dwóch zadań Ring 0 przez IRQ0.
- **v0.5.0 — w trakcie walidacji:** własne GDT/TSS, stosy IST dla krytycznych wyjątków, tworzenie/zakończenie/reaping zadań i test odzyskiwania stron stosów.

## Zakres v0.5.0

- Instalacja własnej tablicy GDT z selektorami Ring 0 i deskryptorem TSS 64-bit.
- TSS zawiera osobny stos wejścia Ring 0 oraz cztery niezależne stosy IST.
- IDT przypisuje IST1 do #DF, IST2 do #GP, IST3 do #PF i IST4 do NMI.
- Task API nadaje niezerowe, monotoniczne identyfikatory, prowadzi stany READY/RUNNING/EXITED/FAILED i odrzuca próbę odzyskania stosu zadania, które jeszcze wykonuje się na tym stosie.
- Reaping jest odroczony do chwili, gdy scheduler wróci ze stosu zadania; potem zwalniana jest strona stosu i slot może zostać ponownie użyty bez ponownego użycia identyfikatora.
- CI w QEMU sprawdza TSS/IST, cykl życia zadania, naturalne zakończenie dwóch zadań oraz odzyskanie stron stosów.

**Granice bezpieczeństwa:** wszystkie zadania nadal działają w Ring 0. TSS/IST zapewnia osobne stosy obsługi wejść i wyjątków, ale nie tworzy izolacji procesów ani sandboxa. Nie ma jeszcze Ring 3, SMP, zapisu FPU/SSE/AVX, blokowania/wybudzania ani kontrolowanego testu #DF przez fault-injection. Wynik QEMU nie jest certyfikacją fizycznego sprzętu.

## Uruchomienie

Na Ubuntu/Debian:

    sudo apt update
    sudo apt install -y build-essential binutils git curl xorriso qemu-system-x86
    make clean
    make test

Log poprawnego testu powinien zawierać: `TSS_OK`, `IDT_OK`, `IST_OK`, `EXCEPTION_SELFTEST_OK`, `TASK_LIFECYCLE_SELFTEST_OK`, `PREEMPTIVE_SELFTEST_OK` oraz `BOOT_OK`.

## Dokumentacja

- [Indeks dokumentów](docs/INDEX.md)
- [Mapa drogowa](docs/ROADMAP.md)
- [Architektura](docs/ARCHITECTURE.md)
- [Model bezpieczeństwa](docs/SECURITY_MODEL.md)
- [Testowanie](docs/TESTING.md)
- [Proces wydań](docs/RELEASE_PROCESS.md)
- [Status implementacji](docs/IMPLEMENTATION_STATUS.md)
- [HYBRID OS v0.4.0 — Preemptive Scheduler](docs/BOOTSTRAP_v0.4.0.md)
- [HYBRID OS v0.5.0 — Task Lifecycle, TSS/IST](docs/BOOTSTRAP_v0.5.0.md)

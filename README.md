# Windows-Hybrid — HYBRID OS

**Projekt:** natywny system operacyjny x86-64.  
**Aktualny etap:** v0.6.0 — Task Blocking, Wakeup & Synchronization Core; **CI PASS w skonfigurowanym QEMU**.

## Historia statusu

- **v0.1.0 — CI PASS:** minimalny kernel, framebuffer i diagnostyka COM1.
- **v0.2.0 — CI PASS w skonfigurowanym QEMU:** Memory Core, IDT, wyjątek i PIT.
- **v0.3.0 — CI PASS w QEMU:** kooperatywny scheduler round-robin.
- **v0.4.0 — CI PASS w QEMU:** test przełączania kontekstu dwóch zadań Ring 0 przez IRQ0.
- **v0.5.0 — CI PASS w QEMU:** GDT/TSS/IST, cykl życia zadań i odzyskiwanie stron stosów.
- **v0.6.0 — CI PASS w QEMU:** stany BLOCKED/READY, wait queues, wybudzanie, mutex z przekazaniem własności i testy synchronizacji.

Potwierdzający przebieg: [GitHub Actions #40310647220](https://github.com/erykjuraszczyk-ui/Windows-Hybrid/actions/runs/40311257137). Log zawiera `SYNC_SELFTEST_OK`, `PREEMPTIVE_SELFTEST_OK` oraz `BOOT_OK`; artefakt `hybrid-os-v0.6.0` zawiera kernel ELF, ISO i logi QEMU.

## Zakres v0.6.0

- Wynik kroku `SCHEDULER_STEP_BLOCK` przenosi zadanie kooperatywne do stanu `BLOCKED`; nieaktywne zadania nie są wybierane do wykonania.
- `scheduler_task_wake` wybudza zadanie tylko ze stanu `BLOCKED`.
- Kolejki oczekiwania obsługują FIFO `wake_one` i `wake_all`, wykrywają pełną kolejkę oraz odrzucają nieaktualne identyfikatory.
- Mutex jest nie-rekurencyjny, sprawdza właściciela i przekazuje własność bezpośrednio kolejnemu oczekującemu (handoff), aby uniknąć wyścigu o ponowne przejęcie blokady.
- Sekcje krytyczne chronią zmiany stanu i kolejki przy zachowaniu poprzedniego stanu IF; kod zadania nie jest wykonywany z wyłączonymi przerwaniami.
- CI testuje blokowanie dwóch zadań, `wake_all`, blokowanie na mutexie, przekazanie własności, zakończenie zadań oraz odzyskiwanie stron.

**Ograniczenia:** mechanizmy blokowania i mutex są obecnie przeznaczone dla kooperatywnych funkcji `step`. Nie są jeszcze API blokowania dowolnej preemptive funkcji entry ani pełnym mechanizmem synchronizacji SMP. Sekcje z `cli` chronią wyłącznie konfigurację jednordzeniową. Brak Ring 3, SMP, FPU/SSE/AVX save/restore i dziedziczenia priorytetów.

## Uruchomienie

Na Ubuntu/Debian:

    sudo apt update
    sudo apt install -y build-essential binutils git curl xorriso qemu-system-x86
    make clean
    make test

Wymagane markery: `TSS_OK`, `IDT_OK`, `IST_OK`, `EXCEPTION_SELFTEST_OK`, `TIMER_OK`, `SCHEDULER_SELFTEST_OK`, `TASK_LIFECYCLE_SELFTEST_OK`, `SYNC_SELFTEST_OK`, `PREEMPTIVE_SELFTEST_OK` oraz `BOOT_OK`.

## Dokumentacja

- [Indeks dokumentów](docs/INDEX.md)
- [Mapa drogowa](docs/ROADMAP.md)
- [Architektura](docs/ARCHITECTURE.md)
- [Model bezpieczeństwa](docs/SECURITY_MODEL.md)
- [Testowanie](docs/TESTING.md)
- [Proces wydań](docs/RELEASE_PROCESS.md)
- [Status implementacji](docs/IMPLEMENTATION_STATUS.md)
- [HYBRID OS v0.5.0 — Task Lifecycle, TSS/IST](docs/BOOTSTRAP_v0.5.0.md)
- [HYBRID OS v0.6.0 — Blocking, Wakeup, Synchronization](docs/BOOTSTRAP_v0.6.0.md)

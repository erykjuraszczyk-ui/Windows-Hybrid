# Status implementacji i ograniczenia

## Potwierdzona baza v0.1.0–v0.2.0

- v0.1.0: CI potwierdziło kompilację ELF64, utworzenie obrazu ISO i minimalny rozruch QEMU.
- v0.2.0: wymagane przebiegi GitHub Actions zakończyły się powodzeniem w konfiguracji QEMU z `-cpu qemu64,-apic`. Testy obejmowały mapę pamięci, alokator stron, IDT, ścieżkę int3 i pięć tyknięć PIT.

Wyniki dotyczą skonfigurowanego emulatora; nie są certyfikacją pracy na fizycznym sprzęcie.

## HYBRID OS v0.3.0 — Scheduler Core

**CI PASS w QEMU** dla kodu planisty i testów kernela:
https://github.com/erykjuraszczyk-ui/Windows-Hybrid/actions/runs/40291915227

Potwierdzone markery logu:
- `HYBRID: TIMER_OK ticks=0x0000000000000005`
- `HYBRID: SCHEDULER_SELFTEST_OK dispatches=0x000000000000000B`
- `HYBRID: BOOT_OK`

Artefakt `hybrid-os-v0.3.0` został utworzony przez CI. Sukces dotyczy kontrolowanego środowiska QEMU.

Implementacja zawiera:
- stałą tablicę 8 zadań i identyfikatory zadań;
- stany UNUSED, READY, RUNNING, EXITED i FAILED;
- kooperatywne wywołanie pojedynczego kroku zadania;
- planowanie round-robin i liczniki wywołań, zakończeń oraz awarii;
- zabezpieczenie przed ponownym wejściem do planisty oraz odmowę dodania zadań poza limitem;
- test kolejności A, B, C, A, B, B, test ukończenia i test pojemności.

## Ograniczenia

- Scheduler jest kooperatywny i step-based; callback musi zwrócić sterowanie. Nie ma stosów zadań, przełączania kontekstu CPU, preempcji ani scheduler tick w ISR.
- Brak SMP, procesów Ring 3, syscall ABI, VFS, IPC oraz zarządzania pamięcią wirtualną przez kernel.
- Wyjątki korzystają z podstawowych bramek IDT; nie skonfigurowano osobnych stosów IST/TSS dla #DF ani docelowego APIC.
- PIC/PIT pozostają ścieżką testową dla prototypu.
- Testy QEMU nie dowodzą poprawności na fizycznym sprzęcie.

Każda funkcja poza zakresem wykonanego testu ma status SPECIFIED / NOT VERIFIED.

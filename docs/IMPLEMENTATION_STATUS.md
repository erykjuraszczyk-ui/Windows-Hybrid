# Status implementacji i ograniczenia

## Potwierdzona baza v0.1.0

Przebieg CI dla v0.1.0 zakończył się powodzeniem:
https://github.com/erykjuraszczyk-ui/Windows-Hybrid/actions/runs/40225741501

Potwierdzono kompilację ELF64, utworzenie ISO BIOS/UEFI i minimalny rozruch QEMU ze znacznikami KERNEL_START, FRAMEBUFFER_OK i BOOT_OK.

## Zakres v0.2.0

Gałąź feat/hybrid-os-v0.2.0 dodaje:
- mapę pamięci Limine oraz alokator fizycznych stron z bitmapami zajętości i przydziału;
- zerowanie stron, walidację zwalniania i test regresyjny podwójnego zwolnienia;
- IDT x86-64 i zestaw stubów wyjątków;
- test ścieżki #BP / int3;
- PIC/PIT i test pięciu tyknięć zegara;
- workflow QEMU sprawdzający znaczniki diagnostyczne.

**Status v0.2.0: oczekuje na wynik CI.** Kod nie jest zweryfikowany do momentu, w którym kompilacja i wszystkie testy QEMU przejdą.

## Ograniczenia

- Testy QEMU nie dowodzą zgodności z fizycznym sprzętem.
- Kernel pozostaje jednowątkowy; brak SMP, planisty, procesów Ring 3, VFS, syscall ABI i zarządzania wirtualną pamięcią przez kernel.
- Wyjątki korzystają z podstawowych bramek IDT; nie skonfigurowano osobnych stosów IST/TSS dla #DF ani APIC.
- PIC/PIT to etap testowy, nie docelowy model przerwań dla współczesnego sprzętu.
- Alokator fizyczny korzysta z HHDM Limine i rezerwuje pamięć metadanych bitmap.

Każda funkcja poza zakresem wykonanego testu ma status SPECIFIED / NOT VERIFIED.

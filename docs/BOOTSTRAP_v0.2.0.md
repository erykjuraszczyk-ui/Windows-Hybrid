# HYBRID OS v0.2.0 — Memory Core, IDT i timer

## Cel wersji

Rozszerzyć działający bootstrap v0.1.0 o podstawowe mechanizmy CPU i pamięci, nie dodając jeszcze procesów Ring 3 ani planisty.

## Zakres implementacji

- **Mapa pamięci:** odczyt mapy Limine i użycie wyłącznie pełnych stron oznaczonych jako USABLE.
- **Alokator fizycznych stron:** bitmapa stron zajętych/wolnych oraz oddzielna bitmapa stron faktycznie przydzielonych przez alokator.
- **Bezpieczeństwo pamięci:** metadane bitmap są rezerwowane, pamięć poniżej 1 MiB jest wyłączona, każda strona jest zerowana przy przydziale, podwójne zwolnienie i próba zwolnienia strony zarezerwowanej są odrzucane.
- **IDT x86-64:** bramki wyjątków CPU i wektor IRQ0; kod segmentu jest odczytywany z bieżącego CS.
- **Diagnostyka wyjątków:** logowanie wektora, kodu błędu i RIP oraz kontrolowany halt dla nieoczekiwanych wyjątków.
- **Timer PIT:** IRQ0 remapowanego PIC, cel około 100 tyknięć na sekundę; pozostałe IRQ PIC pozostają zamaskowane.
- **Testy QEMU:** samokontrola alokacji, zerowania, zwolnienia, odrzucenia podwójnego zwolnienia; wywołanie int3 do sprawdzenia IDT; oczekiwanie na pięć tyknięć PIT.

## Budowanie i testowanie

Na Ubuntu/Debian:

    sudo apt update
    sudo apt install -y build-essential binutils git curl xorriso qemu-system-x86
    make clean
    make test

Test poprawnego startu sprawdza znaczniki na porcie COM1:
- HYBRID: MEMORY_MAP_OK
- HYBRID: MEMORY_SELFTEST_OK
- HYBRID: FRAMEBUFFER_OK
- HYBRID: IDT_OK
- HYBRID: EXCEPTION_SELFTEST_OK
- HYBRID: TIMER_OK
- HYBRID: BOOT_OK

## Ograniczenia

To nadal wczesny kernel jednego rdzenia. Nie ma jeszcze planisty, TSS/IST z osobnymi stosami dla podwójnego błędu, APIC/SMP, pamięci wirtualnej zarządzanej przez kernel, VFS ani procesów użytkownika. PIC/PIT służą obecnie podstawowej ścieżce testowej. Przed użyciem na fizycznym sprzęcie konieczna jest dalsza walidacja.

# HYBRID OS v0.1.0 — instrukcja uruchomienia prototypu

## Cel

Ten etap sprawdza najniższą warstwę projektu: czy bootloader może przekazać sterowanie do kernela x86-64, czy kernel odczytuje framebuffer oraz czy pozostawia ślad diagnostyczny na porcie COM1.

To **nie** jest jeszcze pełny system operacyjny. Brakuje między innymi planisty, zarządzania pamięcią wirtualną, sterowników urządzeń, syscall ABI, procesów użytkownika, VFS i pulpitu.

## Wymagania

Na Ubuntu/Debian:

```bash
sudo apt update
sudo apt install -y build-essential binutils git curl xorriso qemu-system-x86
```

Projekt pobiera przypiętą wersję binarną Limine i nagłówek protokołu z przypiętego commita. Paczka Limine jest sprawdzana sumą SHA-256 przed rozpakowaniem.

## Budowanie obrazu ISO

W katalogu repozytorium:

```bash
make deps
make kernel
make image
```

Artefakty wynikowe:
- `build/kernel.elf` — kernel ELF64;
- `build/hybrid.iso` — obraz hybrydowy przygotowany do rozruchu BIOS/UEFI.

## Test rozruchu

```bash
make test
```

Test uruchamia QEMU bez okna graficznego i szuka w logu COM1 znacznika `HYBRID: BOOT_OK`. Emulator jest celowo zatrzymywany po 12 sekundach, ponieważ kernel prototypowy po inicjalizacji przechodzi w pętlę `hlt`. Znacznik oznacza, że kod kernela przejął sterowanie; nie potwierdza jeszcze stabilności sprzętowej ani obecności usług systemowych.

Można uruchomić system z oknem graficznym:

```bash
make run
```

Aby wyczyścić lokalne artefakty:

```bash
make clean
```

## Kryteria akceptacji v0.1.0

1. Kompilator tworzy kernel ELF64 bez błędów.
2. Obraz ISO powstaje bez błędu narzędzi bootloadera.
3. Test QEMU odnajduje `HYBRID: BOOT_OK` na COM1.
4. Jeżeli framebuffer jest dostępny i zgodny z obsługiwanym formatem RGB 32 bpp, ekran startowy pokazuje HYBRID OS.
5. Workflow CI archiwizuje ELF, ISO i log testu po udanym buildzie.

## Następny etap

Po pozytywnym CI należy dodać przerwania i obsługę wyjątków, zapisywanie informacji o pamięci z bootloadera oraz testy negatywne. Nie zaczynamy planisty ani procesów użytkownika, dopóki rozruch i diagnostyka nie są powtarzalne.

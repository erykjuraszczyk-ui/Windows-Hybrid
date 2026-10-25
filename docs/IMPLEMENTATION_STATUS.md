# Status implementacji i ograniczenia

## Status źródeł

Gałąź rozwojowa `feat/bootstrap-v0.1.0` dodaje pierwszy szkielet wykonawczy:
- kernel x86-64 w `kernel/src/main.c`;
- konfigurację bootloadera oraz skrypt linkera;
- pobieranie przypiętego Limine i przypiętego nagłówka protokołu;
- budowanie obrazu ISO BIOS/UEFI;
- test rozruchu QEMU przez diagnostykę COM1;
- workflow GitHub Actions.

## Wynik weryfikacji prototypu

**CI: PASS** dla commita `95c326407598b8f5c595d9e10235ba26ca65c3df`:
- kompilacja kernela ELF64 zakończyła się powodzeniem;
- pobrana paczka Limine przeszła kontrolę SHA-256;
- obraz ISO BIOS/UEFI został utworzony;
- QEMU zwrócił komunikaty `HYBRID: KERNEL_START`, `HYBRID: FRAMEBUFFER_OK` oraz `HYBRID: BOOT_OK`;
- artefakt `hybrid-os-bootstrap-v0.1.0` został zachowany przez GitHub Actions.

Przebieg CI: https://github.com/erykjuraszczyk-ui/Windows-Hybrid/actions/runs/40225741501

Ten wynik potwierdza minimalny rozruch w QEMU w kontrolowanym środowisku. **Nie** dowodzi jeszcze poprawnej pracy na fizycznych komputerach, stabilności sprzętowej ani bezpieczeństwa całego systemu.

## Co test boot oznacza

Znacznik `HYBRID: BOOT_OK` oznacza, że QEMU przekazał sterowanie do funkcji startowej kernela i kernel wysłał komunikat przez COM1. Nie dowodzi obsługi fizycznego sprzętu, odporności na błędy, zabezpieczeń ani poprawności kolejnych warstw.

## Nadal niewdrożone

- pełna obsługa wyjątków i IDT;
- menedżer fizycznej i wirtualnej pamięci;
- planista, procesy i syscall ABI;
- IPC, VFS oraz trwałe systemy plików;
- sterowniki wejścia, audio, sieci i grafiki poza prostym framebufferem;
- aplikacje Ring 3, GUI i kompozytor;
- konta, capabilities, sandbox i Policy Engine;
- podpisywanie wydań, Secure Boot i bezpieczny aktualizator.

Każda funkcja opisana bez kodu i wyników testów ma status **SPECIFIED / NOT VERIFIED**. Po każdym etapie należy aktualizować ten plik na podstawie konkretnego commita, kompilacji i testów.

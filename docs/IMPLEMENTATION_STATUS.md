# Status implementacji i ograniczenia

## Status źródeł

Gałąź rozwojowa `feat/bootstrap-v0.1.0` dodaje pierwszy szkielet wykonawczy:
- kernel x86-64 w `kernel/src/main.c`;
- konfigurację bootloadera oraz skrypt linkera;
- pobieranie przypiętego Limine i przypiętego nagłówka protokołu;
- budowanie obrazu ISO BIOS/UEFI;
- test rozruchu QEMU przez diagnostykę COM1;
- workflow GitHub Actions.

**Status budowania i uruchomienia:** oczekuje na wynik CI. Dopóki workflow nie przejdzie, nie oznaczamy obrazu jako zweryfikowanego.

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

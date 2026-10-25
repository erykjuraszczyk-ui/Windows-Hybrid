# Status implementacji i ograniczenia

## Co sprawdzono

Na początku porządkowania repozytorium jego historia zawierała tylko początkowy README. Nie było tam źródeł kernela ani usług pozwalających potwierdzić implementację wszystkich opisanych funkcji.

## Co wnosi archiwum

- Spis założeń architektonicznych.
- Zbiorcze specyfikacje wydań od v0.1.0 do v3.0.0.
- Model bezpieczeństwa.
- Strategię testowania i release process.
- Kryteria akceptacji dla projektowanych etapów.

## Czego ten commit nie dowodzi

Nie dowodzi istnienia ani działania:
- kernela x86-64, planisty, VMM i syscall ABI;
- bootloadera, Secure Boot lub slotów A/B;
- VFS, procesów Ring 3 i IPC;
- Surface API, compositor ani działającego VirtIO-GPU;
- kont, capabilities, sandboxa i Policy Engine;
- Package Managera, SDK ani aktualizatora;
- workflow CI, powtarzalnych buildów ani obrazu ISO.

## Status domyślny

Każda funkcja opisana bez kodu i wyników testów ma status **SPECIFIED / NOT VERIFIED**. Zmiana na IMPLEMENTED, TESTED lub RELEASED wymaga wskazania źródłowego kodu, polecenia budowania i wyników testów.

## Następny krok praktyczny

1. Utworzyć rzeczywisty szkielet źródeł i zdefiniować kontrakt bootloadera.
2. Przypiąć wersje kompilatora, linkera i zależności.
3. Zbudować minimalny obraz x86-64 UEFI.
4. Uruchomić go w QEMU i zachować log.
5. Dodawać komponenty warstwami i aktualizować status na podstawie dowodów.

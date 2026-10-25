# Windows-Hybrid — HYBRID OS

**Projekt:** natywny, hybrydowy system operacyjny x86-64.  
**Aktualny etap:** bootstrap v0.1.0 — pierwszy kernel i powtarzalny test rozruchu.

## Status — czytaj przed użyciem

Repozytorium zawiera dokumentację architektury oraz kod minimalnego prototypu kernela w gałęzi rozwojowej. **Nie jest to jeszcze kompletny system operacyjny ani zamiennik Windows/Linux.** Harmonogram procesów, pamięć wirtualna, VFS, procesy Ring 3, sterowniki urządzeń i pulpit pozostają osobnymi etapami.

Rozruch można uznać za zweryfikowany dopiero po udanym buildzie i teście QEMU w CI. Samo istnienie źródeł nie jest dowodem działania.

## Pierwszy prototyp

- kernel x86-64 w C17 z rozszerzeniami kompilatora wymaganymi dla kodu niskopoziomowego;
- protokół startowy Limine z przypiętym commitem nagłówka i sprawdzanym SHA-256 paczki bootloadera;
- ekran startowy przez framebuffer oraz komunikaty diagnostyczne COM1;
- obraz ISO przygotowywany dla rozruchu BIOS/UEFI;
- test rozruchu QEMU w GitHub Actions.

Szczegółowa instrukcja: [docs/BOOTSTRAP_v0.1.0.md](docs/BOOTSTRAP_v0.1.0.md).

## Dokumentacja projektu

- [Indeks dokumentów](docs/INDEX.md)
- [Mapa drogowa v0.1.0–v3.0.0](docs/ROADMAP.md)
- [Architektura](docs/ARCHITECTURE.md)
- [Model bezpieczeństwa](docs/SECURITY_MODEL.md)
- [Testowanie i kryteria akceptacji](docs/TESTING.md)
- [Proces kompilacji i wydań](docs/RELEASE_PROCESS.md)
- [Status implementacji](docs/IMPLEMENTATION_STATUS.md)
- [Zbiorcze specyfikacje v0.1.0–v3.0.0](docs/versions/HYBRID_OS_v0.1.0-v3.0.0.md)

## Planowane warstwy

1. Rozruch i kontrakt bootloader–kernel.
2. Obsługa wyjątków, diagnostyka oraz zarządzanie pamięcią.
3. Przerwania, zegar i planista.
4. Syscall ABI, procesy i IPC.
5. VFS i podstawowe usługi.
6. Grafika, powierzchnie, kompozytor i wejście.
7. Tożsamości, capabilities, sandbox i aktualizacje.

Każdy etap musi dostarczyć kod, polecenia budowania, testy i jawny status. Nie oznaczamy niewdrożonych funkcji jako gotowych.

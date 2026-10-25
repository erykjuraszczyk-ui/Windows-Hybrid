# Windows-Hybrid — HYBRID OS

**Projekt:** natywny system operacyjny x86-64.  
**Aktualny etap:** v0.2.0 — Memory Core, IDT i PIT, weryfikacja przez QEMU.

## Status

- **v0.1.0 — PASS w QEMU:** minimalny kernel, framebuffer oraz diagnostyka COM1.
- **v0.2.0 — w trakcie walidacji:** alokator stron fizycznych, podstawowa obsługa wyjątków i timer PIT.

Nie jest to jeszcze kompletny system operacyjny ani zamiennik Windows/Linux. Procesy Ring 3, planista, wirtualna pamięć zarządzana przez kernel, VFS, IPC i pulpit to przyszłe etapy.

## Uruchomienie

Na Ubuntu/Debian:

    sudo apt update
    sudo apt install -y build-essential binutils git curl xorriso qemu-system-x86
    make clean
    make test

Szczegóły: dokumentacja bootstrap v0.1.0 i HYBRID OS v0.2.0.

## Dokumentacja projektu

- [Indeks dokumentów](docs/INDEX.md)
- [Mapa drogowa](docs/ROADMAP.md)
- [Architektura](docs/ARCHITECTURE.md)
- [Model bezpieczeństwa](docs/SECURITY_MODEL.md)
- [Testowanie i kryteria akceptacji](docs/TESTING.md)
- [Proces kompilacji i wydań](docs/RELEASE_PROCESS.md)
- [Status implementacji](docs/IMPLEMENTATION_STATUS.md)
- [Zbiorcze specyfikacje v0.1.0–v3.0.0](docs/versions/HYBRID_OS_v0.1.0-v3.0.0.md)

# Windows-Hybrid — HYBRID OS

**Projekt:** HYBRID OS, hybrydowy system operacyjny dla architektury x86-64.

To repozytorium archiwizuje specyfikacje, architekturę, plany implementacji, kryteria akceptacji i proces wydawania systemu — od wczesnego v0.1.0 do v3.0.0.

## Ważny status projektu

Dokumentacja opisuje projektowane funkcje. **Nie potwierdza**, że wszystkie komponenty zostały zaimplementowane, zbudowane lub przetestowane. Przed oznaczeniem jakiejkolwiek funkcji jako gotowej wymagane są kod źródłowy, kompilacja i odpowiednie testy.

Przed rozpoczęciem tego porządkowania repozytorium zawierało jedynie początkowy README. Ten etap dodaje przede wszystkim dokumentację projektową, a nie kompletny kernel ani gotowy obraz systemu.

## Dokumentacja

- [Indeks dokumentów](docs/INDEX.md)
- [Mapa drogowa v0.1.0–v3.0.0](docs/ROADMAP.md)
- [Archiitektura](docs/ARCHITECTURE.md)
- [Model bezpieczeństwa](docs/SECURITY_MODEL.md)
- [Testowanie i kryteria akceptacji](docs/TESTING.md)
- [Proces kompilacji i wydań](docs/RELEASE_PROCESS.md)
- [Status implementacji](docs/IMPLEMENTATION_STATUS.md)
- [Zbiorcze specyfikacje wszystkich etapów](docs/versions/HYBRID_OS_v0.1.0-v3.0.0.md)

## Główne założenia

- Architektura x86-64 i rozruch UEFI.
- Kernel i podstawowe usługi w C17, z Assembly tam, gdzie wymaga tego architektura.
- Aplikacje w Ring 3 z wersjonowanym ABI oraz IPC.
- Surface API, okna, współdzielone bufory i wymienne backendy wyświetlania.
- VirtIO-GPU 2D jako opcjonalny backend, jeżeli przejdzie testy.
- Capabilities, sandboxing, tożsamości, polityki dostępu i audyt.
- CI, powtarzalne buildy, podpisane wydania i kontrolowane aktualizacje.
- Zweryfikowany łańcuch rozruchowy jako cel dla v3.0.0.

## Zasada projektu

Każda funkcja musi mieć udokumentowany kontrakt, obsługę błędów, testy i status. Nie traktujemy opisu, przykładowego kodu ani planowanego pliku jako dowodu implementacji.

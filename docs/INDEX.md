# Indeks dokumentacji HYBRID OS

## Dokumenty główne

- [ROADMAP.md](ROADMAP.md) — historia wersji i zależności pomiędzy etapami.
- [ARCHITECTURE.md](ARCHITECTURE.md) — warstwy systemu i granice odpowiedzialności.
- [SECURITY_MODEL.md](SECURITY_MODEL.md) — capabilities, sandbox, tożsamość, polityki i rozruch.
- [TESTING.md](TESTING.md) — testy hostowe, QEMU, fuzzing i kryteria akceptacji.
- [RELEASE_PROCESS.md](RELEASE_PROCESS.md) — CI, buildy powtarzalne, artefakty, podpisy i aktualizacje.
- [IMPLEMENTATION_STATUS.md](IMPLEMENTATION_STATUS.md) — rozdzielenie projektu od zweryfikowanego kodu.
- [Zbiorcze specyfikacje v0.1.0–v3.0.0](versions/HYBRID_OS_v0.1.0-v3.0.0.md).

## Główne etapy

| Wersja | Temat |
|---|---|
| v0.1.0–v1.4.0 | Wczesna architektura i dojrzewanie podstaw systemu |
| v1.5.0 | Surface API i usługi pulpitu |
| v1.6.0 | Shared memory i double buffering |
| v1.7.0 | Frame Scheduler i planowanie prezentacji |
| v1.8.0 | Display Backend API i VirtIO-GPU 2D |
| v1.9.0 | Odzyskiwanie sterowników i bezpieczeństwo DMA |
| v2.0.0–v2.0.1 | Stabilizacja ABI i poprawki regresji |
| v2.1.0 | Diagnostyka, logi i tracing |
| v2.2.0 | SDK i narzędzia aplikacyjne |
| v2.3.0 | Package Manager |
| v2.4.0 | Capabilities i sandboxing |
| v2.5.0 | Konta, grupy i tożsamości |
| v2.6.0 | Policy Engine |
| v2.7.0 | Hardening i fuzzing |
| v2.8.0 | CI i reproducible builds |
| v2.9.0 | Kanały wydań i podpisane aktualizacje |
| v3.0.0 | Secure Boot, Verified Boot i aktualizacje A/B |

## Jak czytać specyfikacje

Każdy etap jest planem, dopóki odpowiedni kod nie zostanie wdrożony i zweryfikowany. W przykładach API, nazw plików i poleceń mogą występować elementy projektowe, których implementacja dopiero ma powstać.

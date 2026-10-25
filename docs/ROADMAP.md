# Mapa drogowa HYBRID OS

## Profil docelowy

- Architektura: x86-64.
- Rozruch: UEFI; docelowo kontrolowany łańcuch zaufania.
- Język kernela i podstawowych usług: C17 oraz Assembly.
- Aplikacje: Ring 3, wersjonowane ABI i IPC.
- Grafika: framebuffer, Surface API, compositor, shared buffers i wymienne backendy.
- Bezpieczeństwo: capabilities, sandbox, UID/GID, Policy Engine i audyt.
- Dostarczanie: CI, powtarzalne kompilacje, podpisy i zweryfikowane aktualizacje.

## Historia wersji

| Wersja | Cel |
|---|---|
| v0.1.0 | x86-64, UEFI, C17, system natywny; grafika odłożona; nowe repozytorium |
| v0.2.0–v0.9.0 | Iteracyjne rozwijanie specyfikacji wczesnego systemu |
| v1.0.0–v1.4.0 | Dojrzewanie podstaw systemu i wejścia/pulpitu; szczegóły wymagają weryfikacji historycznych materiałów |
| v1.5.0 | Surface API i Desktop Services |
| v1.6.0 | Pamięć współdzielona i dwa bufory |
| v1.7.0 | Harmonogram klatek i programowe planowanie prezentacji |
| v1.8.0 | Abstrakcja backendu i VirtIO-GPU 2D |
| v1.9.0 | Reset sterownika, odzyskiwanie i kwarantanna DMA |
| v2.0.0 | Stabilizacja pulpitu i zamrożenie ABI |
| v2.0.1 | Zgodne wstecz poprawki stabilności |
| v2.1.0 | Diagnostyka, logi, trace i raporty awarii |
| v2.2.0 | SDK C17, runtime, szablony i narzędzia aplikacyjne |
| v2.3.0 | Pakiety .happ, zależności i Package Manager |
| v2.4.0 | Capabilities, sandbox i limity |
| v2.5.0 | Konta, grupy, sesje i prawa VFS |
| v2.6.0 | Policy Engine i administracja politykami |
| v2.7.0 | Fuzzing, fault injection i testy zagrożeń |
| v2.8.0 | CI i powtarzalne buildy |
| v2.9.0 | Kanały wydań, metadane i aktualizator |
| v3.0.0 | Secure Boot, manifesty slotów i zweryfikowane aktualizacje |

## Zależności architektoniczne

1. Rozruch, pamięć i przerwania poprzedzają usługi wyższego poziomu.
2. VMM i IPC muszą być stabilne przed shared memory i sandboxingiem.
3. Surface API poprzedza podwójne buforowanie i Frame Scheduler.
4. VirtIO-GPU wymaga działających PCI, virtqueue i zarządzania DMA.
5. SDK wymaga loadera ELF oraz stabilnego kontraktu ABI.
6. Package Manager wymaga VFS i bezpiecznej walidacji pakietów.
7. Tożsamości, capabilities i Policy Engine muszą być egzekwowane w punktach wykonania operacji.
8. CI i powtarzalne buildy poprzedzają zaufane wydania.
9. Verified Boot wymaga jawnego łańcucha zaufania, ochrony kluczy i procedury odzyskiwania.

## Reguła wydania

Kolejny numer nie zamyka poprzedniego etapu automatycznie. Funkcja jest gotowa dopiero po implementacji, kompilacji i zaliczeniu właściwych testów.

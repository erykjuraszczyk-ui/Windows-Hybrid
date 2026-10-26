# Indeks dokumentacji HYBRID OS

## Dokumenty główne

- [BOOTSTRAP v0.1.0](BOOTSTRAP_v0.1.0.md) — kompilacja kernela, ISO i pierwszy test QEMU.
- [HYBRID OS v0.2.0](BOOTSTRAP_v0.2.0.md) — Memory Core, IDT, wyjątki i PIT.
- [HYBRID OS v0.3.0](BOOTSTRAP_v0.3.0.md) — kooperatywny Scheduler Core i test round-robin.
- [HYBRID OS v0.4.0](BOOTSTRAP_v0.4.0.md) — preemptive scheduler, ramki kontekstu i osobne stosy zadań.
- [ROADMAP.md](ROADMAP.md) — wersje oraz zależności pomiędzy etapami.
- [ARCHITECTURE.md](ARCHITECTURE.md) — warstwy systemu i granice odpowiedzialności.
- [SECURITY_MODEL.md](SECURITY_MODEL.md) — capabilities, sandbox, tożsamość, polityki i rozruch.
- [TESTING.md](TESTING.md) — testy hostowe, QEMU, fuzzing i kryteria akceptacji.
- [RELEASE_PROCESS.md](RELEASE_PROCESS.md) — CI, buildy powtarzalne, artefakty, podpisy i aktualizacje.
- [IMPLEMENTATION_STATUS.md](IMPLEMENTATION_STATUS.md) — rozdzielenie projektu od zweryfikowanego kodu.
- [Zbiorcze specyfikacje v0.1.0–v3.0.0](versions/HYBRID_OS_v0.1.0-v3.0.0.md).

## Główne etapy

| Wersja | Temat |
|---|---|
| v0.1.0 | Minimalny kernel i boot smoke test |
| v0.2.0 | Alokator stron, IDT, wyjątki, PIT |
| v0.3.0 | Kooperatywny Scheduler Core, tablica zadań, round-robin |
| v0.4.0 | Preempcja w Ring 0, przełączanie ramek IRQ i stosy zadań |
| v0.3.0+ | Rozszerzanie pamięci, przerwań i usług kernela |
| v1.5.0 | Surface API i usługi pulpitu |
| v1.6.0 | Shared memory i double buffering |
| v1.7.0 | Frame Scheduler i planowanie prezentacji |
| v1.8.0 | Display Backend API i VirtIO-GPU 2D |
| v1.9.0 | Odzyskiwanie sterowników i bezpieczeństwo DMA |
| v2.0.0–v3.0.0 | Stabilizacja ABI, SDK, sandbox, pakiety, CI, Verified Boot |

Każda funkcja wymaga kodu, testów i wyników przed oznaczeniem jako gotowa.

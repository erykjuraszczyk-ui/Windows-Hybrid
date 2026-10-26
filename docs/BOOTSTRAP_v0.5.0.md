# HYBRID OS v0.5.0 — Task Lifecycle, TSS/IST

## Cel

Dodać własne tablice deskryptorów x86-64 i bardziej bezpieczny cykl życia zadań Ring 0, pozostając zgodnym z istniejącą ścieżką rozruchu Limine i testami QEMU.

## GDT/TSS/IST

- Instalowana jest własna GDT z selektorami kodu Ring 0 (0x08), danych Ring 0 (0x10) i TSS (0x18).
- TSS64 ma 104 bajty, RSP0 oraz cztery niezależne stosy IST.
- Każdy stos TSS/IST pochodzi z osobnej strony Memory Core.
- IDT kieruje #DF do IST1, #GP do IST2, #PF do IST3, a NMI do IST4.
- Samotest kontroluje aktywną GDT, TR oraz indeksy IST w załadowanej IDT.

## Task Lifecycle

- `scheduler_task_create` tworzy zadanie z własną stroną stosu i początkową ramką `iretq`.
- Wejście zadania działa z IF wyłączonym do momentu przechwycenia jego indeksu, a potem uruchamia się z przerwaniami włączonymi.
- Powrót z funkcji zadania ustawia stan EXITED.
- `scheduler_task_reap` odmawia odzyskania stosu, jeżeli scheduler nadal działa na tym stosie lub zadanie nie jest EXITED/FAILED.
- Po bezpiecznym reaping strona jest zwalniana, a slot może zostać użyty ponownie. Identyfikatory pozostają monotoniczne, więc nie są ponownie przypisywane staremu zadaniu.
- Test preempcji wykorzystuje dwie skończone funkcje; sprawdza stany EXITED, liczniki obu zadań, przełączenie kontekstu i odzyskanie stron.

## Test

    make clean
    make test

Wymagane markery:
- `HYBRID: TSS_OK`
- `HYBRID: IDT_OK`
- `HYBRID: IST_OK`
- `HYBRID: EXCEPTION_SELFTEST_OK`
- `HYBRID: TASK_LIFECYCLE_SELFTEST_OK`
- `HYBRID: PREEMPTIVE_SELFTEST_OK`
- `HYBRID: BOOT_OK`

## Ograniczenia

Test IST potwierdza konfigurację GDT/TSS/IDT, ale nie wywołuje wymuszonego #DF jako fault-injection. Kernel jest jednordzeniowy i wszystkie zadania nadal działają w Ring 0. Brak Ring 3, SMP, FPU/SSE/AVX save/restore, blokowania/wybudzania i sandboxa procesów. CI PASS będzie dotyczył wyłącznie skonfigurowanego QEMU.

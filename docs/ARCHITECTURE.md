# Architektura HYBRID OS

## 1. Profil docelowy

- Architektura procesora: x86-64.
- Rozruch: UEFI.
- Język kernela i podstawowych usług: C17 oraz Assembly tam, gdzie wymaga tego architektura.
- Aplikacje: Ring 3, stabilne ABI i kontrolowane IPC.
- Pierwsze środowisko testowe: QEMU + UEFI/OVMF.
- Grafika: framebuffer jako podstawa, następnie Surface API, współdzielone bufory i opcjonalny VirtIO-GPU 2D.
- Bezpieczeństwo: capabilities, sandboxing, tożsamości, polityki i audyt.
- Dostarczanie: CI, powtarzalne buildy, podpisane artefakty i zweryfikowane aktualizacje.

Są to założenia projektowe. Nie stanowią potwierdzenia, że wszystkie komponenty istnieją w kodzie.

## 2. Warstwy

1. **UEFI i bootloader** — inicjalizacja rozruchu, przekazanie parametrów i uruchomienie kernela.
2. **Kernel x86-64** — wyjątki, przerwania, planista, VMM, alokator pamięci, syscall ABI i podstawowe sterowniki.
3. **IPC i VFS** — komunikacja procesów i abstrakcja plików.
4. **Usługi systemowe** — init, logd, desktopd, displayd, pkgd, auth/session/account i policyd.
5. **SDK/runtime** — biblioteki C oraz GUI dla aplikacji.
6. **Aplikacje Ring 3** — procesy ograniczone tożsamością, capabilities i sandboxem.
7. **Build/release** — toolchain, testy, artefakty, podpisy i aktualizacje.

## 3. Kernel i procesy

Kernel powinien być właścicielem mechanizmów, których nie można bezpiecznie egzekwować w zwykłej usłudze: mapowania stron, przełączania kontekstu, obsługi przerwań, uprawnień do pamięci i podstawowego cyklu życia procesów.

Operacje aplikacji powinny mieć jawne kody błędów oraz walidację argumentów. Nie wolno używać adresu wirtualnego procesu jako adresu, który inny proces lub urządzenie może bezpośrednio dereferencjonować.

## 4. IPC i VFS

Każdy komunikat IPC powinien określać wersję, rozmiar, opcode i identyfikator żądania. Odbiorca waliduje całą wiadomość, w tym długości, odwołania do obiektów i uprawnienia.

VFS zapewnia jednolity kontrakt operacji plikowych. Prawa dostępu muszą być egzekwowane na obiektach, a nie tylko w warstwie interfejsu użytkownika. Jeżeli konkretny system plików nie zachowuje trwałych UID/GID i trybów dostępu, ograniczenia te muszą być jawnie opisane.

## 5. Grafika

Stos grafiki składa się z:
- Surface API w libhybridgui;
- obiektów powierzchni i kontrolowanego współdzielenia pamięci;
- displayd/compositor;
- Frame Scheduler;
- Display Backend API;
- framebuffer backend;
- opcjonalnego VirtIO-GPU 2D.

Należy odróżnić zakończenie rysowania aplikacji, zwolnienie bufora, ukończenie kompozycji, zakończenie kopiowania do framebufferu i rzeczywistą prezentację zsynchronizowaną z VBlank. Nie należy deklarować sprzętowego VSync bez wsparcia konkretnego backendu.

## 6. Usługi i awarie

Usługi mają jasno opisane odpowiedzialności, timeouty, kody błędów i sposób sprzątania po zakończeniu procesu. Awaria opcjonalnego komponentu nie powinna automatycznie zawieszać całego systemu.

W przypadku urządzenia DMA nie wolno zwalniać pamięci, dopóki nie jest pewne, że urządzenie przestało z niej korzystać. Jeżeli reset nie został potwierdzony, zasoby pozostają w kwarantannie.

## 7. Bezpieczeństwo

Autoryzacja jest egzekwowana przy każdej chronionej operacji. Manifest deklaruje wymagania, ale nie nadaje sam z siebie uprawnień. UID/GID, capabilities i sandbox to odrębne, współpracujące warstwy.

## 8. Weryfikacja

Architektura jest uznawana za wdrożoną dopiero po dostarczeniu kodu, wyników builda i testów. Testy QEMU nie są dowodem obsługi wszystkich komputerów fizycznych; dla sprzętu docelowego wymagana jest osobna walidacja.

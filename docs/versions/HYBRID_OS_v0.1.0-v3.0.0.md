# HYBRID OS — zbiorcze specyfikacje v0.1.0–v3.0.0

**Rodzaj dokumentu:** archiwum ujednoliconych specyfikacji i planów  
**Status:** projekt / not verified  
**Ważne:** dokument konsoliduje zachowane ustalenia. Nie jest dosłownym zrzutem każdej wcześniejszej wiadomości i nie potwierdza implementacji ani wyników testów.

## 1. v0.1.0 — bazowy projekt systemu

### Ustalone wybory
- Architektura: x86-64.
- Rozruch: UEFI.
- Język: C17.
- Zakres: natywny system operacyjny.
- Grafika: odłożona na późniejszy etap.
- Repozytorium: nowe.

### Cel
Rozpocząć od minimalnego, testowalnego systemu uruchamianego w QEMU. Pierwszy etap powinien koncentrować się na kontrakcie bootloadera, strukturze kernela, wyjątkach, przerwaniach, pamięci i diagnostyce. Pulpit nie powinien być warunkiem powodzenia wczesnego rozruchu.

### Kryteria
- Zdefiniować interfejs bootloader–kernel.
- Ustalić toolchain i linker script dla x86-64.
- Zbudować minimalny obraz rozruchowy.
- Uruchomić go w QEMU i zachować log rozruchu.
- Oddzielić specyfikację od funkcji faktycznie zaimplementowanych.

## 2. v0.2.0–v0.9.0 — iteracje wczesnego projektu

W dostępnej historii widać kolejne iteracje numerów v0.2.0–v0.9.0, lecz pełne oryginalne teksty tych specyfikacji nie były dostępne do odtworzenia przy tworzeniu repozytorium. Dlatego nie przypisujemy każdej wersji niezweryfikowanych funkcji.

Dalsza praca powinna uzupełniać tę sekcję ze starych dokumentów lub commitów, jeśli zostaną odzyskane. W każdym przypadku należy zapisać cel, zależności, pliki, testy i status wydania.

## 3. v1.0.0–v1.4.0 — podstawy systemu i pulpitu

Dostępne notatki wskazują na dojrzewanie podstaw systemu i interfejsu wejścia/pulpitu. W kontekście v1.5.0 jako wcześniejszą bazę wymieniono zarządzanie oknami, kursor i wejście myszy. Nie zachowano tu wystarczającej ilości źródłowego tekstu, aby wiarygodnie przypisać szczegółowy zakres każdej z wersji.

Warunek dalszej pracy: audyt rzeczywistego kodu przed traktowaniem tych funkcji jako istniejących.

## 4. v1.5.0 — Surface API & Desktop Services

### Cel
Ustalić publiczny interfejs aplikacji do tworzenia okien, powierzchni graficznych, rysowania 2D, prezentacji i odbioru zdarzeń.

### Architektura
Aplikacje Ring 3 używają libhybridgui i komunikują się z usługami przez wersjonowane IPC. desktopd zarządza oknami, fokusem i wejściem, natomiast displayd zarządza powierzchniami, rysowaniem i kompozycją. Aplikacje nie otrzymują bezpośredniego dostępu do framebufferu.

### Zakres API
- Połączenie i rozłączenie z usługą pulpitu.
- Utworzenie, pokazanie, ukrycie i zamknięcie okna.
- Rysowanie tekstu, linii i prostokątów.
- Zatwierdzenie powierzchni i zgłaszanie dirty regions.
- Odbieranie zdarzeń myszy, klawiatury i cyklu życia okna.
- Obsługa błędów, nieprawidłowych uchwytów i zakończenia procesu.

### Kryteria
- Żądania IPC są walidowane przed zmianą stanu.
- Nieprawidłowa paczka rysowania nie jest wykonywana częściowo.
- Aplikacja A nie może modyfikować powierzchni aplikacji B.
- Zasoby procesu są zwalniane po jego zakończeniu.

## 5. v1.6.0 — Shared Memory & Double Buffering

### Cel
Zmniejszyć koszty przekazywania obrazu przez kontrolowaną pamięć współdzieloną i dwa bufory na powierzchnię.

### Projekt
Kernelowy Shared Memory Manager zarządza stronami, mapowaniami, prawami, referencjami i unieważnianiem. Aplikacja może zapisywać tylko do bufora w stanie CLIENT_WRITABLE. Po commit system odbiera jej prawo zapisu, a displayd korzysta z kontrolowanego mapowania tylko do odczytu. Po zakończeniu odczytu bufor jest zwalniany i zgłaszane jest BUFFER_RELEASED.

### Zasady bezpieczeństwa
- Rozmiary są sprawdzane pod kątem przepełnień.
- Strony są zerowane przed udostępnieniem.
- Uchwyt nie jest adresem fizycznym.
- Nie używa się wskaźników jednej przestrzeni adresowej jako uniwersalnych adresów IPC.
- Stron nie wolno zwolnić przy aktywnym mapowaniu lub referencji.
- Jeśli urządzenie DMA może wciąż korzystać z pamięci, zasób nie może zostać zwolniony.

### Kryteria
- Brak zapisu do bufora podczas odczytu przez displayd.
- Prawidłowy cykl mapowania, commit, release i zamknięcia.
- Kontrolowane zachowanie przy zajętych obu buforach i braku pamięci.

## 6. v1.7.0 — Display Synchronization & Frame Scheduler

### Cel
Dodać kolejkę klatek, planowanie terminów, polityki prezentacji i metryki.

### Projekt
Frame Scheduler w displayd obsługuje kolejki FIFO, MAILBOX i IMMEDIATE. Wykorzystuje zegar monotoniczny, ogranicza liczbę oczekujących klatek i mierzy opóźnienie kolejki, czas kompozycji, czas backendu, pominięte terminy i odrzucone klatki.

Należy rozróżniać:
- przyjęcie żądania;
- zwolnienie bufora;
- zakończenie kompozycji;
- zakończenie kopiowania do framebufferu;
- faktyczną prezentację zsynchronizowaną z VBlank.

### Kryteria
- Zegar monotoniczny nie cofa się w normalnej pracy.
- Kolejki są ograniczone.
- Scheduler nie nadrabia bez końca opóźnionych klatek.
- Nie deklaruje się VSync bez wsparcia sterownika.

## 7. v1.8.0 — Display Driver Abstraction & VirtIO-GPU 2D

### Cel
Odłączyć displayd od konkretnego urządzenia i dodać backend VirtIO-GPU w trybie 2D.

### Projekt
Display Backend API zapewnia wspólny interfejs. Framebuffer pozostaje backendem podstawowym lub awaryjnym. VirtIO-GPU komunikuje się przez VirtIO PCI i virtqueue. Minimalna ścieżka 2D obejmuje odczyt informacji o wyjściu, utworzenie zasobu 2D, przypisanie backing memory, ustawienie scanoutu, transfer danych i odświeżenie regionu.

### Ograniczenia
- Nie zakładać stałych adresów BAR ani fizycznych adresów.
- Negocjować wyłącznie obsługiwane funkcje.
- Walidować odpowiedzi urządzenia.
- Nie utożsamiać trybu 2D z akceleracją 3D.
- Nie zwalniać pamięci DMA, zanim urządzenie nie przestanie z niej korzystać.

### Kryteria
- Urządzenie zostaje wykryte i zainicjalizowane.
- Testowy obraz pojawia się w QEMU.
- Brak urządzenia nie blokuje startu systemu.
- Obsługa błędów i timeoutów ma określoną semantykę.

## 8. v1.9.0 — Display Reliability & Driver Recovery

### Cel
Odzyskiwać błędy backendu i VirtIO-GPU bez przedwczesnego zwalniania zasobów.

### Maszyna stanów
DISPLAY_ONLINE → DISPLAY_DEGRADED → DISPLAY_QUIESCING → DISPLAY_RESETTING → DISPLAY_REINITIALIZING → DISPLAY_RESTORING → ONLINE albo FALLBACK/FAILED.

### Procedura
- Zablokować nowe żądania.
- Wstrzymać Frame Scheduler.
- Oznaczyć wynik poleceń w toku jako nieznany.
- Zresetować urządzenie i czekać na potwierdzenie.
- Dopiero po bezpiecznym zatrzymaniu odzyskać kolejki i zasoby.
- Ponownie skonfigurować kolejki, zasoby 2D, scanout i pełną kompozycję.
- Jeśli reset się nie potwierdzi, pozostawić niepewne zasoby DMA w kwarantannie.

### Kryteria
- Stare odpowiedzi nie zmieniają nowych generacji zasobów.
- Timeout nie jest traktowany jako dowód, że komenda nie została wykonana.
- Nieudany reset kończy się kontrolowanym DISPLAY_FAILED albo sprawdzonym fallbackiem.

## 9. v2.0.0 — Stable Desktop Release

### Cel
Zamrozić publiczne ABI i ujednolicić pulpit, procesy, VFS, IPC i diagnostykę.

### Zakres
- Dokumentacja syscall ABI i protokołów IPC.
- Stabilne Surface API, Window API i Input API.
- Testy regresji dla pamięci, procesów, VFS i sterowników.
- Matryca obsługiwanych środowisk.
- Lista znanych ograniczeń i proces release candidate.

### Kryteria
- System uruchamia się w zdefiniowanej konfiguracji.
- Wymagane testy przechodzą.
- Nie deklaruje się funkcji niezweryfikowanych.
- Artefakty, wyniki testów i wersje narzędzi są zachowane.

## 10. v2.0.1 — Stability & Compatibility Patch

### Cel
Naprawiać rzeczywiste błędy bez niekompatybilnych zmian ABI.

### Proces
Każdy błąd ma reprodukcję, ocenę powagi, poprawkę, test regresyjny i wynik ponownej weryfikacji. Priorytet mają błędy pamięci, autoryzacji, cyklu życia zasobów, IPC i sterowników.

### Kryteria
- Brak niezamierzonych zmian ABI.
- Wszystkie poprawki mają testy.
- Testy nieuruchomione nie są oznaczane jako zaliczone.

## 11. v2.1.0 — Developer Experience & System Diagnostics

### Cel
Dodać uporządkowane logi, tracing, raporty awarii i CLI diagnostyczne.

### Zakres
- Logi strukturalne z timestampem monotonicznym, podsystemem, kodem i poziomem.
- Ograniczone bufory i liczniki utraconych zdarzeń.
- Opcjonalny tracing planisty, pamięci, IPC, VFS, pulpitu i sterowników.
- Raporty panic niezależne od działającej grafiki.
- hybrid-diag do odczytu stanu systemu i eksportu raportu.

### Kryteria
- Diagnostyka nie blokuje przerwań.
- Poufne dane nie trafiają do logów.
- Raporty wskazują rzeczywiste wartości runtime.

## 12. v2.2.0 — Developer SDK & Application Toolkit

### Cel
Pozwolić programistom tworzyć aplikacje bez modyfikowania jądra.

### Zakres
- SDK C17, publiczne nagłówki i kody błędów.
- Runtime C i loader ELF64.
- libhybridgui i przykładowe aplikacje.
- hybrid-app: new, build, test, package, inspect i doctor.
- Manifest aplikacji oraz pakiet .happ.

### Kryteria
- Konsolowa aplikacja referencyjna buduje się i działa.
- Aplikacja GUI korzysta z Surface API.
- Toolchain jest targetowany na HYBRID OS, a nie przypadkowo na Linux hosta.
- Dokumentacja nie deklaruje pełnej libc, jeśli dostępny jest tylko jej podzbiór.

## 13. v2.3.0 — Application Lifecycle & Package Manager

### Cel
Zarządzać instalacją, zależnościami, aktualizacją i usuwaniem aplikacji.

### Zakres
- pkgd i hybrid-pkg.
- Manifest, walidacja pakietu, rejestr i resolver zależności.
- Bezpieczne wypakowywanie z kontrolą ścieżek i rozmiarów.
- Staging, dziennik transakcji, commit i rollback.
- Zachowanie danych użytkownika po usunięciu aplikacji.

### Kryteria
- Błędne pakiety są odrzucane przed zmianą aktywnej instalacji.
- Hash jest sprawdzany względem zaufanych metadanych.
- Awaria nie pozostawia niekontrolowanie częściowo aktywnej instalacji.

## 14. v2.4.0 — Application Permissions & Sandboxing

### Cel
Egzekwować capabilities, sandbox i limity zasobów w punktach wykonania operacji.

### Zakres
- Capabilities wiążące prawa z obiektami.
- Ograniczony kontekst procesu przed startem.
- Kontrola VFS, IPC, pamięci, usług i powierzchni.
- Limity pamięci, uchwytów, kolejek i okien.
- Audyt odmów dostępu.

### Kryteria
- Brak capability oznacza odmowę.
- Aplikacja nie ma bezpośredniego dostępu do framebufferu.
- Brak wymaganej polityki nie prowadzi do uruchomienia bez izolacji.

## 15. v2.5.0 — User Accounts, Identity & Access Policy

### Cel
Dodać lokalne konta, grupy, sesje, poświadczenia procesów i prawa plików.

### Zakres
- UID/GID oraz grupy dodatkowe.
- accountd, authd i sessiond lub odpowiadające im moduły.
- Weryfikacja haseł sprawdzonym algorytmem; brak przechowywania haseł jawnie.
- Właściciele obiektów VFS i podstawowe prawa owner/group/other.
- Wylogowanie i cleanup procesu.
- Audyt zmian kont oraz odmów dostępu.

### Ograniczenia
Jedna aktywna sesja interaktywna w pierwszym zakresie. Dla FAT32 nie należy deklarować natywnych trwałych metadanych UID/GID bez warstwy VFS zweryfikowanej testami.

## 16. v2.6.0 — System Policy Engine & Security Administration

### Cel
Wprowadzić scentralizowaną ocenę reguł dostępu.

### Zakres
- policyd, parser i walidator.
- Deklaratywny format polityki.
- Domyślna odmowa, jawne allow/deny i kody przyczyn.
- Punkty egzekwowania w kernelu, VFS, IPC i usługach.
- Dry-run, wersjonowanie, atomowa aktywacja i rollback.
- Audyt decyzji powiązanych z generacją polityki.

### Kryteria
- Brak reguły powoduje odmowę.
- Błędna polityka nie jest aktywowana.
- Zwykła aplikacja nie może zmienić globalnej polityki.
- Cache decyzji nie omija nowej generacji.

## 17. v2.7.0 — Security Hardening & Threat Testing

### Cel
Sprawdzić odporność parserów, autoryzacji, pamięci i usług.

### Zakres
- Fuzzing parserów polityk, manifestów, IPC, ELF i ścieżek VFS.
- Hostowe sanitizery i analiza statyczna.
- Testy negatywne autoryzacji.
- Fault injection dla braku pamięci, timeoutów i awarii usług.
- Korpus regresji, klasyfikacja błędów i raport bezpieczeństwa.

### Kryteria
- Brak otwartych błędów krytycznych i wysokich w zakresie wydania.
- Każde znalezisko ma reprodukcję i test regresyjny.
- Raport rozróżnia PASS, FAIL i NOT_RUN.

## 18. v2.8.0 — Continuous Integration & Reproducible Builds

### Cel
Automatyzować kontrolę jakości i umożliwić odtworzenie artefaktów.

### Zakres
- Workflow CI dla kompilacji, testów i QEMU.
- Przypięty toolchain i zależności.
- Kontrolowane timestampy oraz ścieżki.
- Dwa czyste buildy i porównanie artefaktów.
- SHA-256, manifest i provenance.
- Minimalne uprawnienia zadań CI; oddzielenie testów od podpisywania.

### Kryteria
- Test FAIL blokuje wydanie.
- Różne artefakty wymagają wyjaśnienia.
- Provenance wskazuje commit, buildera i parametry.
- Nie twierdzi się, że powtarzalność sama dowodzi bezpieczeństwa.

## 19. v2.9.0 — Release Engineering & Update Channels

### Cel
Zarządzać kanałami development, beta i stable, podpisami, metadanymi i aktualizacjami.

### Zakres
- Manifest wydania i zaufane metadane.
- Weryfikacja podpisów, hashy, rozmiarów, wersji i ABI.
- Ochrona przed rollback i nieaktualnymi metadanymi.
- Klient hybrid-update.
- Staging, transakcje, historia i odzyskiwanie.
- Oddzielenie aktualizacji aplikacji od aktualizacji systemu.

### Kryteria
- Zmieniony artefakt jest odrzucany.
- Promocja kanału stable wskazuje ten sam przetestowany artefakt.
- Aktualizacja nie staje się aktywna przed weryfikacją.
- Rollback jest deklarowany tylko, gdy rzeczywiście działa.

## 20. v3.0.0 — Secure Boot & Verified System Update

### Cel
Powiązać zaufanie od firmware UEFI przez menedżera rozruchu i Limine do kernela, initramfs i systemu aktualizacji.

### Docelowy łańcuch
UEFI Secure Boot → podpisany hybrid-bootmgr.efi → weryfikacja manifestu slotu → zatwierdzony Limine i konfiguracja → kernel/initramfs → test gotowości systemu → zatwierdzenie slotu.

### Aktualizacje A/B
Nowa wersja jest zapisywana do nieaktywnego slotu. Po weryfikacji uruchamia się jako kandydat. System potwierdza gotowość dopiero po przejściu kontroli usług. Po powtarzającym się niepowodzeniu wraca do poprzedniego zatwierdzonego slotu.

### Kryteria
- Nieprawidłowe podpisy, manifesty lub hashe blokują użycie.
- Przerwanie aktualizacji nie usuwa ostatniego sprawnego slotu.
- Rotacja kluczy ma zaufaną ścieżkę.
- Zaufane metadane i ochrona przed cofnięciem wersji są testowane.
- Ochrona przed odtworzeniem starego całego dysku nie jest deklarowana bez dodatkowego, zaufanego stanu.

## 21. Wspólne wymagania dla wszystkich wydań

Każdy etap powinien zawierać:
- cel i zakres;
- architekturę i granice odpowiedzialności;
- pliki, które rzeczywiście dodano lub zmieniono;
- polecenia budowania odtworzone z repozytorium;
- testy i kryteria akceptacji;
- znane ograniczenia;
- status SPECIFIED, IMPLEMENTED, TESTED lub RELEASED.

Nie należy podawać planowanych nazw plików, przykładowych komend i pseudokodu jako dowodu, że funkcje działają.

## 22. Następny krok

Najpierw należy utworzyć minimalny, rzeczywiście budujący się szkielet systemu, ustalić boot contract, przypiąć toolchain i dodać test rozruchu QEMU. Następne elementy powinny być implementowane w kolejności zależności z roadmapy, a dokumentacja aktualizowana na podstawie rzeczywistych wyników.

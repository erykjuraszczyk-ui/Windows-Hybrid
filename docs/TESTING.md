# Strategia testowania HYBRID OS

## 1. Poziomy testów

### Testy hostowe
Testują czyste funkcje C: parsery, walidatory, resolver zależności, formaty i logikę polityk. W miarę zgodności używaj analizy statycznej oraz AddressSanitizer i UndefinedBehaviorSanitizer.

### Testy jednostkowe
Sprawdzają małe moduły, warunki graniczne i kody błędów. Każda naprawa błędu powinna mieć test regresyjny.

### Testy integracyjne QEMU
Weryfikują rozruch, kernel, procesy, IPC, VFS, pulpit, sterowniki i odzyskiwanie w powtarzalnej konfiguracji x86-64 UEFI.

### Fuzzing
Cele obejmują parsery polityk, manifestów pakietów, IPC, ELF, ścieżek VFS i odpowiedzi urządzeń. Zachowuj każdy znaleziony przypadek jako materiał regresyjny.

### Testy wydania
Uruchamiaj wymagany zestaw na konkretnym commicie. Zapisuj konfigurację, wersje toolchainu, parametry QEMU/OVMF i logi.

## 2. Statusy testów

Każdy przypadek ma status PASS, FAIL, NOT_RUN albo NOT_IMPLEMENTED. Test pominięty lub dotyczący funkcji niewdrożonej nie może być przedstawiony jako PASS.

## 3. Bezpieczeństwo

- Brak reguły polityki powoduje odmowę.
- Obce i nieaktualne uchwyty są odrzucane.
- Błędne IPC nie wywołuje częściowej operacji.
- Nieprawidłowe manifesty i ścieżki archiwum są odrzucane.
- Limity zasobów są egzekwowane.
- Zakończenie procesu usuwa jego mapowania i zasoby.
- DMA nie jest zwalniane przedwcześnie.
- Zmiana podpisanego artefaktu powoduje błąd weryfikacji.
- Nieudana aktualizacja nie usuwa ostatniej sprawnej wersji.

## 4. Reproducible builds

Dwa niezależne buildy z tego samego źródła i kontrolowanego toolchainu porównuje się bajt po bajcie. Różnice należy wyjaśnić, a nie ignorować. Powtarzalność nie dowodzi braku błędów bezpieczeństwa.

## 5. Fault injection

Scenariusze obejmują brak pamięci, timeout IPC, awarię usługi, nieudany reset GPU, błąd zapisu rejestru i przepełnienie bufora audytu. Używaj atrap oraz izolowanych buildów testowych. Nie wprowadzaj niekontrolowanych awarii na systemie hosta.

## 6. Testy rozruchu i aktualizacji

Testuj prawidłowe oraz nieprawidłowe podpisy, zmienione hashe, przestarzałe metadane, brak miejsca, przerwanie operacji i nieudane pierwsze uruchomienie nowej wersji. Stan poprzedniej sprawnej wersji musi pozostać dostępny zgodnie z projektem odzyskiwania.

## 7. Kryterium zamknięcia błędu

Każdy błąd powinien mieć minimalną reprodukcję, ocenę powagi, poprawkę, test regresyjny i wynik ponownej weryfikacji. Nie usuwaj danych reprodukujących błąd z korpusu fuzzingu po naprawie.

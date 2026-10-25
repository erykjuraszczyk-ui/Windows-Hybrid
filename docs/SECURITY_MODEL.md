# Model bezpieczeństwa HYBRID OS

## 1. Zasady nadrzędne

- **Default deny:** brak jawnej reguły zezwalającej oznacza odmowę.
- **Least privilege:** proces otrzymuje minimalny zestaw praw.
- **Enforcement at operation:** jądro, VFS i usługi sprawdzają dostęp w miejscu wykonania operacji.
- **Isolation:** uchwyty, mapowania i kolejki mają właściciela i cykl życia.
- **Validation:** wszystkie długości, rozmiary, identyfikatory i typy są weryfikowane przed użyciem.
- **Safe failure:** błąd weryfikacji nie może rozszerzać uprawnień.
- **Audit:** zmiany polityk i odmowy dostępu są zapisywane bez rejestrowania sekretów.

## 2. Tożsamości i sesje

Model przewiduje UID/GID, grupy, session ID oraz poświadczenia procesu. Zaufany mechanizm uruchamiania ustala tożsamość procesu. Zwykła aplikacja nie może sama podnieść UID/GID ani dopisać sobie capabilities.

Uwierzytelnienie potwierdza tożsamość, natomiast autoryzacja decyduje, czy dana operacja jest dozwolona. Są to oddzielne kroki.

## 3. Capabilities i sandbox

Capability daje kontrolowany dostęp do określonego zasobu i zakresu operacji. Sandbox dodatkowo ogranicza dostęp do plików, IPC, pamięci i usług. Prawa obiektu, capabilities oraz polityka sandboxa mogą dodatkowo ograniczać dostęp; jedna warstwa nie może potajemnie omijać innej.

Sprawdzanie uprawnień tylko w SDK nie jest zabezpieczeniem. Kernel i każda usługa obsługująca zasób muszą zweryfikować żądanie.

## 4. Policy Engine

Polityki mają wersjonowany, ściśle walidowany format. Błędnej polityki nie wolno aktywować. Zmiana tworzy nową generację, a decyzje i cache muszą respektować unieważnianie po zmianie reguł.

Jeśli silnik nie może wiarygodnie rozstrzygnąć decyzji, operacja objęta polityką kończy się odmową lub kontrolowanym błędem, nigdy automatycznym zezwoleniem.

## 5. Pakiety

Pakiet musi przejść kontrolę manifestu, architektury, ABI, rozmiarów, ścieżek, zależności i zaufanych metadanych przed instalacją. Sam hash nie potwierdza autentyczności, jeżeli źródło oczekiwanego hasha nie jest zaufane.

Nie uruchamiaj skryptów instalacyjnych pochodzących z niezaufanego pakietu. Najpierw używaj stagingu i transakcji. Dane użytkownika rozdziel od plików aplikacji.

## 6. Tożsamości plików

VFS powinien kontrolować prawa właściciela, grupy i pozostałych użytkowników. Uprawnienia do katalogów nadrzędnych, otwarte uchwyty i sandbox muszą zostać uwzględnione. Dla FAT32 i innych systemów bez natywnych UID/GID nie należy deklarować trwałych metadanych bez dodatkowej, zweryfikowanej warstwy.

## 7. Kryptografia i hasła

Nie twórz własnych algorytmów kryptograficznych. Weryfikatory haseł wymagają sprawdzonego algorytmu, losowej soli i bezpiecznego źródła losowości. Nie zapisuj haseł, kluczy prywatnych ani sekretów w logach.

## 8. Pamięć i DMA

Kontroluj przepełnienia rozmiarów, prawa mapowań, generacje uchwytów i referencje. Pamięć udostępniona DMA nie może zostać zwolniona, dopóki urządzenie nie zostało bezpiecznie zatrzymane lub zresetowane. Niepotwierdzony reset oznacza kwarantannę zasobów.

## 9. Secure Boot i aktualizacje

Zaufanie musi obejmować każdy etap rozruchu oraz wszystkie ładowane pliki. Podpis pierwszego pliku nie dowodzi integralności pozostałych komponentów. Podpisane metadane muszą określać artefakty, a aktualizator musi chronić przed podmianą, cofnięciem wersji i niespójnością zestawu.

Ochrona przed odtworzeniem starego obrazu całego dysku wymaga dodatkowego, zaufanego stanu; nie należy deklarować jej na podstawie samych podpisów.

## 10. Status

Dokument opisuje wymagany model bezpieczeństwa. Każda granica zaufania wymaga rzeczywistej implementacji i testu negatywnego.

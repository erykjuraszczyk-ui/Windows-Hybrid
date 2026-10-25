# Proces kompilacji, CI i wydawania

## 1. Wersjonowanie

- PATCH: zgodne poprawki błędów.
- MINOR: nowe funkcje zgodne wstecz.
- MAJOR: niekompatybilna zmiana kontraktu lub architektury.

Numer wersji nie jest dowodem gotowości. Status musi wynikać z kodu, buildów i testów.

## 2. CI

Workflow powinien rozdzielać:
1. kontrolę źródeł i konfiguracji;
2. kompilację;
3. testy hostowe;
4. testy bezpieczeństwa i fuzz-smoke;
5. testy integracyjne QEMU;
6. porównanie powtarzalnych buildów;
7. generowanie manifestu i provenance;
8. podpisywanie oraz publikację.

Nieudany wymagany test blokuje wydanie. Niezaufane pull requesty nie mogą mieć dostępu do kluczy podpisujących ani nadmiernych uprawnień zapisu.

## 3. Powtarzalne buildy

Przypnij wersje kompilatora, linkera i zależności. Kontroluj locale, strefę czasową, ścieżki i znaczniki czasu. Używaj SOURCE_DATE_EPOCH, jeżeli narzędzia go obsługują. Porównuj dwa czyste buildy i wyjaśniaj różnice.

## 4. Artefakty

Każdy artefakt powinien mieć wpis z nazwą, rozmiarem, SHA-256, identyfikatorem źródłowego commita i profilem builda. Manifest nie może zawierać przykładowych wartości jako prawdziwych danych.

Provenance opisuje, jak artefakt powstał, ale samo nie dowodzi bezpieczeństwa ani powtarzalności.

## 5. Kanały wydań

- development: buildy robocze.
- beta: kandydaci przed wydaniem.
- stable: wyłącznie zaakceptowane wersje.

Promocja do stable powinna wskazywać na ten sam artefakt, który przeszedł testy. Jeśli artefakt został ponownie zbudowany, trzeba go ponownie zweryfikować.

## 6. Podpisy i klucze

Rozdziel klucze testowe i produkcyjne. Klucze podpisujące nie mogą być dostępne w zwykłym jobie testującym niezaufane zmiany. Zdefiniuj rotację, unieważnienie i odzyskiwanie po kompromitacji klucza.

## 7. Aktualizacje

Weryfikuj zaufane metadane, podpisy, rozmiary, skróty, wersje, architekturę i ABI. Przygotuj staging i dziennik transakcji, a aktywną wersję zmieniaj dopiero po pozytywnej weryfikacji.

Aktualizacje aplikacji i systemu mają osobne ścieżki. Rollback jest dostępny tylko wtedy, gdy istnieje kompletna zweryfikowana kopia i rzeczywisty mechanizm odzyskiwania.

## 8. Release checklist

- [ ] Źródło i testy wskazują ten sam zatwierdzony commit.
- [ ] Wymagane testy mają PASS.
- [ ] Nie ma otwartych błędów blokujących.
- [ ] Buildy są powtarzalne albo różnice są udokumentowane.
- [ ] Artefakty mają manifest, hashe i provenance.
- [ ] Podpisy pochodzą z uprawnionego procesu.
- [ ] Instrukcje odzyskiwania i znane ograniczenia są opublikowane.

# Contributing to DefineExtractor

[**Deutsch**](#deutsch) | [**English**](#english)

---

## <a name="deutsch"></a>Deutsch

Danke, dass du zu **DefineExtractor** beitragen möchtest!

### Voraussetzungen

Bevor du beginnst, stelle sicher, dass du die [README.md](README.md) gelesen hast und das Projekt lokal bauen kannst.

### Wie du beitragen kannst

#### Fehler melden (Bug Report)

1. Prüfe zuerst, ob das Problem bereits in den [Issues](../../issues) gemeldet wurde.
2. Falls nicht, erstelle ein neues Issue mit:
   - einer klaren, beschreibenden Überschrift
   - den Schritten zur Reproduktion
   - dem erwarteten und dem tatsächlichen Verhalten
   - Betriebssystem, Visual Studio-Version und Build-Konfiguration (Debug/Release)

#### Feature-Anfragen

1. Erstelle ein Issue mit dem Label `enhancement`.
2. Beschreibe den Anwendungsfall klar — warum ist das Feature nützlich?

#### Code beisteuern (Pull Request)

1. **Fork** das Repository und klone es lokal.
2. Erstelle einen neuen Branch:
   ```bat
   git checkout -b feature/mein-feature
   ```
3. Halte dich an den bestehenden Code-Stil (C++20, keine externen Abhängigkeiten außer Catch2 für Tests).
4. **Schreibe Tests** für jede neue Funktion oder jeden Fix in `tests/test_helpers.cpp`.
5. Stelle sicher, dass alle Tests bestehen:
   ```bat
   cmake --preset debug && cmake --build --preset debug --target test_helpers
   .\build\debug\test_helpers.exe
   ```
6. Stelle sicher, dass das Projekt in **Debug und Release** fehlerfrei baut:
   ```bat
   cmake --build --preset debug
   cmake --build --preset release
   ```
7. Committe deine Änderungen mit einer aussagekräftigen Commit-Nachricht.
8. Öffne einen **Pull Request** mit einer Beschreibung der Änderungen und dem Grund dafür.

### Code-Stil

- **Sprache**: C++20
- **Standard-Bibliothek**: Nur STL — keine externen Bibliotheken im Produktionscode.
- **Namenskonventionen**: `camelCase` für Funktionen und Variablen, `PascalCase` für Typen.
- **Kommentare**: Englisch.
- **Neue Hilfsfunktionen**: Wenn möglich in `DefineExtractor/helpers.h` auslagern (testbar halten).
- **Performance**: Regex nur als letztes Mittel — zuerst `string::find` als Pre-Filter prüfen.

### Commit-Nachrichten

Kurz und prägnant, auf Englisch:
```
fix: correct getIndent for trailing whitespace
feat: add pre-filter before regex_search
refactor: extract isAnyIfStart to helpers.h
```

---

## <a name="english"></a>English

Thank you for considering a contribution to **DefineExtractor**!

### Prerequisites

Make sure you have read the [README.md](README.md) and can build the project locally before you start.

### How to Contribute

#### Reporting Bugs

1. Check [Issues](../../issues) first to avoid duplicates.
2. Open a new issue with:
   - a clear, descriptive title
   - steps to reproduce
   - expected vs. actual behaviour
   - OS, Visual Studio version, and build configuration (Debug/Release)

#### Feature Requests

1. Open an issue with the `enhancement` label.
2. Describe the use-case clearly — why would this feature be useful?

#### Submitting a Pull Request

1. **Fork** the repository and clone it locally.
2. Create a new branch:
   ```bat
   git checkout -b feature/my-feature
   ```
3. Follow the existing code style (C++20, no external production dependencies).
4. **Write tests** for any new function or bug fix in `tests/test_helpers.cpp`.
5. Verify all tests pass:
   ```bat
   cmake --preset debug && cmake --build --preset debug --target test_helpers
   .\build\debug\test_helpers.exe
   ```
6. Verify the project builds cleanly in **both Debug and Release**:
   ```bat
   cmake --build --preset debug
   cmake --build --preset release
   ```
7. Commit your changes with a meaningful message.
8. Open a **Pull Request** describing what changed and why.

### Code Style

- **Language**: C++20
- **Dependencies**: STL only in production code — no external libraries.
- **Naming**: `camelCase` for functions and variables, `PascalCase` for types.
- **Comments**: English.
- **New helpers**: Extract to `DefineExtractor/helpers.h` where possible to keep them unit-testable.
- **Performance**: Prefer `string::find` pre-filters over raw `std::regex_search` calls on every line.

### Commit Messages

Keep them short and descriptive, in English:
```
fix: correct getIndent for trailing whitespace
feat: add pre-filter before regex_search
refactor: extract isAnyIfStart to helpers.h
```

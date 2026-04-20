# DefineExtractor

[**Deutsch**](#deutsch) | [**English**](#english)

---

## <a name="deutsch"></a>Deutsch

### 1. Überblick

**DefineExtractor** ist ein C++20-Kommandozeilenprogramm für Windows, das Quellcode nach folgenden Mustern durchsucht:
- `#define`-Makros in `.h`- und `.cpp`-Dateien
- `app.xyz`-Aufrufe in `.py`-Dateien

Das Tool nutzt Multithreading und Memory-Mapping für effizientes Scannen großer Codebasen.

---

### 2. Hauptfunktionen

1. **Automatische Header-Erkennung**
   - Findet `locale_inc.h` (Client) sowie `service.h` / `commondefines.h` (Server) durch rekursives Durchsuchen einschlägiger Unterordner.

2. **Makro-Listing**
   - Zeigt alle definierten Makros aus den erkannten Headern an, um gezielt nach einem bestimmten `#define` zu suchen.

3. **Python-Parameter**
   - Ermittelt alle Aufrufe im Format `app.xyz` innerhalb von `.py`-Dateien und listet diese übersichtlich auf.

4. **Ausgabedateien in `Output/`**
   - Für jedes gescannte Makro bzw. jeden Python-Parameter erzeugt das Tool zwei Textdateien:
     - `*_DEFINE.txt`: `#if <DEFINE>`-Blöcke bzw. `if app.xyz`-Blöcke
     - `*_FUNC.txt`: Funktionen, in denen das Define bzw. der Parameter auftaucht

5. **Multithreading**
   - Dateien werden parallel auf alle verfügbaren CPU-Kerne verteilt.
   - Ein Pre-Filter (`#` + Define-Name) verhindert unnötige Regex-Aufrufe auf irrelevanten Zeilen.

---

### 3. Voraussetzungen

| Komponente | Version |
|---|---|
| Visual Studio | 2022 oder 2026 (Workload: *Desktopentwicklung mit C++*) |
| CMake | ≥ 3.15 (im VS-Lieferumfang enthalten) |
| C++-Standard | C++20 |
| Betriebssystem | Windows (x64) |

---

### 4. Build – CMake & Visual Studio

Das Projekt verwendet **CMake mit Presets** (`CMakePresets.json`). Zwei Konfigurationen stehen bereit: **Debug** und **Release**.

#### Option A – Visual Studio (empfohlen)

1. Visual Studio starten.
2. **Datei → Öffnen → Ordner…** → Projektordner auswählen (dort liegt `CMakeLists.txt`).
3. VS erkennt `CMakePresets.json` automatisch und zeigt im Konfigurations-Dropdown:
   - `Debug (x64)` → `build/debug/DefineExtractor.exe`
   - `Release (x64)` → `build/release/DefineExtractor.exe`
4. Konfiguration wählen → **Strg+Umschalt+B** zum Bauen.
5. Im Dropdown **„Startobjekt auswählen"** → `DefineExtractor.exe` wählen → **F5** / **Strg+F5**.

#### Option B – Developer Command Prompt

```bat
:: Aus dem Projektordner:
cmake --preset debug    && cmake --build --preset debug
cmake --preset release  && cmake --build --preset release
```

#### Ausgabepfade

| Konfiguration | Pfad |
|---|---|
| Debug | `build/debug/DefineExtractor.exe` |
| Release | `build/release/DefineExtractor.exe` |

---

### 5. Tests

Das Projekt enthält Unit-Tests mit **Catch2 v2** (Single-Header, keine externe Abhängigkeit):

```bat
cmake --preset debug && cmake --build --preset debug --target test_helpers
.\build\debug\test_helpers.exe
```

Alle 18 Tests müssen bestehen, bevor Änderungen eingereicht werden.

---

### 6. Verzeichnisstruktur

```
DefineExtractor/
├─ DefineExtractor/
│   ├─ DefineExtractor.cpp   # Hauptlogik
│   └─ helpers.h             # Performance-Hilfsfunktionen (testbar)
├─ tests/
│   ├─ test_helpers.cpp      # Catch2-Tests
│   └─ catch2/catch.hpp      # Catch2 v2 Single-Header
├─ .github/
│   └─ dependabot.yml
├─ CMakeLists.txt
├─ CMakePresets.json         # Debug- & Release-Presets
├─ HOWTO.md
├─ CONTRIBUTING.md
├─ SECURITY.md
└─ CODE_OF_CONDUCT.md
```

---

### 7. Programmstart & Ordnerstruktur

Starte `DefineExtractor.exe` per Doppelklick oder über CMD/PowerShell.  
Lege Client-, Server- und Python-Ordner **auf derselben Ebene** wie den Programmordner ab:

```
C:\MeineProjekte\
  ├─ DefineExtractor\   ← EXE hier
  ├─ MeinClient\
  ├─ MeinServer\
  └─ PythonStuff\
```

**Ablauf:**
1. Pfad-Menü: **Client**, **Server** und **Python Root** setzen.
2. Hauptmenü: Gewünschten Scan-Typ wählen.
3. Define / Parameter auswählen → Ergebnisse landen in `Output/`.

---

### 8. Bekannte Einschränkungen

- **Regex-Grenzen**: Bei stark verschachtelten oder unkonventionellen Makros kann die Erkennung fehlschlagen.
- **Windows-Fokus**: Memory-Mapping (`MapViewOfFile`) ist Windows-spezifisch; unter Linux greift ein `getline`-Fallback.
- **Keine tiefe Python-Analyse**: Nur `if app.xyz`-Blöcke und `def`-Funktionen werden erfasst.

---

### 9. Mitmachen & Sicherheit

- Beiträge willkommen: siehe [CONTRIBUTING.md](CONTRIBUTING.md)
- Sicherheitslücken bitte nicht öffentlich melden: siehe [SECURITY.md](SECURITY.md)
- Umgangston: siehe [CODE_OF_CONDUCT.md](CODE_OF_CONDUCT.md)

---

### 10. Lizenz

Kein spezifischer Lizenztext enthalten. Verwendung auf eigene Verantwortung.

---

## <a name="english"></a>English

### 1. Overview

**DefineExtractor** is a Windows-oriented C++20 command-line tool that searches source code for:
- `#define` macros in `.h` and `.cpp` files
- `app.xyz` references in `.py` files

It uses multithreading and memory-mapped I/O for fast scanning of large codebases.

---

### 2. Key Features

1. **Automatic Header Detection** — finds `locale_inc.h` (Client) and `service.h` / `commondefines.h` (Server) by recursive folder scan.
2. **Macro Listing** — displays all macros from the detected header for targeted `#define` searches.
3. **Python Parameter Discovery** — locates `app.xyz` calls in `.py` files.
4. **Output Files in `Output/`** — two files per macro or parameter:
   - `*_DEFINE.txt`: matching `#if … #endif` or `if app.xyz` blocks
   - `*_FUNC.txt`: functions referencing the define or parameter
5. **Multithreading** — distributes files across all CPU cores with a fast pre-filter that skips the regex on irrelevant lines.

---

### 3. Requirements

| Component | Version |
|---|---|
| Visual Studio | 2022 or 2026 (workload: *Desktop development with C++*) |
| CMake | ≥ 3.15 (bundled with VS) |
| C++ standard | C++20 |
| OS | Windows (x64) |

---

### 4. Build — CMake & Visual Studio

The project uses **CMake Presets** (`CMakePresets.json`) with two configurations: **Debug** and **Release**.

#### Option A — Visual Studio (recommended)

1. Launch Visual Studio.
2. **File → Open → Folder…** → select the project root (where `CMakeLists.txt` lives).
3. VS reads `CMakePresets.json` automatically and shows in the configuration dropdown:
   - `Debug (x64)` → `build/debug/DefineExtractor.exe`
   - `Release (x64)` → `build/release/DefineExtractor.exe`
4. Select a configuration → **Ctrl+Shift+B** to build.
5. In **"Select Startup Item"** choose `DefineExtractor.exe` → **F5** / **Ctrl+F5**.

#### Option B — Developer Command Prompt

```bat
cmake --preset debug    && cmake --build --preset debug
cmake --preset release  && cmake --build --preset release
```

#### Output paths

| Configuration | Path |
|---|---|
| Debug | `build/debug/DefineExtractor.exe` |
| Release | `build/release/DefineExtractor.exe` |

---

### 5. Tests

Unit tests use **Catch2 v2** (single-header, no external install required):

```bat
cmake --preset debug && cmake --build --preset debug --target test_helpers
.\build\debug\test_helpers.exe
```

All 18 tests must pass before submitting changes.

---

### 6. Project Layout

```
DefineExtractor/
├─ DefineExtractor/
│   ├─ DefineExtractor.cpp   # core logic
│   └─ helpers.h             # performance helpers (unit-tested)
├─ tests/
│   ├─ test_helpers.cpp      # Catch2 tests
│   └─ catch2/catch.hpp      # Catch2 v2 single-header
├─ .github/
│   └─ dependabot.yml
├─ CMakeLists.txt
├─ CMakePresets.json         # Debug & Release presets
├─ HOWTO.md
├─ CONTRIBUTING.md
├─ SECURITY.md
└─ CODE_OF_CONDUCT.md
```

---

### 7. Running the Program

Launch `DefineExtractor.exe` by double-clicking or from CMD/PowerShell.  
Place Client, Server, and Python folders **at the same level** as the program folder:

```
C:\MyProjects\
  ├─ DefineExtractor\   ← exe here
  ├─ MyClient\
  ├─ MyServer\
  └─ PythonStuff\
```

**Workflow:**
1. Path menu: set **Client**, **Server**, and **Python Root**.
2. Main menu: choose the scan type.
3. Select a define or parameter → results appear in `Output/`.

---

### 8. Known Limitations

- **Regex boundaries**: Highly unconventional or deeply nested macros may be missed or over-matched.
- **Windows focus**: Memory-mapped I/O (`MapViewOfFile`) is Windows-specific; a `getline` fallback is used on other platforms.
- **Limited Python analysis**: Only `if app.xyz` blocks and `def` functions are captured.

---

### 9. Contributing & Security

- Contributions welcome: see [CONTRIBUTING.md](CONTRIBUTING.md)
- Security issues — do not file public issues: see [SECURITY.md](SECURITY.md)
- Community standards: see [CODE_OF_CONDUCT.md](CODE_OF_CONDUCT.md)

---

### 10. License

No specific license is included. Use at your own risk.

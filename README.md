# DefineExtractor

[**Deutsch**](#deutsch) | [**English**](#english)

[**Download für Windows x64 / Windows x64 download**](https://github.com/M2tecDev/DefineExtractor/releases/latest)

[![Made with Slint](https://raw.githubusercontent.com/slint-ui/slint/e9c1ca295f9356af71f1e251c287de18406b46f6/logo/MadeWithSlint-logo-whitebg.png)](https://slint.dev/)

---

## <a name="deutsch"></a>Deutsch

### 1. Überblick

**DefineExtractor** ist ein Windows-Werkzeug mit grafischer Oberfläche und Konsole. Es durchsucht Quellcode nach folgenden Mustern:
- `#define`-Makros in `.h`- und `.cpp`-Dateien
- `app.xyz`-Aufrufe in `.py`-Dateien

Alle ausgewählten Symbole werden in einem Durchlauf pro Datei gesucht. Für den portablen Download das ZIP in einen beschreibbaren Ordner entpacken und `DefineExtractorGUI.exe` starten. Die Konsole ist als `DefineExtractor.exe` enthalten.

---

### 2. Hauptfunktionen

1. **Automatische Header-Erkennung**
   - Findet `locale_inc.h` (Client) sowie `service.h` / `commondefines.h` (Server) durch rekursives Durchsuchen einschlägiger Unterordner.

2. **Makro-Listing**
   - Zeigt alle definierten Makros aus den erkannten Headern an, um gezielt nach einem bestimmten `#define` zu suchen.

3. **Python-Parameter**
   - Listet `app.xyz` aus `if`- und `elif`-Bedingungen in Python-Dateien auf.

4. **Ausgabedateien in `Output/`**
   - Jeder Lauf erhält einen eigenen Ordner in `Output/`. Pro Symbol gibt es zwei Textdateien mit relativen Quellpfaden und Zeilenbereichen.
     - `*_DEFINE.txt` enthält die passenden C++-Präprozessorzweige oder Python-`if`/`elif`-Blöcke.
     - `*_FUNC.txt` enthält die zugehörigen Funktionen.

5. **Multithreading**
   - Die Worker teilen sich die Dateien. Jede Datei wird einmal für alle ausgewählten Symbole gelesen und analysiert.

---

### 3. Voraussetzungen

| Komponente | Version |
|---|---|
| Visual Studio | 2022 oder 2026 (Workload: *Desktopentwicklung mit C++*) |
| CMake | ≥ 3.25 (im VS-Lieferumfang enthalten) |
| Rust | Stable, erforderlich zum Bauen von Slint |
| C++-Standard | C++23 |
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
5. Im Dropdown **„Startobjekt auswählen"** → `DefineExtractorGUI.exe` oder `DefineExtractor.exe` wählen → **F5** / **Strg+F5**.

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

Das Projekt enthält Unit-Tests mit **Catch2 v3** (über CMake FetchContent bezogen):

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
│   # Catch2 v3 wird automatisch via CMake bezogen
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
3. Ein oder mehrere Defines oder Parameter auswählen. In der Konsole werden die Nummern mit Leerzeichen oder Kommas getrennt.

---

### 8. Bekannte Einschränkungen

- Die C++-Analyse erfasst Quelltext in allen Präprozessorzweigen. Sie expandiert keine Makros und verwendet Heuristiken für Funktionsgrenzen.
- Die Python-Analyse erfasst `app.xyz` in `if`- und `elif`-Bedingungen und ordnet die Treffer umgebenden Funktionen zu.

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

**DefineExtractor** is a Windows desktop and console tool that searches source code for:
- `#define` macros in `.h` and `.cpp` files
- `app.xyz` references in `.py` files

It checks all selected symbols in one pass per file. To use the portable download, extract the ZIP to a writable folder and run `DefineExtractorGUI.exe`. The package also includes `DefineExtractor.exe` for console use.

---

### 2. Key Features

1. **Automatic Header Detection** — finds `locale_inc.h` (Client) and `service.h` / `commondefines.h` (Server) by recursive folder scan.
2. **Macro Listing** — displays all macros from the detected header for targeted `#define` searches.
3. **Python Parameter Discovery** — lists `app.xyz` references in `if` and `elif` conditions.
4. **Output Files** — each run gets a directory in `Output/` with two files per symbol. Reports list relative source paths and line ranges.
   - `*_DEFINE.txt` contains matching C++ preprocessor branches or Python `if`/`elif` blocks.
   - `*_FUNC.txt` contains the associated functions.

---

### 3. Requirements

| Component | Version |
|---|---|
| Visual Studio | 2022 or 2026 (workload: *Desktop development with C++*) |
| CMake | ≥ 3.25 (bundled with VS) |
| Rust | Stable, required to build Slint |
| C++ standard | C++23 |
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
5. In **"Select Startup Item"** choose `DefineExtractorGUI.exe` or `DefineExtractor.exe` → **F5** / **Ctrl+F5**.

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

Unit tests use **Catch2 v3** (fetched via CMake FetchContent):

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
│   # Catch2 v3 is fetched automatically via CMake
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
3. Select one or more defines or parameters. In the console, separate indices with spaces or commas.

---

### 8. Known Limitations

- C++ scanning considers source text in every preprocessor branch. It uses heuristics for function boundaries and does not expand macros.
- Python scanning captures `app.xyz` in `if` and `elif` conditions and associates matches with enclosing functions.

---

### 9. Contributing & Security

- Contributions welcome: see [CONTRIBUTING.md](CONTRIBUTING.md)
- Security issues — do not file public issues: see [SECURITY.md](SECURITY.md)
- Community standards: see [CODE_OF_CONDUCT.md](CODE_OF_CONDUCT.md)

---

### 10. License

No specific license is included. Use at your own risk.

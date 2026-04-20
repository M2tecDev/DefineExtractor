# DefineExtractor – CMake-Projekt in Visual Studio 2022 öffnen und bauen

## Voraussetzungen

- Visual Studio 2022 mit der Workload **„Desktopentwicklung mit C++"**
- CMake-Unterstützung (wird automatisch mitinstalliert)
- Ninja-Build (ebenfalls Teil der VS-Workload)

---

## Schritte

### 1. Ordner öffnen

1. Visual Studio starten.
2. **Datei → Öffnen → Ordner…** wählen.
3. Den Stammordner des Projekts auswählen (dort liegt `CMakeLists.txt`).

> VS erkennt `CMakeLists.txt` automatisch und aktiviert den CMake-Modus.

---

### 2. CMake-Cache generieren

1. VS generiert den CMake-Cache beim ersten Öffnen automatisch im Hintergrund.
2. Status im **Ausgabe**-Fenster verfolgen (Ansicht → Ausgabe → „CMake").
3. Falls nötig: **Projekt → CMake-Cache löschen und neu generieren**.

Standard-Konfiguration: `x64-Debug` (Ninja-Build).

---

### 3. Startobjekt auswählen

1. In der Symbolleiste das Dropdown **„Startobjekt auswählen"** öffnen.
2. **`DefineExtractor.exe`** auswählen.

---

### 4. Bauen und Starten

| Aktion | Tastenkürzel |
|--------|-------------|
| Bauen  | `Strg+Umschalt+B` |
| Starten (ohne Debugger) | `Strg+F5` |
| Debuggen | `F5` |

---

## Konfiguration wechseln (Debug / Release)

1. **Projekt → CMake-Einstellungen** öffnen (`CMakeSettings.json` wird erstellt).
2. Mit **„+"** eine neue Konfiguration (`x64-Release`) hinzufügen.
3. Cache neu generieren → bauen.

---

## Ausgabepfad

Binärdatei liegt nach dem Build unter:

```
out/build/<Konfiguration>/DefineExtractor/DefineExtractor.exe
```

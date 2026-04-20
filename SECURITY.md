# Security Policy

[**Deutsch**](#deutsch) | [**English**](#english)

---

## <a name="deutsch"></a>Deutsch

### Unterstützte Versionen

Sicherheitsupdates werden ausschließlich für den aktuellen `main`-Branch bereitgestellt.

| Version / Branch | Support |
|---|---|
| `main` (aktuell) | Ja |
| Ältere Releases | Nein |

### Sicherheitslücken melden

**Bitte erstelle kein öffentliches GitHub-Issue für Sicherheitslücken.**

Melde Sicherheitsprobleme stattdessen vertraulich:

1. Öffne ein [GitHub Security Advisory](../../security/advisories/new) in diesem Repository.
2. Oder sende eine E-Mail an den Projektverantwortlichen (E-Mail-Adresse im GitHub-Profil).

Bitte nenne in deiner Meldung:
- eine Beschreibung der Schwachstelle
- Schritte zur Reproduktion
- mögliche Auswirkungen
- falls bekannt: einen Vorschlag zur Behebung

### Reaktionszeit

Wir versuchen, Sicherheitsmeldungen innerhalb von **7 Werktagen** zu bestätigen und **30 Tagen** zu beheben. Du wirst über den Fortschritt informiert.

### Hinweis zum Scope

DefineExtractor ist ein lokales Kommandozeilenwerkzeug ohne Netzwerkzugriff. Potenzielle Angriffsflächen sind:
- Verarbeitung von nicht vertrauenswürdigen Quelldateien (z.B. pathologische Regex-Eingaben)
- Schreibzugriff auf das `Output/`-Verzeichnis

---

## <a name="english"></a>English

### Supported Versions

Security updates are provided for the current `main` branch only.

| Version / Branch | Supported |
|---|---|
| `main` (current) | Yes |
| Older releases | No |

### Reporting a Vulnerability

**Please do not open a public GitHub Issue for security vulnerabilities.**

Instead, report security issues confidentially:

1. Open a [GitHub Security Advisory](../../security/advisories/new) in this repository.
2. Or send an e-mail to the maintainer (address on the GitHub profile).

Please include:
- a description of the vulnerability
- steps to reproduce
- potential impact
- a suggested fix if you have one

### Response Time

We aim to acknowledge security reports within **7 business days** and resolve them within **30 days**. You will be kept informed of progress.

### Scope

DefineExtractor is a local command-line tool with no network access. Potential attack surfaces are:
- Processing untrusted source files (e.g. pathological regex inputs)
- Write access to the `Output/` directory

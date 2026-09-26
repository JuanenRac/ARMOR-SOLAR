<p align="center">
  <img src="images/ARMOR_BANNER.svg" alt="ARMOR-SOLAR banner" width="100%">
</p>

# ☀️ ARMOR-SOLAR

<p align="center">
  <a href="README.md">🇺🇸 English</a> |
  <a href="README_spa.md">🇪🇸 Español</a> |
  <a href="README_fra.md">🇫🇷 Français</a> |
  <a href="README_ita.md">🇮🇹 Italiano</a> |
  🇩🇪 <b>Deutsch</b> |
  <a href="README_zho.md">🇨🇳 简体中文</a> |
  <a href="README_jpn.md">🇯🇵 日本語</a>
</p>

### Überwachung von Solar-Wechselrichtern und -Batterien: die seriellen Protokolle und die Nachrichten eines Gateway-Knotens

<p align="center">
  <img src="https://img.shields.io/badge/License-GPL%203.0-blue.svg" alt="GPL 3.0">
  <img src="https://img.shields.io/badge/Language-C%2B%2B17-00599c.svg" alt="Language">
  <img src="https://img.shields.io/badge/Devices-Voltronic%20%C2%B7%20Pylontech-ffb020.svg" alt="Devices">
  <img src="https://img.shields.io/badge/Maturity-scaffolding-FFB020.svg" alt="Maturity">
</p>

---

**Ehrlichkeitsprüfung - was heute läuft:** **Reifegrad: Scaffolding.** Die Protokollbibliothek (100 Prüfungen) dekodiert Voltronic- / MPP-Solar-Wechselrichter und die Pylontech-Konsole, Zelle für Zelle und mit den Kapazitäten, aus typischen, von Hand geschriebenen Antworten. Die Nachrichten, die sie ausgibt, werden von ARMOR-COMMON validiert, von ARMOR-SERVER gelesen und in den Menüs Wechselrichter und Batterien von Studio gezeigt, alles mit erzeugten Messwerten. **Es wurde kein Wechselrichter und keine Batterie angeschlossen**, es gibt noch keine Knoten-Firmware, und ANT-BMS wird nicht dekodiert, weil sein Protokolldokument nicht im Projekt liegt.

---

## 🎯 Überblick

* **Voltronic- / MPP-Solar-Wechselrichter** (Axpert, PIP, InfiniSolar und Klone), RS232 mit 2400 Baud: die Rahmen und ihre CRC sowie die Messwerte `QPIGS` (Netz, Ausgang, Batterie, PV, Statusbits), `QMOD` (Modus), `QPIWS` (Warnungen und Störungen mit Namen) und `QPIRI` (Nennwerte). Nur lesende Befehle lassen sich bauen: eine Einstellung ändert, wie das Haus versorgt wird.
* **Pylontech-Batterien** (US2000, US3000, US5000): die `pwr`-Tabelle der Konsole (Spannung, Strom, Temperaturen und Ladezustand jedes Moduls), `bat <n>` (Spannung und Temperatur jeder Zelle) und `info <n>` (Modell, Rest- und Gesamtkapazität, Zyklen), zusammengefasst als ein Stapel mit Gesamtkapazität und -energie, und der RS485-Rahmen mit seinen zwei Prüfungen. Die Formate sind aus dem Gedächtnis der öffentlichen Konsole geschrieben und können sich je nach Firmware unterscheiden.
* **Die Nachrichten eines Gateway-Knotens** (`armor/solar/<Knoten>/<Gerät>/state`, eine je Wechselrichter oder Batteriestapel), in ARMOR-COMMON mit Schemas und Konformitätsvektoren definiert, hier serialisiert und von einem Skript geprüft.
* **Der Entwurf des Rests:** welche Platine (nur WLAN oder Ethernet mit PoE), wie man RS232 und RS485 sicher verdrahtet (Pegelwandlung und Isolation) und in welcher Reihenfolge die Studio-Menüs gebaut wurden.
* **Noch nicht:** ANT-BMS, die Firmware des Knotens und der Android-Tab. Nichts wurde an ein echtes Gerät angeschlossen.

## 📂 Struktur des Repositorys

```text
ARMOR-SOLAR/
├── core/    voltronic.hpp, pylontech.hpp, solar_json.hpp, json.hpp
├── tests/   test_solar.cpp, emit_samples.cpp, check_samples.py
└── docs/    PROTOCOLS.md, NODE_HARDWARE.md, SOLAR_MESSAGES.md, STUDIO_MENUS.md
```

## 🛠️ Entwicklungsumgebung

```bash
cmake -S tests -B build/host && cmake --build build/host
build/host/test_solar                                # 100 checks, -Werror
build/host/emit_samples | python tests/check_samples.py   # the messages have the fields of contract version 0
```

## 🔗 Verwandte Projekte

**A.R.M.O.R.** (Autonomous Radar & Multimodal Observation Range) ist ein Perimeter-Sicherheitssystem aus unabhängigen Repositorys. Jedes hat eine eigene Version, eigene Tests und ein eigenes README; hier ist die Familie:

* **[ARMOR-COMMON](../ARMOR-COMMON)** - Nachrichtenverträge, Validierer, Konformitätsvektoren und generierte Typen
* **[ARMOR-RADAR](../ARMOR-RADAR)** - Feldknoten-Firmware für ESP32-S3 mit drei Radaren und eigenem Web-Panel
* **ARMOR-SOLAR** (dieses Repository) - Protokolle für Solar-Wechselrichter und -Batterien und die Nachrichten eines Gateway-Knotens
* **[ARMOR-SERVER](../ARMOR-SERVER)** - Zentraler Koordinator: Telemetrie, Alarme, Geräte, Solarmesswerte und Kameras
* **[ARMOR-STUDIO](../ARMOR-STUDIO)** - Web-Konsole: Kameras, Radar, Alarme, Solarenergie und 2D/3D-Standortdesigner
* **[ARMOR-ANDROID-CONTROL](../ARMOR-ANDROID-CONTROL)** - Android-Bedienclient mit Live-Radar in 2D/3D
* **[ARMOR-SERVER-AI](../ARMOR-SERVER-AI)** - Visuelle Inferenzrichtlinie, die ihre Entscheidungen erklärt und nie handelt
* **[ARMOR-VOICE-AI](../ARMOR-VOICE-AI)** - Offline-Sprachabsichten mit einer nicht fälschbaren Bestätigung
* **[ARMOR-HARDWARE](../ARMOR-HARDWARE)** - Gehäuse, Elektronik und die Abnahmematrix am Prüfstand
* **[ARMOR-DEVOPS](../ARMOR-DEVOPS)** - Bereitstellung, CM5-Prüfstand, Backup und TLS
* **[ARMOR-SIMULATOR](../ARMOR-SIMULATOR)** - Offline-Telemetriesimulator mit wiederholbaren Fehlern
* **[ARMOR-DOCS](../ARMOR-DOCS)** - Architektur, Sicherheitsgrundlage und die Fähigkeitsmatrix

## 📚 Dokumentation und Community

Hier gibt es mehr zu lesen:

* [Fähigkeitsmatrix: was belegt ist und was nicht](../ARMOR-DOCS/docs/CAPABILITY_MATRIX.md)
* [Projektkatalog: Versionen und wie die Repositorys voneinander abhängen](../ARMOR-DOCS/docs/PROJECT_CATALOG.md)
* [Änderungsverlauf dieses Repositorys](CHANGELOG.md)
* [Lizenz (GPL-3.0-or-later)](LICENSE)
* Fragen, Ideen und Meldungen: electrohobby3d@gmail.com

## 👤 AUTOR

**JuanenRac (Electro Hobby 3D)** · electrohobby3d@gmail.com

## 📜 LIZENZ

GPL-3.0-or-later - siehe [LICENSE](LICENSE).

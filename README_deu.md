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

### Solar-Gateway-Knoten: liest Wechselrichter und Batterien über bis zu zehn serielle Ports (ESP32-S3-Firmware, sein Web-Panel und die Protokollbibliothek)

<p align="center">
  <img src="https://img.shields.io/badge/License-GPL%203.0-blue.svg" alt="GPL 3.0">
  <img src="https://img.shields.io/badge/Language-C%2B%2B17-00599c.svg" alt="Language">
  <img src="https://img.shields.io/badge/Devices-Voltronic%20%C2%B7%20Pylontech-ffb020.svg" alt="Devices">
  <img src="https://img.shields.io/badge/Maturity-scaffolding-FFB020.svg" alt="Maturity">
</p>

---

**Ehrlichkeitsprüfung - was heute läuft:** **Reifegrad: Scaffolding.** Die Firmware baut im ESP-IDF-5.4.2-Container, ihr Kern (die Einstellungen, der Austausch mit jeder Geräteart, die Arithmetik des emulierten UART: 2,550 Prüfungen) ist am Rechner mit Stellvertreter-Geräten getestet, die Nachrichten, die sie erzeugt, akzeptiert ARMOR-COMMON, und das Panel wurde in einem Browser gegen einen Stellvertreter-Knoten ausprobiert. **Sie lief nie auf einer Platine, und es wurde kein Wechselrichter und keine Batterie angeschlossen**: das WLAN, das Panel mit TLS, das Update, die Hardware- und emulierten UARTs und die Formate der Protokolle (aus öffentlichen Dokumenten und aus dem Gedächtnis geschrieben) sind unerprobt. Die ANT-BMS-Frames in den Tests wurden von anderen an deren eigenen Geräten aufgezeichnet: dieses Projekt hat selbst noch keines gelesen.

---

## 🎯 Überblick

* **Zwei Platinen, eine Firmware:** ein ESP32-S3-WROOM-1 N16R8 per WLAN (Standard) und die Waveshare ESP32-S3-ETH am Ethernet-Kabel (DHCP oder feste Adresse; ihr eigenes WLAN bleibt, um sie vom Handy aus zu erreichen). Das Image wird beim Bauen gewählt (`tools/build_node.sh generic s3-wifi` oder `generic s3-eth`); Pin-Tabelle, Standard-Pins der Ports und Zugangsweg folgen der Platine, und ein Image gilt nur für seine Platine. Beide bauen und sind am Rechner getestet; keine lief je auf einer Platine.
* **Die Firmware des Knotens** (ESP32-S3-WROOM-1 N16R8 per WLAN oder Waveshare ESP32-S3-ETH am Kabel): zehn serielle Ports, drei Hardware-UARTs und sieben emulierte (bis 19200 Baud), jeder unabhängig und jeder liest einen Wechselrichter, eine Batterie oder einen Rohmonitor, sodass ein Knoten nur Wechselrichter, nur Batterien oder eine Mischung lesen kann.
* **Das Web-Panel des Knotens,** auch das der Radarknoten: Einrichtung mit einem Code, der auf der USB-Konsole erscheint, Anmeldung und Benutzer, WLAN (Station und Access Point), Broker, Update per Funk mit Rollback, Protokoll, HTTPS und die Seiten der Ports (mit dem, was jeder hört, für noch nicht dekodierte Protokolle) und der Messwerte, in sieben Sprachen.
* **Voltronic- / MPP-Solar-Wechselrichter** (Axpert, PIP, InfiniSolar und Klone), RS232 mit 2400 Baud: die Rahmen und ihre CRC sowie die Messwerte `QPIGS` (Netz, Ausgang, Batterie, PV, Statusbits), `QMOD` (Modus), `QPIWS` (Warnungen und Störungen mit Namen) und `QPIRI` (Nennwerte). Nur lesende Befehle lassen sich bauen: eine Einstellung ändert, wie das Haus versorgt wird. Drei Dialekte, am Port gewählt oder von ihm gefunden (*auto*): PI30, REVO (ein anderes `QPIGS`, Antworten mit Prüfsumme) und PI18 (`^P005GS`: InfiniSolar V, LV5048, SunGoldPower), nur lesend.
* **Pylontech-Batterien** (US2000, US3000, US5000): die `pwr`-Tabelle der Konsole (Spannung, Strom, Temperaturen und Ladezustand jedes Moduls), `bat <n>` (Spannung und Temperatur jeder Zelle) und `info <n>` (Modell, Rest- und Gesamtkapazität, Zyklen), zusammengefasst als ein Stapel mit Gesamtkapazität und -energie, und der RS485-Rahmen mit seinen zwei Prüfungen. Die Formate sind aus dem Gedächtnis der öffentlichen Konsole geschrieben und können sich je nach Firmware unterscheiden. Die Spalten werden über die Namen der Kopfzeile gefunden (auch das Format der US5000 V2.3 mit Id-Spalten, MosTempr und SysAlarm.St), die Restladung und das Balancing kommen aus `bat`, Modell, Nennkapazität und Zyklen aus `info` und `stat`, die einmal abgefragt und eine halbe Stunde behalten werden.
* **Die Nachrichten des Knotens** (`armor/solar/<Knoten>/<Gerät>/state`, eine je Wechselrichter oder Batteriestapel), in ARMOR-COMMON mit Schemas und Konformitätsvektoren definiert; was die Ports erzeugen, wird dagegen geprüft.
* **ANT-BMS-Batterien** (die schwarzen Platinen selbstgebauter Akkus, 7S bis 32S), 3,3-V-UART mit 19200 Baud: beide Protokolle seiner Firmware (der Knoten fragt im einen und dann im anderen und bleibt bei dem, das antwortet), mit Zellen, Temperaturen, Ladezustand, Strom, Kapazitäten sowie den Zuständen der MOSFETs und des Balancers, geprüft an Frames, die an vier echten Modellen aufgezeichnet wurden. Nur Lesen: seine Schreibbefehle können eine Batterie unter Last trennen. Eine Schaltfläche auf der Port-Seite liest außerdem Modell, Version und 56 Schutz- und Balancing-Einstellungen des BMS (neueres Protokoll), nur lesend.
* **Konfiguration vom Handy über Bluetooth,** derselbe Kanal wie beim Radarknoten: Die ARMOR-App findet den Knoten als `ARMOR-XXXXXX` und stellt Name, WLAN, Adresse, Broker und Bluetooth-Modus mit den Benutzern und dem Einrichtungscode des Panels ein ([das Protokoll](docs/BLE_PROVISIONING.md)). Er lauscht nur, solange der Knoten keinen Benutzer hat, sofern nichts anderes eingestellt ist. Der Funkteil lief noch nie auf einer Platine.
* **Die Basisplatine mit Multiplexern (das *mux*-Profil):** drei Hardware-UARTs, je eines hinter einem 74HC4052, bedienen bis zu zehn Ports in Gruppen zu 4, 4 und 2, die sich auf ihrer Leitung abwechseln, jeder Port mit eigener Geschwindigkeit und Polarität, und eine LED pro Port über ein 74HC595; die Zellen eines hohen Pylontech-Stapels lassen sich im Wechsel lesen. Für einen Wechselrichter im Standarddialekt sind der **zweite PV-Eingang** (`QPIGS2`) und die **Einheiten eines Parallelsystems** (`QPGS`) optionale Messungen. Nichts davon lief bisher auf einer Platine.
* **Noch nicht:** die Bluetooth-Verbindung des ANT-BMS (der Knoten nutzt das Kabel, das stabilere von beiden) und ein Lauf auf einer echten Platine mit echten Geräten.

## 📂 Struktur des Repositorys

```text
ARMOR-SOLAR/
├── main/    the ESP-IDF component: app_main, solar_manager (one task per port, or per group in the mux profile), uart_ports, mux_board, port_leds_hw, network, web_server, api_shared, mqtt_link, node_store, tls_cert, ble_provision, board_ethernet
├── core/    voltronic, voltronic_pi18, pylontech, ant_bms, ant_registers, ant_settings, solar_json + solar_config, poller, soft_uart, netplan, auth, board_s3, ble_frame, ble_dispatch, mux_group, port_leds, console_probe, json (no hardware in them)
├── panel/   the web panel: index.html, app.js, text.js (7 languages), style.css
├── tools/   build_node.sh, pack_panel.py, panel_mock.mjs, panel_browser_test.mjs
├── tests/   test_solar.cpp, test_node.cpp, test_board_eth.cpp, test_ble.cpp, test_mux.cpp, test_parallel.cpp, test_console.cpp, emit_samples.cpp, emit_poller_samples.cpp, check_samples.py (+ the ANT-BMS frames)
└── docs/    NODE_FIRMWARE, NODE_HARDWARE, BLE_PROVISIONING, PROTOCOLS, SOLAR_MESSAGES, STUDIO_MENUS
```

## 🛠️ Entwicklungsumgebung

```bash
cmake -S tests -B build/host && cmake --build build/host
build/host/test_solar && build/host/test_node && build/host/test_board_eth && build/host/test_ble && build/host/test_mux && build/host/test_parallel && build/host/test_console      # 2,550 checks, -Werror
build/host/emit_poller_samples | python tests/check_samples.py   # what the ports make is accepted by ARMOR-COMMON
tools/build_node.sh generic                       # the firmware image for the N16R8 board in the ESP-IDF container: dist/generic-s3-wifi.bin
tools/build_node.sh generic s3-eth               # the same firmware for the Waveshare ESP32-S3-ETH (Ethernet): dist/generic-s3-eth.bin
node tools/panel_mock.mjs --user admin:adminpass123   # the panel without a board
```

Siehe die [Firmware-Anleitung](docs/NODE_FIRMWARE.md) (die Platine, die Ports, der erste Start und die Prüfliste am Prüfstand) und die [Verdrahtungshinweise](docs/NODE_HARDWARE.md).

## 🔗 Verwandte Projekte

**A.R.M.O.R.** (Autonomous Radar & Multimodal Observation Range) ist ein Perimeter-Sicherheitssystem aus unabhängigen Repositorys. Jedes hat eine eigene Version, eigene Tests und ein eigenes README; hier ist die Familie:

* **[ARMOR-COMMON](https://github.com/JuanenRac/ARMOR-COMMON)** - Nachrichtenverträge, Validierer, Konformitätsvektoren und generierte Typen
* **[ARMOR-RADAR](https://github.com/JuanenRac/ARMOR-RADAR)** - Feldknoten-Firmware für ESP32-S3 mit drei Radaren und eigenem Web-Panel
* **ARMOR-SOLAR** (dieses Repository) - Protokolle für Solar-Wechselrichter und -Batterien und die Nachrichten eines Gateway-Knotens
* **[ARMOR-ELECTRICAL](https://github.com/JuanenRac/ARMOR-ELECTRICAL)** - Elektroknoten: Zähler, die Nachricht der Netzmesswerte und die Regeln fürs Schalten
* **[ARMOR-ALARM](https://github.com/JuanenRac/ARMOR-ALARM)** - Alarmknoten und Alarmzentrale: Zonen, Scharfschalten, Verzögerungen, Sirene und PIN, mit dem Server oder ohne ihn
* **[ARMOR-HMI](https://github.com/JuanenRac/ARMOR-HMI)** - Touch-Panel: der Systemzustand auf einem Wandbildschirm, Scharf- und Quittieren sowie das Zuhause des Sprachassistenten
* **[ARMOR-NETWORK](https://github.com/JuanenRac/ARMOR-NETWORK)** - Das lokale Netzwerk: seine Geräte, das Internet und was sich ändert
* **[ARMOR-SERVER](https://github.com/JuanenRac/ARMOR-SERVER)** - Zentraler Koordinator: Telemetrie, Alarme, Geräte, Solarmesswerte und Kameras
* **[ARMOR-STUDIO](https://github.com/JuanenRac/ARMOR-STUDIO)** - Web-Konsole: Kameras, Radar, Alarme, Solarenergie und 2D/3D-Standortdesigner
* **[ARMOR-ANDROID-CONTROL](https://github.com/JuanenRac/ARMOR-ANDROID-CONTROL)** - Android-Bedienclient mit Live-Radar in 2D/3D
* **[ARMOR-SERVER-AI](https://github.com/JuanenRac/ARMOR-SERVER-AI)** - Visuelle Inferenzrichtlinie, die ihre Entscheidungen erklärt und nie handelt
* **[ARMOR-VOICE-AI](https://github.com/JuanenRac/ARMOR-VOICE-AI)** - Offline-Sprachabsichten mit einer nicht fälschbaren Bestätigung
* **[ARMOR-HARDWARE](https://github.com/JuanenRac/ARMOR-HARDWARE)** - Gehäuse, Elektronik und die Abnahmematrix am Prüfstand
* **[ARMOR-DEVOPS](https://github.com/JuanenRac/ARMOR-DEVOPS)** - Bereitstellung, CM5-Prüfstand, Backup und TLS
* **[ARMOR-SIMULATOR](https://github.com/JuanenRac/ARMOR-SIMULATOR)** - Offline-Telemetriesimulator mit wiederholbaren Fehlern
* **[ARMOR-UPDATER](https://github.com/JuanenRac/ARMOR-UPDATER)** - Erkennt, installiert und aktualisiert die eigenen Repositories des Ökosystems
* **[ARMOR-DOCS](https://github.com/JuanenRac/ARMOR-DOCS)** - Architektur, Sicherheitsgrundlage und die Fähigkeitsmatrix

## 📚 Dokumentation und Community

Hier gibt es mehr zu lesen:

* [Fähigkeitsmatrix: was belegt ist und was nicht](https://github.com/JuanenRac/ARMOR-DOCS/blob/main/docs/CAPABILITY_MATRIX.md)
* [Projektkatalog: Versionen und wie die Repositorys voneinander abhängen](https://github.com/JuanenRac/ARMOR-DOCS/blob/main/docs/PROJECT_CATALOG.md)
* [Änderungsverlauf dieses Repositorys](CHANGELOG.md)
* [Lizenz (GPL-3.0-or-later)](LICENSE)
* Fragen, Ideen und Meldungen: electrohobby3d@gmail.com

## 👤 AUTOR

**JuanenRac (Electro Hobby 3D)** · electrohobby3d@gmail.com

## 📜 LIZENZ

GPL-3.0-or-later - siehe [LICENSE](LICENSE).

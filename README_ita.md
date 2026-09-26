<p align="center">
  <img src="images/ARMOR_BANNER.svg" alt="ARMOR-SOLAR banner" width="100%">
</p>

# ☀️ ARMOR-SOLAR

<p align="center">
  <a href="README.md">🇺🇸 English</a> |
  <a href="README_spa.md">🇪🇸 Español</a> |
  <a href="README_fra.md">🇫🇷 Français</a> |
  🇮🇹 <b>Italiano</b> |
  <a href="README_deu.md">🇩🇪 Deutsch</a> |
  <a href="README_zho.md">🇨🇳 简体中文</a> |
  <a href="README_jpn.md">🇯🇵 日本語</a>
</p>

### Nodo gateway solare: legge inverter e batterie da fino a dieci porte seriali (firmware ESP32-S3, il suo pannello web e la libreria dei protocolli)

<p align="center">
  <img src="https://img.shields.io/badge/License-GPL%203.0-blue.svg" alt="GPL 3.0">
  <img src="https://img.shields.io/badge/Language-C%2B%2B17-00599c.svg" alt="Language">
  <img src="https://img.shields.io/badge/Devices-Voltronic%20%C2%B7%20Pylontech-ffb020.svg" alt="Devices">
  <img src="https://img.shields.io/badge/Maturity-scaffolding-FFB020.svg" alt="Maturity">
</p>

---

**Controllo di onestà - cosa funziona oggi:** **Maturità: scaffolding.** Il firmware si compila nel container ESP-IDF 5.4.2, il suo nucleo (le impostazioni, lo scambio con ogni tipo di dispositivo, l'aritmetica dell'UART emulata: 600 controlli) è testato su computer con dispositivi simulati, i messaggi che produce sono accettati da ARMOR-COMMON e il pannello è stato provato in un browser contro un nodo simulato. **Non ha mai girato su una scheda e nessun inverter o batteria è stato collegato**: il Wi-Fi, il pannello con TLS, l'aggiornamento, le UART hardware ed emulate e i formati dei protocolli (scritti da documenti pubblici e a memoria) non sono mai stati provati. I frame ANT-BMS dei test sono stati catturati da altri sui loro apparecchi: questo progetto non ne ha letto nessuno di persona.

---

## 🎯 Panoramica

* **Due schede, un solo firmware:** una ESP32-S3-WROOM-1 N16R8 via Wi-Fi (predefinita) e la Waveshare ESP32-S3-ETH sul suo cavo Ethernet (DHCP o indirizzo fisso; tiene la propria rete Wi-Fi per raggiungerla da un telefono). L'immagine si sceglie in compilazione (`tools/build_node.sh generic s3-wifi` o `generic s3-eth`); la tabella dei pin, i pin predefiniti delle porte e l'accesso seguono la scheda, e un'immagine vale solo per la sua scheda. Entrambe compilano e sono testate sul computer; nessuna ha girato su una scheda.
* **Il firmware del nodo** (ESP32-S3-WROOM-1 N16R8 via Wi-Fi, o Waveshare ESP32-S3-ETH via cavo): dieci porte seriali, tre UART hardware e sette emulate (fino a 19200 baud), ognuna indipendente e ognuna legge un inverter, una batteria o un monitor grezzo, così un nodo può leggere solo inverter, solo batterie o un misto.
* **Il pannello web del nodo,** lo stesso dei nodi radar: configurazione con un codice mostrato sulla console USB, accesso e utenti, Wi-Fi (stazione e punto di accesso), broker, aggiornamento via radio con ripristino, registro, HTTPS, e le pagine delle porte (con ciò che ognuna sente, per protocolli non ancora decodificati) e delle letture, in sette lingue.
* **Inverter Voltronic / MPP Solar** (Axpert, PIP, InfiniSolar e cloni), RS232 a 2400 baud: i frame e il loro CRC, e le letture `QPIGS` (rete, uscita, batteria, FV, bit di stato), `QMOD` (modo), `QPIWS` (avvisi e guasti per nome) e `QPIRI` (valori nominali). Si possono costruire solo comandi di lettura: un'impostazione cambia come viene alimentata la casa.
* **Batterie Pylontech** (US2000, US3000, US5000): la tabella `pwr` della console (tensione, corrente, temperature e stato di carica di ogni modulo), `bat <n>` (tensione e temperatura di ogni cella) e `info <n>` (modello, capacità residua e totale, cicli), riassunte come un unico pacco con capacità ed energia totali, e il frame RS485 con i suoi due controlli. I formati sono scritti a memoria dalla console pubblica e possono variare tra i firmware.
* **I messaggi del nodo** (`armor/solar/<nodo>/<dispositivo>/state`, uno per inverter o pacco di batterie), definiti in ARMOR-COMMON con schemi e vettori di conformità; ciò che producono le porte è verificato contro di essi.
* **Batterie ANT-BMS** (le schede nere dei pacchi autocostruiti, da 7S a 32S), UART a 3,3 V a 19200 baud: entrambi i protocolli del suo firmware (il nodo chiede nell'uno e poi nell'altro e resta con quello che risponde), con celle, temperature, stato di carica, corrente, capacità e stati dei MOSFET e del bilanciatore, verificati su frame catturati su quattro modelli reali. Solo lettura: i suoi comandi di scrittura possono scollegare una batteria sotto carico. Un pulsante della pagina delle porte legge anche modello, versione e le 56 impostazioni di protezione e bilanciamento del BMS (protocollo nuovo), in sola lettura.
* **Non ancora:** il collegamento Bluetooth dell'ANT-BMS (il nodo usa il cavo, il più stabile dei due), la scheda Android e una prova su una scheda reale con dispositivi reali.

## 📂 Struttura del repository

```text
ARMOR-SOLAR/
├── main/    the ESP-IDF component: app_main, solar_manager (one task per port), uart_ports, network, web_server, api_shared, mqtt_link, node_store, tls_cert
├── core/    voltronic, pylontech, ant_bms, solar_json, json + solar_config, poller, soft_uart, netplan, auth, board_s3 (no hardware in them)
├── panel/   the web panel: index.html, app.js, text.js (7 languages), style.css
├── tools/   build_node.sh, pack_panel.py, panel_mock.mjs
├── tests/   test_solar.cpp, test_node.cpp, emit_samples.cpp, emit_poller_samples.cpp, check_samples.py
└── docs/    NODE_FIRMWARE, NODE_HARDWARE, PROTOCOLS, SOLAR_MESSAGES, STUDIO_MENUS
```

## 🛠️ Ambiente di sviluppo

```bash
cmake -S tests -B build/host && cmake --build build/host
build/host/test_solar && build/host/test_node && build/host/test_board_eth      # 600 checks, -Werror
build/host/emit_poller_samples | python tests/check_samples.py   # what the ports make is accepted by ARMOR-COMMON
tools/build_node.sh generic                       # the firmware image for the N16R8 board in the ESP-IDF container: dist/generic-s3-wifi.bin
tools/build_node.sh generic s3-eth               # the same firmware for the Waveshare ESP32-S3-ETH (Ethernet): dist/generic-s3-eth.bin
node tools/panel_mock.mjs --user admin:adminpass123   # the panel without a board
```

Vedi la [guida al firmware](docs/NODE_FIRMWARE.md) (la scheda, le porte, il primo avvio e l'elenco delle prove da banco) e le [note di cablaggio](docs/NODE_HARDWARE.md).

## 🔗 Progetti correlati

**A.R.M.O.R.** (Autonomous Radar & Multimodal Observation Range) è un sistema di sicurezza perimetrale fatto di repository indipendenti. Ognuno ha la propria versione, i propri test e il proprio README; ecco la famiglia:

* **[ARMOR-COMMON](../ARMOR-COMMON)** - Contratti dei messaggi, validatori, vettori di conformità e tipi generati
* **[ARMOR-RADAR](../ARMOR-RADAR)** - Firmware del nodo di campo per ESP32-S3 con tre radar e un proprio pannello web
* **ARMOR-SOLAR** (questo repository) - Protocolli di inverter e batterie solari e messaggi di un nodo gateway
* **[ARMOR-SERVER](../ARMOR-SERVER)** - Coordinatore centrale: telemetria, allarmi, dispositivi, letture solari e telecamere
* **[ARMOR-STUDIO](../ARMOR-STUDIO)** - Console web: telecamere, radar, allarmi, energia solare e progettista del sito 2D/3D
* **[ARMOR-ANDROID-CONTROL](../ARMOR-ANDROID-CONTROL)** - Client Android dell'operatore con radar 2D/3D in tempo reale
* **[ARMOR-SERVER-AI](../ARMOR-SERVER-AI)** - Politica di inferenza visiva che spiega le sue decisioni e non agisce mai
* **[ARMOR-VOICE-AI](../ARMOR-VOICE-AI)** - Intenti vocali offline con una conferma impossibile da falsificare
* **[ARMOR-HARDWARE](../ARMOR-HARDWARE)** - Contenitori, elettronica e matrice di accettazione da banco
* **[ARMOR-DEVOPS](../ARMOR-DEVOPS)** - Distribuzione, banco di prova CM5, backup e TLS
* **[ARMOR-SIMULATOR](../ARMOR-SIMULATOR)** - Simulatore di telemetria offline con guasti ripetibili
* **[ARMOR-DOCS](../ARMOR-DOCS)** - Architettura, base di sicurezza e matrice delle capacità

## 📚 Documentazione e comunità

Dove leggere di più:

* [Matrice delle capacità: cosa è provato e cosa no](../ARMOR-DOCS/docs/CAPABILITY_MATRIX.md)
* [Catalogo dei progetti: versioni e dipendenze tra i repository](../ARMOR-DOCS/docs/PROJECT_CATALOG.md)
* [Cronologia delle modifiche di questo repository](CHANGELOG.md)
* [Licenza (GPL-3.0-or-later)](LICENSE)
* Domande, idee e segnalazioni: electrohobby3d@gmail.com

## 👤 AUTORE

**JuanenRac (Electro Hobby 3D)** · electrohobby3d@gmail.com

## 📜 LICENZA

GPL-3.0-or-later - vedi [LICENSE](LICENSE).

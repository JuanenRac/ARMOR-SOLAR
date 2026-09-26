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

### Monitoraggio di inverter e batterie solari: i protocolli seriali e i messaggi di un nodo gateway

<p align="center">
  <img src="https://img.shields.io/badge/License-GPL%203.0-blue.svg" alt="GPL 3.0">
  <img src="https://img.shields.io/badge/Language-C%2B%2B17-00599c.svg" alt="Language">
  <img src="https://img.shields.io/badge/Devices-Voltronic%20%C2%B7%20Pylontech-ffb020.svg" alt="Devices">
  <img src="https://img.shields.io/badge/Maturity-scaffolding-FFB020.svg" alt="Maturity">
</p>

---

**Controllo di onestà - cosa funziona oggi:** **Maturità: scaffolding.** La libreria di protocolli (100 controlli) decodifica gli inverter Voltronic / MPP Solar e la console Pylontech, cella per cella e con le capacità, da risposte tipiche scritte a mano. I messaggi che stampa sono validati da ARMOR-COMMON, letti da ARMOR-SERVER e mostrati nei menu Inverter e Batterie di Studio, tutto con letture generate. **Nessun inverter né batteria è stato collegato**, non c'è ancora il firmware del nodo e ANT-BMS non è decodificato perché il suo documento di protocollo non è nel progetto.

---

## 🎯 Panoramica

* **Inverter Voltronic / MPP Solar** (Axpert, PIP, InfiniSolar e cloni), RS232 a 2400 baud: i frame e il loro CRC, e le letture `QPIGS` (rete, uscita, batteria, FV, bit di stato), `QMOD` (modo), `QPIWS` (avvisi e guasti per nome) e `QPIRI` (valori nominali). Si possono costruire solo comandi di lettura: un'impostazione cambia come viene alimentata la casa.
* **Batterie Pylontech** (US2000, US3000, US5000): la tabella `pwr` della console (tensione, corrente, temperature e stato di carica di ogni modulo), `bat <n>` (tensione e temperatura di ogni cella) e `info <n>` (modello, capacità residua e totale, cicli), riassunte come un unico pacco con capacità ed energia totali, e il frame RS485 con i suoi due controlli. I formati sono scritti a memoria dalla console pubblica e possono variare tra i firmware.
* **I messaggi di un nodo gateway** (`armor/solar/<nodo>/<dispositivo>/state`, uno per inverter o pacco di batterie), definiti in ARMOR-COMMON con schemi e vettori di conformità, serializzati qui e verificati da uno script.
* **Il progetto del resto:** quale scheda (solo Wi-Fi o Ethernet con PoE), come cablare RS232 e RS485 in sicurezza (conversione di livello e isolamento) e l'ordine in cui sono stati costruiti i menu di Studio.
* **Non ancora:** ANT-BMS, il firmware del nodo e la scheda Android. Nulla è stato collegato a un dispositivo reale.

## 📂 Struttura del repository

```text
ARMOR-SOLAR/
├── core/    voltronic.hpp, pylontech.hpp, solar_json.hpp, json.hpp
├── tests/   test_solar.cpp, emit_samples.cpp, check_samples.py
└── docs/    PROTOCOLS.md, NODE_HARDWARE.md, SOLAR_MESSAGES.md, STUDIO_MENUS.md
```

## 🛠️ Ambiente di sviluppo

```bash
cmake -S tests -B build/host && cmake --build build/host
build/host/test_solar                                # 100 checks, -Werror
build/host/emit_samples | python tests/check_samples.py   # the messages have the fields of contract version 0
```

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

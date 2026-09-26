<p align="center">
  <img src="images/ARMOR_BANNER.svg" alt="ARMOR-SOLAR banner" width="100%">
</p>

# ☀️ ARMOR-SOLAR

<p align="center">
  <a href="README.md">🇺🇸 English</a> |
  <a href="README_spa.md">🇪🇸 Español</a> |
  🇫🇷 <b>Français</b> |
  <a href="README_ita.md">🇮🇹 Italiano</a> |
  <a href="README_deu.md">🇩🇪 Deutsch</a> |
  <a href="README_zho.md">🇨🇳 简体中文</a> |
  <a href="README_jpn.md">🇯🇵 日本語</a>
</p>

### Nœud passerelle solaire : lit onduleurs et batteries par jusqu'à dix ports série (firmware ESP32-S3, son panneau web et la bibliothèque de protocoles)

<p align="center">
  <img src="https://img.shields.io/badge/License-GPL%203.0-blue.svg" alt="GPL 3.0">
  <img src="https://img.shields.io/badge/Language-C%2B%2B17-00599c.svg" alt="Language">
  <img src="https://img.shields.io/badge/Devices-Voltronic%20%C2%B7%20Pylontech-ffb020.svg" alt="Devices">
  <img src="https://img.shields.io/badge/Maturity-scaffolding-FFB020.svg" alt="Maturity">
</p>

---

**Vérification d'honnêteté - ce qui fonctionne aujourd'hui:** **Maturité : scaffolding.** Le firmware se compile dans le conteneur ESP-IDF 5.4.2, son cœur (les réglages, l'échange avec chaque type d'équipement, l'arithmétique de l'UART émulé : 697 contrôles) est testé sur ordinateur avec des équipements simulés, les messages qu'il produit sont acceptés par ARMOR-COMMON et le panneau a été essayé dans un navigateur face à un nœud simulé. **Il n'a jamais tourné sur une carte et aucun onduleur ni batterie n'a été connecté** : le Wi-Fi, le panneau en TLS, la mise à jour, les UART matériels et émulés et les formats des protocoles (écrits d'après des documents publics et de mémoire) sont inessayés. Les trames ANT-BMS des tests ont été capturées par d'autres sur leurs propres appareils : ce projet n'en a lu aucune lui-même.

---

## 🎯 Présentation

* **Deux cartes, un firmware :** une ESP32-S3-WROOM-1 N16R8 en Wi-Fi (par défaut) et la Waveshare ESP32-S3-ETH sur son câble Ethernet (DHCP ou adresse fixe ; son propre réseau Wi-Fi est conservé pour la joindre depuis un téléphone). L'image se choisit à la compilation (`tools/build_node.sh generic s3-wifi` ou `generic s3-eth`) ; la table des broches, les broches par défaut des ports et l'accès suivent la carte, et une image ne vaut que pour sa carte. Les deux compilent et sont testées sur ordinateur ; aucune n'a tourné sur une carte.
* **Le firmware du nœud** (ESP32-S3-WROOM-1 N16R8 en Wi-Fi, ou Waveshare ESP32-S3-ETH sur câble) : dix ports série, trois UART matériels et sept émulés (jusqu'à 19200 bauds), chacun indépendant et lisant un onduleur, une batterie ou un moniteur brut, si bien qu'un nœud peut lire seulement des onduleurs, seulement des batteries ou un mélange.
* **Le panneau web du nœud,** celui des nœuds radar aussi : configuration avec un code affiché sur la console USB, connexion et utilisateurs, Wi-Fi (station et point d'accès), broker, mise à jour par radio avec retour arrière, journal, HTTPS, et les pages des ports (avec ce que chacun entend, pour les protocoles pas encore décodés) et des relevés, en sept langues.
* **Onduleurs Voltronic / MPP Solar** (Axpert, PIP, InfiniSolar et clones), RS232 à 2400 bauds : les trames et leur CRC, et les relevés `QPIGS` (réseau, sortie, batterie, PV, bits d'état), `QMOD` (mode), `QPIWS` (avertissements et pannes par nom) et `QPIRI` (valeurs nominales). Seules les commandes de lecture peuvent être construites : un réglage change la façon dont la maison est alimentée. Trois dialectes, choisis sur le port ou trouvés par lui (*auto*) : PI30, REVO (un autre `QPIGS`, réponses avec somme de contrôle) et PI18 (`^P005GS` : InfiniSolar V, LV5048, SunGoldPower), en lecture seule.
* **Batteries Pylontech** (US2000, US3000, US5000) : le tableau `pwr` de la console (tension, courant, températures et état de charge de chaque module), `bat <n>` (tension et température de chaque cellule) et `info <n>` (modèle, capacité restante et totale, cycles), résumés en une pile avec sa capacité et son énergie totales, et la trame RS485 avec ses deux contrôles. Les formats sont écrits de mémoire de la console publique et peuvent varier selon les firmwares. Les colonnes sont trouvées par les noms de l'en-tête (y compris le format de l'US5000 V2.3 avec ses colonnes Id, MosTempr et SysAlarm.St), la charge restante et l'équilibrage viennent de `bat`, et le modèle, la capacité nominale et les cycles de `info` et `stat`, demandés une fois et gardés une demi-heure.
* **Les messages du nœud** (`armor/solar/<nœud>/<appareil>/state`, un par onduleur ou pile de batteries), définis dans ARMOR-COMMON avec schémas et vecteurs de conformité ; ce que produisent les ports est vérifié contre eux.
* **Batteries ANT-BMS** (les cartes noires des packs faits maison, 7S à 32S), UART 3,3 V à 19200 bauds : les deux protocoles de son firmware (le nœud demande dans l'un puis dans l'autre et garde celui qui répond), avec les cellules, les températures, l'état de charge, le courant, les capacités et les états des MOSFET et de l'équilibreur, vérifiés sur des trames capturées sur quatre modèles réels. Lecture seule : ses commandes d'écriture peuvent déconnecter une batterie en charge. Un bouton de la page des ports lit aussi le modèle, la version et les 56 réglages de protection et d'équilibrage du BMS (protocole récent), en lecture seule.
* **Pas encore :** la liaison Bluetooth de l'ANT-BMS (le nœud utilise le câble, le plus stable des deux) et un essai sur une vraie carte avec de vrais équipements.

## 📂 Structure du dépôt

```text
ARMOR-SOLAR/
├── main/    the ESP-IDF component: app_main, solar_manager (one task per port), uart_ports, network, web_server, api_shared, mqtt_link, node_store, tls_cert
├── core/    voltronic, pylontech, ant_bms, solar_json, json + solar_config, poller, soft_uart, netplan, auth, board_s3 (no hardware in them)
├── panel/   the web panel: index.html, app.js, text.js (7 languages), style.css
├── tools/   build_node.sh, pack_panel.py, panel_mock.mjs
├── tests/   test_solar.cpp, test_node.cpp, emit_samples.cpp, emit_poller_samples.cpp, check_samples.py
└── docs/    NODE_FIRMWARE, NODE_HARDWARE, PROTOCOLS, SOLAR_MESSAGES, STUDIO_MENUS
```

## 🛠️ Environnement de développement

```bash
cmake -S tests -B build/host && cmake --build build/host
build/host/test_solar && build/host/test_node && build/host/test_board_eth      # 600 checks, -Werror
build/host/emit_poller_samples | python tests/check_samples.py   # what the ports make is accepted by ARMOR-COMMON
tools/build_node.sh generic                       # the firmware image for the N16R8 board in the ESP-IDF container: dist/generic-s3-wifi.bin
tools/build_node.sh generic s3-eth               # the same firmware for the Waveshare ESP32-S3-ETH (Ethernet): dist/generic-s3-eth.bin
node tools/panel_mock.mjs --user admin:adminpass123   # the panel without a board
```

Voir le [guide du firmware](docs/NODE_FIRMWARE.md) (la carte, les ports, le premier démarrage et la liste d'essais sur banc) et les [notes de câblage](docs/NODE_HARDWARE.md).

## 🔗 Projets liés

**A.R.M.O.R.** (Autonomous Radar & Multimodal Observation Range) est un système de sécurité périmétrique composé de dépôts indépendants. Chacun a sa propre version, ses propres tests et son propre README ; voici la famille :

* **[ARMOR-COMMON](../ARMOR-COMMON)** - Contrats de messages, validateurs, vecteurs de conformité et types générés
* **[ARMOR-RADAR](../ARMOR-RADAR)** - Firmware du nœud de terrain pour ESP32-S3 avec trois radars et son propre panneau web
* **ARMOR-SOLAR** (ce dépôt) - Protocoles des onduleurs et batteries solaires et messages d'un nœud passerelle
* **[ARMOR-SERVER](../ARMOR-SERVER)** - Coordinateur central : télémétrie, alarmes, appareils, relevés solaires et caméras
* **[ARMOR-STUDIO](../ARMOR-STUDIO)** - Console web : caméras, radar, alarmes, énergie solaire et concepteur de site 2D/3D
* **[ARMOR-ANDROID-CONTROL](../ARMOR-ANDROID-CONTROL)** - Client Android de l'opérateur avec radar 2D/3D en direct
* **[ARMOR-SERVER-AI](../ARMOR-SERVER-AI)** - Politique d'inférence visuelle qui explique ses décisions et n'agit jamais
* **[ARMOR-VOICE-AI](../ARMOR-VOICE-AI)** - Intentions vocales hors ligne avec une confirmation impossible à falsifier
* **[ARMOR-HARDWARE](../ARMOR-HARDWARE)** - Boîtiers, électronique et matrice d'acceptation sur banc
* **[ARMOR-DEVOPS](../ARMOR-DEVOPS)** - Déploiement, banc d'essai CM5, sauvegarde et TLS
* **[ARMOR-SIMULATOR](../ARMOR-SIMULATOR)** - Simulateur de télémétrie hors ligne avec des pannes reproductibles
* **[ARMOR-DOCS](../ARMOR-DOCS)** - Architecture, base de sécurité et matrice des capacités

## 📚 Documentation et communauté

Pour en savoir plus :

* [Matrice des capacités : ce qui est prouvé et ce qui ne l'est pas](../ARMOR-DOCS/docs/CAPABILITY_MATRIX.md)
* [Catalogue des projets : versions et dépendances entre les dépôts](../ARMOR-DOCS/docs/PROJECT_CATALOG.md)
* [Historique des modifications de ce dépôt](CHANGELOG.md)
* [Licence (GPL-3.0-or-later)](LICENSE)
* Questions, idées et rapports : electrohobby3d@gmail.com

## 👤 AUTEUR

**JuanenRac (Electro Hobby 3D)** · electrohobby3d@gmail.com

## 📜 LICENCE

GPL-3.0-or-later - voir [LICENSE](LICENSE).

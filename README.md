<p align="center">
  <img src="images/ARMOR_BANNER.svg" alt="ARMOR-SOLAR banner" width="100%">
</p>

# ☀️ ARMOR-SOLAR

<p align="center">
  🇺🇸 <b>English</b> |
  <a href="README_spa.md">🇪🇸 Español</a> |
  <a href="README_fra.md">🇫🇷 Français</a> |
  <a href="README_ita.md">🇮🇹 Italiano</a> |
  <a href="README_deu.md">🇩🇪 Deutsch</a> |
  <a href="README_zho.md">🇨🇳 简体中文</a> |
  <a href="README_jpn.md">🇯🇵 日本語</a>
</p>

### Solar gateway node: reads inverters and batteries through up to ten serial ports (ESP32-S3 firmware, its web panel and the protocol library)

<p align="center">
  <img src="https://img.shields.io/badge/License-GPL%203.0-blue.svg" alt="GPL 3.0">
  <img src="https://img.shields.io/badge/Language-C%2B%2B17-00599c.svg" alt="Language">
  <img src="https://img.shields.io/badge/Devices-Voltronic%20%C2%B7%20Pylontech-ffb020.svg" alt="Devices">
  <img src="https://img.shields.io/badge/Maturity-scaffolding-FFB020.svg" alt="Maturity">
</p>

---

**Honesty check - what runs today:** **Maturity: scaffolding.** The firmware builds in the ESP-IDF 5.4.2 container, its core (the settings, the exchange with each kind of equipment, the arithmetic of the emulated UART: 2,550 checks) is tested on a computer with stand-in equipment, the messages it makes are accepted by ARMOR-COMMON and the panel was exercised in a browser against a stand-in node. **It has never run on a board and no inverter or battery has been connected**: the Wi-Fi, the panel over TLS, the update, the hardware and emulated UARTs and the formats of the protocols (written from public documents and from memory) are untried. The ANT-BMS frames in its tests were captured by other people on their own units: this project has not read one itself.

---

## 🎯 Overview

* **Two boards, one firmware:** an ESP32-S3-WROOM-1 N16R8 on Wi-Fi (the default) and the Waveshare ESP32-S3-ETH on its Ethernet cable (DHCP or a fixed address; its own Wi-Fi network is kept to reach it from a phone). The image is chosen when it is built (`tools/build_node.sh generic s3-wifi` or `generic s3-eth`); the pin table, the default pins of the ports and the way in follow the board, and an image is only for its own board. Both build and are host-tested; neither has run on a board.
* **The firmware of the node** (ESP32-S3-WROOM-1 N16R8 on Wi-Fi, or the Waveshare ESP32-S3-ETH on its cable): ten serial ports, three hardware UARTs and seven emulated ones (up to 19200 baud), each independent and each reading an inverter, a battery or a raw monitor, so a node may read only inverters, only batteries or a mix.
* **The node's web panel,** the radar nodes' too: set-up with a code shown on the USB console, login and users, Wi-Fi (station and access point), broker, update over the air with rollback, log, HTTPS, and the pages of the ports (with what each one hears, for protocols not decoded yet) and of the readings, in seven languages.
* **Voltronic / MPP Solar inverters** (Axpert, PIP, InfiniSolar and clones), RS232 at 2400 baud: the frames and their CRC, and the readings `QPIGS` (grid, output, battery, PV, status bits), `QMOD` (mode), `QPIWS` (warnings and faults by name) and `QPIRI` (ratings). Only reading commands can be built: a setting changes how the house is fed. Three dialects, chosen on the port or found by it (*auto*): PI30, REVO (another `QPIGS`, checksum replies) and PI18 (`^P005GS`: InfiniSolar V, LV5048, SunGoldPower), read only.
* **Pylontech batteries** (US2000, US3000, US5000): the console's `pwr` table (every module's voltage, current, temperatures and state of charge), `bat <n>` (the voltage and temperature of every cell) and `info <n>` (model, remaining and full capacity, cycles), summarised as one stack with its total capacity and energy, and the RS485 frame with its two checks. The formats are written from memory of the public console and may differ between firmwares. The columns are found by the header's names (the US5000 V2.3 layout with its Id columns, MosTempr and SysAlarm.St included), the remaining charge and the balancing come from `bat`, and the model, rated capacity and cycles from `info` and `stat`, asked once and kept for half an hour.
* **The messages of the node** (`armor/solar/<node>/<device>/state`, one per inverter or battery stack), defined in ARMOR-COMMON with schemas and conformance vectors; what the ports make is checked against them.
* **ANT-BMS batteries** (the black boards of home-made packs, 7S to 32S), 3.3 V UART at 19200 baud: both protocols of its firmware (the node asks in one and then in the other and keeps to the one that answers), with the cells, temperatures, state of charge, current, capacities and the states of the MOSFETs and the balancer, checked against frames captured on four real models. Reading only: its write commands can disconnect a battery under load. A button on the ports page also reads the BMS's model, version and 56 protection and balancing settings (newer protocol), read only.
* **Configuration from a phone over Bluetooth,** the same channel as the radar node's: the ARMOR app finds the node as `ARMOR-XXXXXX` and sets its name, Wi-Fi, address, broker and Bluetooth mode with the panel's users and set-up code ([the protocol](docs/BLE_PROVISIONING.md)). It listens only while the node has no user, unless told otherwise. The radio side has never run on a board.
* **The base board with multiplexers (the *mux* profile):** three hardware UARTs, each behind a 74HC4052, serve up to eight ports in groups of 4, 2 and 2 that take turns on their line, each port with its own speed and polarity, and one LED per port through a 74HC595; the cells of a tall Pylontech stack can be read in rotation. For an inverter in the standard dialect, the **second PV input** (`QPIGS2`) and the **units of a parallel system** (`QPGS`) are optional readings. None of it has run on a board.
* **Not yet:** the ANT-BMS's Bluetooth link (the node uses the cable, the more stable of the two) and a run on a real board with real equipment.

## 📂 Repository Structure

```text
ARMOR-SOLAR/
├── main/    the ESP-IDF component: app_main, solar_manager (one task per port, or per group in the mux profile), uart_ports, mux_board, port_leds_hw, network, web_server, api_shared, mqtt_link, node_store, tls_cert, ble_provision, board_ethernet
├── core/    voltronic, voltronic_pi18, pylontech, ant_bms, ant_registers, ant_settings, solar_json + solar_config, poller, soft_uart, netplan, auth, board_s3, ble_frame, ble_dispatch, mux_group, port_leds, console_probe, json (no hardware in them)
├── panel/   the web panel: index.html, app.js, text.js (7 languages), style.css
├── tools/   build_node.sh, pack_panel.py, panel_mock.mjs, panel_browser_test.mjs
├── tests/   test_solar.cpp, test_node.cpp, test_board_eth.cpp, test_ble.cpp, test_mux.cpp, test_parallel.cpp, test_console.cpp, emit_samples.cpp, emit_poller_samples.cpp, check_samples.py (+ the ANT-BMS frames)
└── docs/    NODE_FIRMWARE, NODE_HARDWARE, BLE_PROVISIONING, PROTOCOLS, SOLAR_MESSAGES, STUDIO_MENUS
```

## 🛠️ Development Environment

```bash
cmake -S tests -B build/host && cmake --build build/host
build/host/test_solar && build/host/test_node && build/host/test_board_eth && build/host/test_ble && build/host/test_mux && build/host/test_parallel && build/host/test_console      # 2,550 checks, -Werror
build/host/emit_poller_samples | python tests/check_samples.py   # what the ports make is accepted by ARMOR-COMMON
tools/build_node.sh generic                       # the firmware image for the N16R8 board in the ESP-IDF container: dist/generic-s3-wifi.bin
tools/build_node.sh generic s3-eth               # the same firmware for the Waveshare ESP32-S3-ETH (Ethernet): dist/generic-s3-eth.bin
node tools/panel_mock.mjs --user admin:adminpass123   # the panel without a board
```

See the [firmware guide](docs/NODE_FIRMWARE.md) (the board, the ports, the first start and the bench checklist) and the [wiring notes](docs/NODE_HARDWARE.md).

## 🔗 Related Projects

**A.R.M.O.R.** (Autonomous Radar & Multimodal Observation Range) is a perimeter-security system made of independent repositories. Each one has its own version, its own tests and its own README; this is the family:

* **[ARMOR-COMMON](https://github.com/JuanenRac/ARMOR-COMMON)** - Message contracts, validators, conformance vectors and generated types
* **[ARMOR-RADAR](https://github.com/JuanenRac/ARMOR-RADAR)** - Field-node firmware for ESP32-S3 with three radars and its own web panel
* **ARMOR-SOLAR** (this repository) - Solar inverter and battery protocols and the messages of a gateway node
* **[ARMOR-ELECTRICAL](https://github.com/JuanenRac/ARMOR-ELECTRICAL)** - Electrical node: meters, the message of the network's readings and the rules for switching
* **[ARMOR-HMI](https://github.com/JuanenRac/ARMOR-HMI)** - Touch panel: the state of the system on a wall screen, arming and acknowledging, and the home of the voice assistant
* **[ARMOR-NETWORK](https://github.com/JuanenRac/ARMOR-NETWORK)** - The local network: its devices, the internet and what changes
* **[ARMOR-SERVER](https://github.com/JuanenRac/ARMOR-SERVER)** - Central coordinator: telemetry, alarms, devices, solar readings and cameras
* **[ARMOR-STUDIO](https://github.com/JuanenRac/ARMOR-STUDIO)** - Web console: cameras, radar, alarms, solar energy and the 2D/3D site designer
* **[ARMOR-ANDROID-CONTROL](https://github.com/JuanenRac/ARMOR-ANDROID-CONTROL)** - Android operator client with a live 2D/3D radar
* **[ARMOR-SERVER-AI](https://github.com/JuanenRac/ARMOR-SERVER-AI)** - Visual inference policy that explains its decisions and never actuates
* **[ARMOR-VOICE-AI](https://github.com/JuanenRac/ARMOR-VOICE-AI)** - Offline voice intents with a confirmation that cannot be forged
* **[ARMOR-HARDWARE](https://github.com/JuanenRac/ARMOR-HARDWARE)** - Enclosures, electronics and the bench acceptance matrix
* **[ARMOR-DEVOPS](https://github.com/JuanenRac/ARMOR-DEVOPS)** - Deployment, the CM5 test bench, backup and TLS
* **[ARMOR-SIMULATOR](https://github.com/JuanenRac/ARMOR-SIMULATOR)** - Offline telemetry simulator with repeatable faults
* **[ARMOR-UPDATER](https://github.com/JuanenRac/ARMOR-UPDATER)** - Detects, installs and updates the ecosystem's own repositories
* **[ARMOR-DOCS](https://github.com/JuanenRac/ARMOR-DOCS)** - Architecture, security baseline and the capability matrix

## 📚 Documentation & Community

Where to read more:

* [Capability matrix: what is proven and what is not](https://github.com/JuanenRac/ARMOR-DOCS/blob/main/docs/CAPABILITY_MATRIX.md)
* [Project catalogue: versions and how the repositories depend on each other](https://github.com/JuanenRac/ARMOR-DOCS/blob/main/docs/PROJECT_CATALOG.md)
* [Changelog of this repository](CHANGELOG.md)
* [License (GPL-3.0-or-later)](LICENSE)
* Questions, ideas and reports: electrohobby3d@gmail.com

## 👤 AUTHOR

**JuanenRac (Electro Hobby 3D)** · electrohobby3d@gmail.com

## 📜 LICENSE

GPL-3.0-or-later - see [LICENSE](LICENSE).

<p align="center">
  <img src="images/ARMOR_BANNER.svg" alt="ARMOR-SOLAR banner" width="100%">
</p>

# ☀️ ARMOR-SOLAR

<p align="center">🇺🇸 <b>English</b> | <a href="README_spa.md">🇪🇸 Español</a></p>

### Solar inverter and battery monitoring: the serial protocols, the gateway-node design and the plan for Studio's solar menus

<p align="center">
  <img src="https://img.shields.io/badge/License-GPL%203.0-blue.svg" alt="GPL 3.0">
  <img src="https://img.shields.io/badge/Language-C%2B%2B17-00599c.svg" alt="Language">
  <img src="https://img.shields.io/badge/Devices-Voltronic%20%C2%B7%20Pylontech-ffb020.svg" alt="Devices">
  <img src="https://img.shields.io/badge/Maturity-scaffolding-FFB020.svg" alt="Maturity">
</p>

---

**Honesty check - what runs today:** **Maturity: scaffolding.** The protocol library (83 checks) decodes the Voltronic / MPP Solar inverters and the Pylontech console from typical replies written by hand, and its messages are checked by a script. **No inverter or battery has been connected**, there is no node firmware, the server does not read these messages and Studio has no solar menu yet; ANT-BMS is not decoded because its protocol document is not in the project.

---

## 1. 🛠️ OVERVIEW

* **Voltronic / MPP Solar inverters** (Axpert, PIP, InfiniSolar and clones), RS232 at 2400 baud: the frames and their CRC, and the readings `QPIGS` (grid, output, battery, PV, status bits), `QMOD` (mode), `QPIWS` (warnings and faults by name) and `QPIRI` (ratings). Only reading commands can be built: a setting changes how the house is fed.
* **Pylontech batteries** (US2000, US3000, US5000): the table of the console's `pwr` command (every module's voltage, current, temperatures, cell range, state of charge and states), summarised as one stack, and the RS485 frame with its two checks.
* **The messages of a gateway node**, contract version 0 (`armor/solar/<node>/<device>/state`, one per inverter or battery stack), serialised and checked by a script.
* **The design of the rest:** which board (Wi-Fi only or Ethernet with PoE), how to wire RS232 and RS485 safely (level conversion and isolation), and the Studio menus (energy flow, inverters, batteries, alarms) in the order they should be built.
* **Not yet:** ANT-BMS (its protocol document is not in the project), the firmware of the node, the server and Studio. Nothing has been connected to a real device.

---

## 2. 🔧 BUILD & RUN

```bash
cmake -S tests -B build/host && cmake --build build/host
build/host/test_solar                                # 83 checks, -Werror
build/host/emit_samples | python tests/check_samples.py   # the messages have the fields of contract version 0
```

---

## 📂 DIRECTORY STRUCTURE

```text
ARMOR-SOLAR/
├── core/    voltronic.hpp, pylontech.hpp, solar_json.hpp, json.hpp
├── tests/   test_solar.cpp, emit_samples.cpp, check_samples.py
└── docs/    PROTOCOLS.md, NODE_HARDWARE.md, SOLAR_MESSAGES.md, STUDIO_MENUS.md
```

---

## 👤 AUTHOR

**JuanenRac (Electro Hobby 3D)** · electrohobby3d@gmail.com

## 📜 LICENSE

GPL-3.0-or-later - see [LICENSE](LICENSE).

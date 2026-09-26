<p align="center">
  <img src="images/ARMOR_BANNER.svg" alt="ARMOR-SOLAR banner" width="100%">
</p>

# ☀️ ARMOR-SOLAR

<p align="center"><a href="README.md">🇺🇸 English</a> | 🇪🇸 <b>Español</b></p>

### Monitorización de inversores solares y baterías: los protocolos serie, el diseño de los nodos pasarela y el plan de los menús solares de Studio

<p align="center">
  <img src="https://img.shields.io/badge/License-GPL%203.0-blue.svg" alt="GPL 3.0">
  <img src="https://img.shields.io/badge/Language-C%2B%2B17-00599c.svg" alt="Language">
  <img src="https://img.shields.io/badge/Devices-Voltronic%20%C2%B7%20Pylontech-ffb020.svg" alt="Devices">
  <img src="https://img.shields.io/badge/Maturity-scaffolding-FFB020.svg" alt="Maturity">
</p>

---

**Comprobación de honestidad - qué funciona hoy:** **Madurez: scaffolding.** La biblioteca de protocolos (83 comprobaciones) decodifica los inversores Voltronic / MPP Solar y la consola de Pylontech a partir de respuestas típicas escritas a mano, y un script comprueba sus mensajes. **No se ha conectado ningún inversor ni batería**, no hay firmware del nodo, el servidor no lee estos mensajes y Studio aún no tiene menú solar; ANT-BMS no se decodifica porque su documento de protocolo no está en el proyecto.

---

## 1. 🛠️ DESCRIPCIÓN

* **Inversores Voltronic / MPP Solar** (Axpert, PIP, InfiniSolar y clones), RS232 a 2400 baudios: las tramas y su CRC, y las lecturas `QPIGS` (red, salida, batería, FV, bits de estado), `QMOD` (modo), `QPIWS` (avisos y fallos por nombre) y `QPIRI` (valores nominales). Solo se pueden construir comandos de lectura: un ajuste cambia cómo se alimenta la casa.
* **Baterías Pylontech** (US2000, US3000, US5000): la tabla del comando `pwr` de la consola (tensión, corriente, temperaturas, rango de celdas, estado de carga y estados de cada módulo), resumida como una pila, y la trama RS485 con sus dos comprobaciones.
* **Los mensajes de un nodo pasarela**, versión 0 del contrato (`armor/solar/<nodo>/<dispositivo>/state`, uno por inversor o pila de baterías), serializados y comprobados por un script.
* **El diseño del resto:** qué placa (solo Wi-Fi o Ethernet con PoE), cómo cablear RS232 y RS485 con seguridad (conversión de niveles y aislamiento) y los menús de Studio (flujo de energía, inversores, baterías, alarmas) en el orden en que deben construirse.
* **Todavía no:** ANT-BMS (su documento de protocolo no está en el proyecto), el firmware del nodo, el servidor y Studio. Nada se ha conectado a un dispositivo real.

---

## 2. 🔧 COMPILAR Y EJECUTAR

```bash
cmake -S tests -B build/host && cmake --build build/host
build/host/test_solar                                # 83 comprobaciones, -Werror
build/host/emit_samples | python tests/check_samples.py   # los mensajes tienen los campos de la versión 0 del contrato
```

---

## 📂 ESTRUCTURA DE DIRECTORIOS

```text
ARMOR-SOLAR/
├── core/    voltronic.hpp, pylontech.hpp, solar_json.hpp, json.hpp
├── tests/   test_solar.cpp, emit_samples.cpp, check_samples.py
└── docs/    PROTOCOLS.md, NODE_HARDWARE.md, SOLAR_MESSAGES.md, STUDIO_MENUS.md
```

---

## 👤 AUTOR

**JuanenRac (Electro Hobby 3D)** · electrohobby3d@gmail.com

## 📜 LICENCIA

GPL-3.0-or-later - véase [LICENSE](LICENSE).

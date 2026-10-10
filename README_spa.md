<p align="center">
  <img src="images/ARMOR_BANNER.svg" alt="ARMOR-SOLAR banner" width="100%">
</p>

# ☀️ ARMOR-SOLAR

<p align="center">
  <a href="README.md">🇺🇸 English</a> |
  🇪🇸 <b>Español</b> |
  <a href="README_fra.md">🇫🇷 Français</a> |
  <a href="README_ita.md">🇮🇹 Italiano</a> |
  <a href="README_deu.md">🇩🇪 Deutsch</a> |
  <a href="README_zho.md">🇨🇳 简体中文</a> |
  <a href="README_jpn.md">🇯🇵 日本語</a>
</p>

### Nodo pasarela solar: lee inversores y baterías por hasta diez puertos serie (firmware para ESP32-S3, su panel web y la biblioteca de protocolos)

<p align="center">
  <img src="https://img.shields.io/badge/License-GPL%203.0-blue.svg" alt="GPL 3.0">
  <img src="https://img.shields.io/badge/Language-C%2B%2B17-00599c.svg" alt="Language">
  <img src="https://img.shields.io/badge/Devices-Voltronic%20%C2%B7%20Pylontech-ffb020.svg" alt="Devices">
  <img src="https://img.shields.io/badge/Maturity-scaffolding-FFB020.svg" alt="Maturity">
</p>

---

**Comprobación de honestidad - qué funciona hoy:** **Madurez: scaffolding.** El firmware se compila en el contenedor ESP-IDF 5.4.2, su núcleo (los ajustes, el intercambio con cada tipo de equipo, la aritmética de la UART emulada: 2,550 comprobaciones) está probado en un ordenador con equipos simulados, los mensajes que produce los acepta ARMOR-COMMON y el panel se probó en un navegador contra un nodo simulado. **Nunca ha funcionado en una placa y no se ha conectado ningún inversor ni batería**: el Wi-Fi, el panel con TLS, la actualización, las UART de hardware y emuladas y los formatos de los protocolos (escritos de documentos públicos y de memoria) están sin probar. Las tramas de ANT-BMS de sus pruebas las capturaron otras personas en sus propios equipos: este proyecto no ha leído ninguno.

---

## 🎯 Descripción general

* **Dos placas, un firmware:** una ESP32-S3-WROOM-1 N16R8 por Wi-Fi (la de serie) y la Waveshare ESP32-S3-ETH por su cable Ethernet (DHCP o una dirección fija; conserva su propia red Wi-Fi para llegar a ella desde un móvil). La imagen se elige al compilar (`tools/build_node.sh generic s3-wifi` o `generic s3-eth`); la tabla de pines, los pines por defecto de los puertos y la forma de acceso siguen a la placa, y una imagen es solo para su placa. Ambas compilan y tienen tests en el ordenador; ninguna ha funcionado en una placa.
* **El firmware del nodo** (ESP32-S3-WROOM-1 N16R8 por Wi-Fi, o Waveshare ESP32-S3-ETH por cable): diez puertos serie, tres UART de hardware y siete emuladas (hasta 19200 baudios), cada una independiente y cada una lee un inversor, una batería o un monitor en crudo, de modo que un nodo puede leer solo inversores, solo baterías o una mezcla.
* **El panel web del nodo,** el mismo de los nodos radar: configuración con un código que se muestra en la consola USB, acceso y usuarios, Wi-Fi (estación y punto de acceso), broker, actualización por aire con vuelta atrás, registro, HTTPS, y las páginas de los puertos (con lo que oye cada uno, para protocolos aún sin decodificar) y de las lecturas, en siete idiomas.
* **Inversores Voltronic / MPP Solar** (Axpert, PIP, InfiniSolar y clones), RS232 a 2400 baudios: las tramas y su CRC, y las lecturas `QPIGS` (red, salida, batería, FV, bits de estado), `QMOD` (modo), `QPIWS` (avisos y averías por nombre) y `QPIRI` (valores nominales). Solo se pueden construir comandos de lectura: un ajuste cambia cómo se alimenta la casa. Tres dialectos, elegidos en el puerto o encontrados por él (*auto*): PI30, REVO (otro `QPIGS`, respuestas con suma de comprobación) y PI18 (`^P005GS`: InfiniSolar V, LV5048, SunGoldPower), solo lectura.
* **Baterías Pylontech** (US2000, US3000, US5000): la tabla `pwr` de la consola (tensión, corriente, temperaturas y estado de carga de cada módulo), `bat <n>` (tensión y temperatura de cada celda) e `info <n>` (modelo, capacidad restante y total, ciclos), resumidas como una pila con su capacidad y energía totales, y la trama RS485 con sus dos comprobaciones. Los formatos están escritos de memoria de la consola pública y pueden variar entre firmwares. Las columnas se encuentran por los nombres de la cabecera (incluido el formato de la US5000 V2.3 con sus columnas Id, MosTempr y SysAlarm.St), la carga restante y el equilibrado salen de `bat`, y el modelo, la capacidad nominal y los ciclos de `info` y `stat`, que se piden una vez y se guardan media hora.
* **Los mensajes del nodo** (`armor/solar/<nodo>/<dispositivo>/state`, uno por inversor o pila de baterías), definidos en ARMOR-COMMON con esquemas y vectores de conformidad; lo que producen los puertos se comprueba contra ellos.
* **Baterías con ANT-BMS** (las placas negras de los packs caseros, de 7S a 32S), UART de 3,3 V a 19200 baudios: los dos protocolos de su firmware (el nodo pregunta en uno y luego en el otro y se queda con el que responde), con las celdas, temperaturas, estado de carga, corriente, capacidades y los estados de los MOSFET y del equilibrador, comprobados con tramas capturadas en cuatro modelos reales. Solo lectura: sus comandos de escritura pueden desconectar una batería en carga. Un botón de la página de puertos lee además el modelo, la versión y los 56 ajustes de protección y equilibrado de la BMS (protocolo nuevo), solo lectura.
* **Configuración desde el móvil por Bluetooth,** el mismo canal que el del nodo radar: la app ARMOR encuentra el nodo como `ARMOR-XXXXXX` y ajusta su nombre, Wi-Fi, dirección, broker y modo de Bluetooth con los usuarios y el código de puesta en marcha del panel ([el protocolo](docs/BLE_PROVISIONING.md)). Solo escucha mientras el nodo no tiene usuarios, salvo que se indique otra cosa. La parte de radio nunca ha corrido en una placa.
* **La placa base con multiplexores (perfil *mux*):** tres UART de hardware, cada una detrás de un 74HC4052, atienden hasta ocho puertos en grupos de 4, 2 y 2 que se turnan en su línea, cada puerto con su propia velocidad y polaridad, y un LED por puerto a través de un 74HC595; las celdas de una pila Pylontech alta pueden leerse por turnos. Para un inversor en el dialecto estándar, la **segunda entrada fotovoltaica** (`QPIGS2`) y las **unidades de un sistema en paralelo** (`QPGS`) son lecturas opcionales. Nada de esto ha corrido en una placa.
* **Todavía no:** el enlace Bluetooth del ANT-BMS (el nodo usa el cable, el más estable de los dos) y una prueba en una placa real con equipos reales.

## 📂 Estructura del repositorio

```text
ARMOR-SOLAR/
├── main/    el componente de ESP-IDF: app_main, solar_manager (una tarea por puerto, o por grupo en el perfil mux), uart_ports, mux_board, port_leds_hw, network, web_server, api_shared, mqtt_link, node_store, tls_cert, ble_provision, board_ethernet
├── core/    voltronic, voltronic_pi18, pylontech, ant_bms, ant_registers, ant_settings, solar_json + solar_config, poller, soft_uart, netplan, auth, board_s3, ble_frame, ble_dispatch, mux_group, port_leds, console_probe, json (sin hardware)
├── panel/   el panel web: index.html, app.js, text.js (7 idiomas), style.css
├── tools/   build_node.sh, pack_panel.py, panel_mock.mjs, panel_browser_test.mjs
├── tests/   test_solar.cpp, test_node.cpp, test_board_eth.cpp, test_ble.cpp, test_mux.cpp, test_parallel.cpp, test_console.cpp, emit_samples.cpp, emit_poller_samples.cpp, check_samples.py (+ las tramas del ANT-BMS)
└── docs/    NODE_FIRMWARE, NODE_HARDWARE, BLE_PROVISIONING, PROTOCOLS, SOLAR_MESSAGES, STUDIO_MENUS
```

## 🛠️ Entorno de desarrollo

```bash
cmake -S tests -B build/host && cmake --build build/host
build/host/test_solar && build/host/test_node && build/host/test_board_eth && build/host/test_ble && build/host/test_mux && build/host/test_parallel && build/host/test_console      # 2,550 checks, -Werror
build/host/emit_poller_samples | python tests/check_samples.py   # what the ports make is accepted by ARMOR-COMMON
tools/build_node.sh generic                       # the firmware image for the N16R8 board in the ESP-IDF container: dist/generic-s3-wifi.bin
tools/build_node.sh generic s3-eth               # the same firmware for the Waveshare ESP32-S3-ETH (Ethernet): dist/generic-s3-eth.bin
node tools/panel_mock.mjs --user admin:adminpass123   # the panel without a board
```

Véase la [guía del firmware](docs/NODE_FIRMWARE.md) (la placa, los puertos, el primer arranque y la lista de pruebas en banco) y las [notas de cableado](docs/NODE_HARDWARE.md).

## 🔗 Proyectos relacionados

**A.R.M.O.R.** (Autonomous Radar & Multimodal Observation Range) es un sistema de seguridad perimetral hecho de repositorios independientes. Cada uno tiene su propia versión, sus propias pruebas y su propio README; esta es la familia:

* **[ARMOR-COMMON](https://github.com/JuanenRac/ARMOR-COMMON)** - Contratos de mensajes, validadores, vectores de conformidad y tipos generados
* **[ARMOR-RADAR](https://github.com/JuanenRac/ARMOR-RADAR)** - Firmware del nodo de campo para ESP32-S3 con tres radares y su propio panel web
* **ARMOR-SOLAR** (este repositorio) - Protocolos de inversores y baterías solares y los mensajes de un nodo pasarela
* **[ARMOR-ELECTRICAL](https://github.com/JuanenRac/ARMOR-ELECTRICAL)** - Nodo eléctrico: contadores, el mensaje de las lecturas de la red y las reglas para maniobrar
* **[ARMOR-ALARM](https://github.com/JuanenRac/ARMOR-ALARM)** - Nodo y central de alarma: zonas, armado, retardos, sirena y PIN, con el servidor o sin él
* **[ARMOR-HMI](https://github.com/JuanenRac/ARMOR-HMI)** - Panel táctil: el estado del sistema en una pantalla de pared, armar y reconocer alarmas, y el hogar del asistente de voz
* **[ARMOR-NETWORK](https://github.com/JuanenRac/ARMOR-NETWORK)** - La red local: sus dispositivos, internet y lo que cambia
* **[ARMOR-SERVER](https://github.com/JuanenRac/ARMOR-SERVER)** - Coordinador central: telemetría, alarmas, dispositivos, lecturas solares y cámaras
* **[ARMOR-STUDIO](https://github.com/JuanenRac/ARMOR-STUDIO)** - Consola web: cámaras, radar, alarmas, energía solar y el diseñador de sitio 2D/3D
* **[ARMOR-ANDROID-CONTROL](https://github.com/JuanenRac/ARMOR-ANDROID-CONTROL)** - Cliente Android del operador con radar 2D/3D en vivo
* **[ARMOR-SERVER-AI](https://github.com/JuanenRac/ARMOR-SERVER-AI)** - Política de inferencia visual que explica sus decisiones y nunca actúa
* **[ARMOR-VOICE-AI](https://github.com/JuanenRac/ARMOR-VOICE-AI)** - Intenciones de voz sin conexión con una confirmación imposible de falsificar
* **[ARMOR-HARDWARE](https://github.com/JuanenRac/ARMOR-HARDWARE)** - Cajas, electrónica y la matriz de aceptación en banco
* **[ARMOR-DEVOPS](https://github.com/JuanenRac/ARMOR-DEVOPS)** - Despliegue, el banco de pruebas de la CM5, copias de seguridad y TLS
* **[ARMOR-SIMULATOR](https://github.com/JuanenRac/ARMOR-SIMULATOR)** - Simulador de telemetría sin conexión con fallos repetibles
* **[ARMOR-UPDATER](https://github.com/JuanenRac/ARMOR-UPDATER)** - Detecta, instala y actualiza los propios repositorios del ecosistema
* **[ARMOR-DOCS](https://github.com/JuanenRac/ARMOR-DOCS)** - Arquitectura, base de seguridad y la matriz de capacidades

## 📚 Documentación y comunidad

Dónde leer más:

* [Matriz de capacidades: qué está probado y qué no](https://github.com/JuanenRac/ARMOR-DOCS/blob/main/docs/CAPABILITY_MATRIX.md)
* [Catálogo de proyectos: versiones y cómo dependen unos de otros](https://github.com/JuanenRac/ARMOR-DOCS/blob/main/docs/PROJECT_CATALOG.md)
* [Historial de cambios de este repositorio](CHANGELOG.md)
* [Licencia (GPL-3.0-or-later)](LICENSE)
* Preguntas, ideas e informes: electrohobby3d@gmail.com

## 👤 AUTOR

**JuanenRac (Electro Hobby 3D)** · electrohobby3d@gmail.com

## 📜 LICENCIA

GPL-3.0-or-later - véase [LICENSE](LICENSE).

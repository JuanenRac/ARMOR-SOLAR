# The firmware of the solar node

The firmware of an **ESP32-S3-WROOM-1 N16R8** board (16 MB of flash, 8 MB of octal PSRAM, two USB-C sockets, no Ethernet: Wi-Fi only). It reads solar
inverters and batteries through up to **ten serial ports** and publishes their readings to A.R.M.O.R.'s broker (`armor/solar/<node>/<device>/state`, the
messages of ARMOR-COMMON). It has the same web panel as the radar nodes' (setup, login, users, Wi-Fi, broker, over-the-air update with rollback, log, the panel
over HTTPS) and the pages of its own: the ports and the readings.

**Nothing here has run on a board, and no inverter or battery has been connected.** What has been done: the firmware builds in the ESP-IDF 5.4.2 container
(a 1.2 MB image in a 3 MB slot), its core (settings, the exchange with each kind of equipment, the emulated UART's arithmetic) is tested on a computer with
stand-ins for the equipment, the messages it makes are accepted by ARMOR-COMMON, and the panel was exercised in a browser against a stand-in node.

## The board

The pin map used is the one printed for this board (two USB-C sockets: **USB & OTG**, the chip's native USB on GPIO 19 and 20, and **USB to serial**, a CH343P on
UART0's GPIO 43 and 44):

| GPIO | Use |
| --- | --- |
| 26 to 32 | The external flash: never offered |
| 33 to 37 | The octal PSRAM of the N16R8 module (35, 36 and 37 are on the header: do not use them on this module) |
| 19, 20 | The native USB: the log and the flashing |
| 0, 3, 45, 46 | Strapping pins (0 is the BOOT button): usable, but what is wired there must not pull them the wrong way while the chip starts |
| 43, 44 | UART0's pins to the USB-serial socket: usable, but a cable in that socket drives them |
| 48 | The on-board RGB LED |
| the rest of the header | Free |

The log comes out of the **USB & OTG** socket (the console is moved to the native USB so that UART0 is free for a device); flash through the same socket.

## The ten ports

| Port | What it is | Default pins RX, TX, DE |
| --- | --- | --- |
| 1 | hardware UART1 | 16, 15, 7 |
| 2 | hardware UART2 | 18, 17, 8 |
| 3 | hardware UART0 (on pins away from the USB-serial socket) | 4, 5, 6 |
| 4 to 10 | **emulated** (software) | 9 10 -; 11 12 -; 13 14 -; 21 47 -; 38 39 -; 40 41 -; 42 1 - |

Every port is 8N1 and independent: a node may read only inverters, only batteries, or a mix (an inverter on port 1, a battery stack on port 2, another inverter on
an emulated port...). Each enabled port has:

- **Equipment:** *Voltronic / MPP Solar inverter* (2400 baud, `QPIGS`, `QMOD`, `QPIWS`), *Pylontech battery* (115200 baud, the console's `pwr`, then `bat n` and
  `info n` of each module for its cells, capacities, cycles and model), *ANT-BMS battery* (19200 baud, one request and one frame back; both protocols of its firmware, the
  node finds which one) or *Raw monitor* (nothing is asked; what arrives is kept and shown as hexadecimal, to look at a protocol that is not decoded).
- **Device name:** a piece of the topic (`armor/solar/<node>/<name>/state`) and what Studio calls the equipment.
- **Speed**, **seconds between readings**, and for a battery how many modules to read cell by cell.
- **RX, TX and DE pins.** DE is the direction pin of an RS485 transceiver (high while the node sends; tie DE and RE together).

**Emulated ports** time the line by software: an interrupt on every edge of the RX pin notes the time and the bytes are worked out from those times, and a hardware
timer changes the TX pin at every bit. They are for slow lines: **up to 19200 baud** (the panel refuses more). The Pylontech console runs at 115200 baud, so it needs a
hardware port; the Voltronic inverters (2400 baud) and the ANT-BMS (19200 baud) fit an emulated one. The chip has four hardware timers only, so **the seven emulated
ports share one for sending** and one of them sends at a time (a request is a few bytes at a slow speed, and the others go on listening meanwhile); receiving needs no
timer, only the interrupt of each RX pin.

A port with a problem says so in the panel and stays out of the way of the others: *waiting* for its first reading, *reporting*, *silent* (nothing comes: check TX
and RX, the ground, the converter, the speed), *garbled* (bytes come but no answer is valid: wrong speed, missing converter, another protocol) or *error* (the port
could not be opened: unusable pins, a wrong speed). A reading is published only when it is whole: an inverter needs all three answers, so a missing list of warnings
never looks like "no warnings".

## Wiring

Read `NODE_HARDWARE.md` first. In short: **never connect an RS232 or RS485 line straight to a pin**; use a MAX3232 (3.3 V) for RS232 and a MAX485 for RS485, isolated
if the equipment shares its ground with a battery bank, with a common ground on the node's side. The pins take 3.3 V only.

## First start

1. Build the image (`tools/build_node.sh generic`, in Linux or WSL with Docker: `dist/generic.bin`) and flash it through the **USB & OTG** socket:
   `python -m esptool --chip esp32s3 -p COMx write_flash 0x0 dist/generic.bin`.
2. A node with no user opens the Wi-Fi **ARMOR-SETUP-xxxxxx** (the password is the setup code) and shows the code on its USB console every 15 seconds. Join it,
   open `http://192.168.4.1/`, enter the code, create the administrator and, if you want, the Wi-Fi network the node is to join. The node restarts.
3. After the setup the node keeps its own Wi-Fi **ARMOR-SOLAR-xxxxxx** (its password is the setup code; change it in the Wi-Fi page) so it can always be reached, and
   joins the network you gave it. Holding BOOT for 8 seconds within 30 seconds of power-up erases the settings and the users.
4. In the panel: **Serial ports** (enable and set each port, save, restart), **Broker** (the address and the identity), **Readings** (the last reading of every port).
5. On the broker: `sudo scripts/mqtt_identity.sh add solar-node <node id>` (ARMOR-DEVOPS) makes the node's identity, which may write `armor/solar/<node id>/#` and
   nothing else. In Studio, *Inverters* and *Batteries* declare the equipment with the same node id and device name; the first reading fills it in.

## Bench checklist (everything below is untried)

- The node starts, opens the setup Wi-Fi, the panel answers over HTTP and HTTPS, the setup completes, the update from the panel works and rolls back a bad image.
- A hardware port with a Voltronic inverter: `QPIGS`, `QMOD` and `QPIWS` are answered; compare the panel's numbers with the inverter's display; look at *what it hears*.
- A hardware port with a Pylontech console: `pwr`, `bat n`, `info n`; the formats are written from memory of the public console and may differ between firmwares.
- An emulated port at 2400 baud: the same inverter through it; check the framing errors and the overruns in the ports page while Wi-Fi is busy.
- An RS485 transceiver with the DE pin: the request goes out and the answer comes back, with no echo mistaken for an answer.
- An ANT-BMS on a hardware port and on an emulated one (19200 baud is the limit of the emulated ones): the *what the equipment reported* line of the ports page shows the protocol it found (`old` or `new`) and
  the MOSFET codes; compare the cells, the current and the state of charge with the BMS's own app. Its 3.3 V supply must be on the connector.
- All ten ports enabled at once with equipment on several of them, Wi-Fi busy: the counters of overruns and framing errors of the emulated ports stay at zero, the requests of two emulated ports never overlap
  badly (one sends while the other waits).
- Read any battery whose protocol is not decoded through a raw port and keep the capture: it is what its decoder will be written from.
- Leave it running for a day: no reset, no growth of the memory shown in the overview, readings in Studio.

## Files

`main/` is the ESP-IDF component (`app_main.cpp`, `solar_manager.cpp` one task per port, `uart_ports.cpp` the hardware and emulated ports, `network.cpp`,
`web_server.cpp`, `api_shared.cpp`, `mqtt_link.cpp`, `node_store.cpp`, `tls_cert.cpp`); `core/` is the part with no hardware in it (`solar_config.hpp` the settings,
`poller.hpp` the exchange with each kind of equipment, `ant_bms.hpp` the ANT-BMS's two protocols, `soft_uart.hpp`, `netplan.hpp`, `auth.hpp` and the protocol library); `panel/` the web panel (seven languages).
`tools/panel_mock.mjs` is a stand-in node for working on the panel without a board.

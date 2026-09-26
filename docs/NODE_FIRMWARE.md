# The firmware of the solar node

The firmware of **two boards**, one code base with a board profile chosen when the image is built (see *The two boards*): an **ESP32-S3-WROOM-1 N16R8** (16 MB of flash, 8 MB of octal PSRAM, two USB-C sockets, no Ethernet: Wi-Fi is its way in) and the **Waveshare ESP32-S3-ETH** (its way in is the Ethernet cable). It reads solar
inverters and batteries through up to **ten serial ports** and publishes their readings to A.R.M.O.R.'s broker (`armor/solar/<node>/<device>/state`, the
messages of ARMOR-COMMON). It has the same web panel as the radar nodes' (setup, login, users, Wi-Fi, broker, over-the-air update with rollback, log, the panel
over HTTPS) and the pages of its own: the ports and the readings.

**Nothing here has run on a board, and no inverter or battery has been connected.** What has been done: the firmware builds for both boards in the ESP-IDF 5.4.2 container
(a 1.3 MB image in a 3 MB slot), its core (settings, the exchange with each kind of equipment, the emulated UART's arithmetic) is tested on a computer with
stand-ins for the equipment, the messages it makes are accepted by ARMOR-COMMON, and the panel was exercised in a browser against a stand-in node. The host tests are 2,548 checks.

## Configuration from a phone over Bluetooth

The node listens over Bluetooth Low Energy (NimBLE) with the same channel as the radar node's, so the ARMOR app on a phone finds it as `ARMOR-XXXXXX` and sets up its name, Wi-Fi, address, broker and Bluetooth mode, with the panel's users and set-up code ([BLE_PROVISIONING.md](BLE_PROVISIONING.md)). The setting `ble.mode` (Network page) is `setup` by default (the node listens only while it has no user), `always` or `off` (the Bluetooth stack is not even started). What a node lists in its settings differs between the three kinds of A.R.M.O.R. node, and the app only touches what they all have. The radio side builds for both boards and the framing and the access rules are tested on a computer; **it has never run on a board and no phone has talked to it.**

## The two boards

| Profile | Board | Way in | Build |
| --- | --- | --- | --- |
| `s3-wifi` (the default) | ESP32-S3-WROOM-1 N16R8, two USB-C sockets | Wi-Fi: a station and/or its own access point | `tools/build_node.sh generic s3-wifi` -> `dist/generic-s3-wifi.bin` |
| `s3-eth` | Waveshare ESP32-S3-ETH: ESP32-S3R8, 16 MB flash, W5500 Ethernet (RJ45, PoE through a separate module), microSD socket, camera connector | The cable, with DHCP or a fixed address; Wi-Fi may be kept as an access point of its own (192.168.4.x, not bridged to the cable) | `tools/build_node.sh generic s3-eth` -> `dist/generic-s3-eth.bin` |

The profile decides the **pin table**, the **default pins of the ports**, the **default way in** and whether the **W5500 driver** is built. **An image is for ONE board**: an `s3-wifi` image on the Waveshare board would
put ports on the W5500's pins, and an `s3-eth` image on the other board would look for a W5500 that is not there. The panel shows the board's name in the overview and the image's own name is in the file.

### The N16R8 board (`s3-wifi`)

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

### The Waveshare ESP32-S3-ETH board (`s3-eth`)

Its USB-C socket is the chip's native USB (the log and the flashing). The pin map is ARMOR-RADAR's, for the same board:

| GPIO | Use |
| --- | --- |
| 26 to 32, 33 to 37, 19 and 20 | The flash, the octal PSRAM and the native USB: never offered |
| 9 to 14 | The W5500 (reset, interrupt, MOSI, MISO, clock, chip select): never offered |
| 8 | Wired to the camera connector: never offered |
| 4 to 7 | The microSD socket: usable when no card is used (the panel says so) |
| 0, 3, 45, 46 | Strapping pins: usable with care, as on the other board |
| the rest (1, 2, 15 to 18, 21, 38 to 44, 47, 48) | Free |

The defaults of the ports move to free pins (1 to 3: UART1 and UART2 on 16/15 and 18/17, UART0 on the SD pins 4/5; 4 to 9: emulated on 1/2, 21/47, 38/39, 40/41, 42/48 and 43/44). **The tenth port has no free pair left and sits on the strapping pins 3 and 46**: leave
it off unless you need it, and only wire equipment whose idle level does not disturb the boot. The emulated ports' interrupt service is not IRAM-safe on this board (the W5500's handler shares it), so an edge that arrives while the
flash is being written (a setting saved, an update) may be delayed: a reading in that instant can be lost, the next one is fine.

**Ethernet:** the address comes from DHCP unless you fix one in *Network* (address, mask, gateway and DNS are checked together). The node's own Wi-Fi network (kept by the set-up, `ARMOR-SOLAR-xxxxxx`, the setup code as its password) is a separate
network for reaching the node from a phone; it is not joined to the cable. A node that is being set up is reachable through the cable and through the set-up network at once.

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

## Asking a battery's console a question (*Ask the battery*)

A Pylontech port's card in the panel has a small tool for an administrator: it sends **one question** to the battery's console, from a short list of commands that only read (`pwrsys`, `pwr`, `help`, and `bat`, `info`, `stat`, `soh`, `data` with a module from 1 to 16), and shows the answer exactly as it came. The text that goes on the wire is rebuilt from that list and never copied from what was typed, so no command that changes anything can be sent; the port's readings pause while it waits (at most eight seconds), and in the mux profile the question is asked between two turns. It is there to capture what a command whose output is not described anywhere (`pwrsys`) really answers on the battery in front of you, so that it can be turned into a reading. Not run against a real battery.

## The base board with multiplexers (the *mux* profile)

The three hardware UARTs are all a node has for the Pylontech console (115200 baud is beyond the emulated ports), so the base board with **eight RJ45 sockets** puts a **74HC4052** multiplexer in front of each UART and a **MAX3232** on every pair of ports. The setting *Ports of the node* (Ports page, `profile` in the settings) chooses between the two ways of using the pins: **direct** (the default: ten ports, each with its own pins, as before) and **mux** (the eight ports of that board).

| Group | UART | TX | RX | S0 | S1 | Ports |
| --- | --- | --- | --- | --- | --- | --- |
| A | UART1 | GPIO 15 | GPIO 16 | GPIO 17 | GPIO 18 | 1 to 4 |
| B | UART2 | GPIO 1 | GPIO 2 | GPIO 38 | none (ground) | 5 and 6 |
| C | UART0 | GPIO 40 | GPIO 41 | GPIO 42 | none (ground) | 7 and 8 |

These are the defaults and all of them can be changed in the panel (the pins are checked as always: none may be reserved or shared). The LEDs' shift register (a **74HC595**, one LED per port) takes GPIO 21 (data), 39 (clock) and 47 (latch); with the three pins on *none* the node has no LEDs. The pins are free on both boards.

**Who has the line.** The ports of a group take turns, one at a time (`core/mux_group.hpp`): a port whose cycle is due gets the line, the multiplexer is pointed at it, the UART is given **that port's speed and polarity** (a group may mix a Pylontech console at 115200 and an inverter at 2400) and emptied of whatever the port before left, and the port's exchange runs until its cycle is over; then the next port in order. A port that is not due costs no time; a silent one costs its own timeouts and nobody else's; a *raw* port only listens for a second and a half every six seconds. The three groups run at the same time. A cycle that lasts more than 90 seconds is cut. The reading of an ANT-BMS's settings, asked in the panel, is done between two turns.

**The LEDs:** a short pulse for every good reading of a port, fast blinking for a moment when its cycle ended with no reading (no answer, or a wrong one), and dark when the port is switched off.

**Signals upside down.** A device with TTL levels (an ANT-BMS) wired through a MAX3232 arrives with its levels flipped, and what the node sends it goes out at RS232 levels (about ±5 V). The setting *Signals arrive inverted* of a port makes the UART flip both directions; it does not make the levels right for a TTL input, which needs a path without the MAX3232 (see the wiring notes). RS232 equipment (the Pylontech console, an Axpert) needs no inversion.

**Tall stacks.** A Pylontech stack of eight modules takes a few seconds to read cell by cell, and other ports wait meanwhile. *Modules' cells per cycle* (`cells_per_cycle`) reads the cells of only that many modules in each cycle, in rotation; the others keep the cells of their last turn. The first cycle reads them all.

The profile has been tested on a computer with a stand-in for the multiplexer and the equipment (which lose the bytes when the channel moves, ignore a wrong speed or polarity and inject a stray byte at every change); **it has never run on a board**, nor has any multiplexer switched a real line.

## Wiring

Read `NODE_HARDWARE.md` first. In short: **never connect an RS232 or RS485 line straight to a pin**; use a MAX3232 (3.3 V) for RS232 and a MAX485 for RS485, isolated
if the equipment shares its ground with a battery bank, with a common ground on the node's side. The pins take 3.3 V only.

## First start

1. Build the image for your board (`tools/build_node.sh generic s3-wifi` or `tools/build_node.sh generic s3-eth`, in Linux or WSL with Docker: `dist/generic-<board>.bin`) and flash it through the **USB & OTG** socket
   (the USB-C socket of the Waveshare board): `python -m esptool --chip esp32s3 -p COMx write_flash 0x0 dist/generic-<board>.bin`.
2. A node with no user opens the Wi-Fi **ARMOR-SETUP-xxxxxx** (the password is the setup code) and shows the code on its USB console every 15 seconds. Join it,
   open `http://192.168.4.1/`, enter the code, create the administrator and, if you want, the Wi-Fi network the node is to join. The node restarts.
   On the Waveshare board the cable works too: plug it in and open the address DHCP gave the node (its MAC is on the console); there is no Wi-Fi to give in the set-up.
3. After the setup the node keeps its own Wi-Fi **ARMOR-SOLAR-xxxxxx** (its password is the setup code; change it in the Wi-Fi page) so it can always be reached, and
   joins the network you gave it. Holding BOOT for 8 seconds within 30 seconds of power-up erases the settings and the users.
4. In the panel: **Serial ports** (enable and set each port, save, restart), **Broker** (the address and the identity), **Readings** (the last reading of every port).
5. On the broker: `sudo scripts/mqtt_identity.sh add solar-node <node id>` (ARMOR-DEVOPS) makes the node's identity, which may write `armor/solar/<node id>/#` and
   nothing else. In Studio, *Inverters* and *Batteries* declare the equipment with the same node id and device name; the first reading fills it in.

## Bench checklist (everything below is untried)

- The node starts, opens the setup Wi-Fi, the panel answers over HTTP and HTTPS, the setup completes, the update from the panel works and rolls back a bad image (the `.bin` to upload is `build/generic-<board>/armor_solar.bin`, not the merged image).
- **Waveshare board:** the W5500 answers on SPI (the overview says so when it does not), the link comes up with the cable, DHCP gives an address, a fixed address is kept after a restart, unplugging the cable is noticed and plugging it back recovers without a restart, the emulated ports still count no overruns while the panel is busy, and PoE powers the board without USB (read the schematic before connecting both).
- A hardware port with a Voltronic inverter: `QPIGS`, `QMOD` and `QPIWS` are answered; compare the panel's numbers with the inverter's display; look at *what it hears*.
- A hardware port with a Pylontech console: `pwr`, `bat n`, `info n`; the formats are written from memory of the public console and may differ between firmwares.
- An emulated port at 2400 baud: the same inverter through it; check the framing errors and the overruns in the ports page while Wi-Fi is busy.
- An RS485 transceiver with the DE pin: the request goes out and the answer comes back, with no echo mistaken for an answer.
- An ANT-BMS on a hardware port and on an emulated one (19200 baud is the limit of the emulated ones): the *what the equipment reported* line of the ports page shows the protocol it found (`old` or `new`) and
  the MOSFET codes; compare the cells, the current and the state of charge with the BMS's own app. Its 3.3 V supply must be on the connector.
- All ten ports enabled at once with equipment on several of them, Wi-Fi busy: the counters of overruns and framing errors of the emulated ports stay at zero, the requests of two emulated ports never overlap
  badly (one sends while the other waits).
- On an inverter port, leave *Protocol* on *auto* the first time: the port page says which dialect answered (PI30, REVO or PI18) after the first reading; if nothing answers, try each by hand (a REVO with a CRC reply has to be chosen by hand) and keep a raw capture of the silence. Check the PV power against the inverter's own display: a REVO read as PI30 shows the wrong PV numbers.
- Keep the text of `pwr`, `bat 1`, `info` and `stat` of each model in a file (the maker's program or a terminal at 115200): only `info` and `stat` have been captured so far, and the layouts of the other two are those of the public descriptions.
- On a Pylontech port, compare `pwr`, `bat 0` and `info 0` in a terminal with what the panel shows (columns by name: a wrong header would shift every value); the cycles and the model appear after the first `info` and `stat`.
- On an ANT-BMS port of the newer protocol, *Read the BMS settings*: the model and version appear, then the table of values; compare a few (the number of cells, the capacity, the cell over- and under-voltage limits) with the BMS's own app. It is read only: check that the BMS's own settings did not change. The old protocol says so instead.
- Read any battery whose protocol is not decoded through a raw port and keep the capture: it is what its decoder will be written from.
- Leave it running for a day: no reset, no growth of the memory shown in the overview, readings in Studio.

## Files

`main/` is the ESP-IDF component (`app_main.cpp`, `solar_manager.cpp` one task per port, `uart_ports.cpp` the hardware and emulated ports, `network.cpp`,
`web_server.cpp`, `api_shared.cpp`, `mqtt_link.cpp`, `node_store.cpp`, `tls_cert.cpp`, and for the Waveshare board `board_ethernet.cpp`, the W5500 driver); `core/` is the part with no hardware in it (`solar_config.hpp` the settings,
`poller.hpp` the exchange with each kind of equipment, `ant_bms.hpp` the ANT-BMS's two protocols, `soft_uart.hpp`, `netplan.hpp`, `auth.hpp` and the protocol library); `panel/` the web panel (seven languages).
`tools/panel_mock.mjs` is a stand-in node for working on the panel without a board.

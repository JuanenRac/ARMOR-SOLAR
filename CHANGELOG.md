# Changelog

All notable changes to this project are documented here.

## [0.3.2] - A warning for a pair that mixes hardware

- **The ports page warns when the two ports of a pair ask for different hardware** (mux profile, ports 1-2, 3-4, 5-6 and 7-8): each MAX3232 of the base board serves one pair, which is fitted either as RS232 (Pylontech, inverter) or as TTL (ANT-BMS), not both. With both ports of a pair switched on, one on an ANT-BMS and the other on a Pylontech or an inverter, each card says so; a port with the raw kind does not count. It is a hint, not an error: the settings are still saved. In the seven languages.

## [0.3.1] - LEDs 9 and 10 on pins both boards have

- **The default pins of LEDs 9 and 10 are GPIO 45 and GPIO 3** (0.3.0 said GPIO 8, which is wired to the camera connector of the Waveshare ESP32-S3-ETH, is never offered there and so made the default settings invalid on that board). Both are strapping pins that only read at reset: an LED behind a 470 ohm to ground does not pull them the wrong way. Still changeable in the panel. Not run on a board.

## [0.3.0] - LEDs 9 and 10 on their own pins

- **LEDs 9 and 10 (the two inverter ports) hang straight on a GPIO each** (defaults GPIO 45 and GPIO 3, settings `leds.led9` and `leds.led10` in the panel; -1 leaves one out), so the base board needs no second 74HC595 for two LEDs. The register goes back to eight bits (`shift_out`) and drives LEDs 1 to 8; bits 8 and 9 of the pattern go to the two pins. This replaces the chained second register of 0.2.9. Both pins are checked against the other pins like any other. Not run on a board.

## [0.2.9] - Ten LEDs

- **LEDs 9 and 10 for the two inverter ports**, on a second 74HC595 chained behind the first one (its SER on the first one's QH', the same clock and latch, LED 9 on its QA and LED 10 on its QB): the node shifts sixteen bits every time (`shift_out16`), so a board with a single register is unaffected. The documentation says why an LED on the first register's QH' cannot be a ninth LED (it repeats LED 8).

## [0.2.8] - The second select line of group B moves to GPIO 46

- **The base board's group B takes GPIO 46 by default** (0.2.7 said GPIO 4, which is a pin of the microSD socket of the Waveshare ESP32-S3-ETH and does not reach its header, so a board with both footprints could not wire it). GPIO 46 is on the header of both boards and reads low at start, so with a 100 kohm to ground the multiplexer sits on its first channel until the firmware takes over. The documentation says so, and the header of the Ethernet board is no longer described as having the microSD pins.

## [0.2.7] - The base board with ten ports

- **The mux profile serves ten ports** in groups of 4, 4 and 2 (batteries 1 to 8 and inverters 9 and 10), the shape of version 1.0 of the base board: group B gets its second select line (GPIO 4 by default; GPIO 37, which that version of the board wires, belongs to the octal PSRAM of both boards and is refused), so its four ports are reachable. The ports of a group were already limited only by its channels; what changed is the defaults, the number of ports the settings allow in this profile and the words of the panel.
- **Docs:** `NODE_HARDWARE.md` describes the board - which MAX3232 serves which sockets and which TTL connectors, the rule that a converter and its two connectors are never used together, what the firmware does with each variant - and lists what was found reading its netlist (the pin, the unconnected 5 V, the open connectors, the floating select lines, the decoupling). Nothing has been tried on a board.

## [0.2.6] - A Pylontech battery that sleeps is woken, and the node can update itself from GitHub

- **The node can look for a newer firmware on GitHub and install it** (*Firmware and log -> Check GitHub for a new version*), next to - never instead of - the upload of a file: it asks the repository's latest release, offers it only when it is newer and the release carries the SHA-256 of the image, downloads **the image built for this board** (`armor_solar-<board>.bin`) straight into the other firmware slot with a progress bar, and installs it only when the hash matches; then it restarts into it. Administrators only; it needs the node to reach the Internet. The code is the one the radar node already had, now shared by every node (`main/github_update.*`, `core/semver.hpp`, `core/release_assets.hpp` in ARMOR-COMMON's firmware base).
- **The panel and its tests:** the *Firmware and log* page has the card in the seven languages, the stand-in node (`tools/panel_mock.mjs`) answers the three routes (`GET /api/v1/ota/check`, `POST /api/v1/ota/install`, `GET /api/v1/ota/progress`), and the host tests check the comparison of versions and, for a release with and without an image per board, that a board never picks an image that is not its own.
- **A Pylontech console that has been quiet is woken before the first question.** At the start, and after a silence, the poller sends a bare Enter before `pwr`: a console that has been idle may swallow its first command, which looked like a battery that does not answer. The host tests cover it (`test_pylontech_wake`) and every poller sample strips that leading Enter.
- **A node can subscribe to the broker** (`mqtt_link::subscribe`, shared with the other nodes) and the panel's stylesheet gained two small rules; nothing changes for the solar node itself.

## [0.2.5] - More from batteries and inverters, no silence without a clock, and a login over HTTP after one over HTTPS

- **More of what the equipment says now reaches the message.** A battery stack carries its own power reading, the status codes of the MOSFETs and whether a protection has switched one off (ANT-BMS), and the number of cells being balanced (Pylontech, when its console tells); an inverter carries its DC bus voltage. They are left out when the equipment does not say, never made up.
- **A node without a clock no longer falls silent.** It used to drop everything it read until a time server answered, so a node on a network without internet, or one that restarted after a power cut with the provider down, said nothing at all. It now publishes with the time since it started, and ARMOR-SERVER stamps such a message with the moment it receives it; as soon as the clock is set it sends real dates again.
- **The broker link no longer races with itself** when the node moves to another saved broker: the client is replaced under a lock and the old one is stopped outside it, so a message being published at that moment can no longer use a client that is being destroyed.
- **An ANT-BMS with many temperature sensors** (more than six, plus the MOSFET and the balancer) no longer makes the server refuse the whole battery message: the module lists at most eight temperatures, and the mean and the extremes still count all of them.
- **The panel's help** (*Firmware and log*) describes the firmware slots and their switch, the copy of the log, in the seven languages.
- **The node says what kind it is.** The public session answer (`/api/v1/session`) now carries `kind` (`radar`, `solar`, `electrical` or `hmi`) next to the node's id, version and board, so Studio's search for nodes in the network can tell a radar node from a solar one before it is added, even when its panel title cannot be read.
- **Copy the log** (*Firmware and log -> Log*): a button copies everything the log shows to the clipboard, in the seven languages. It also works when the panel is opened over plain HTTP, where the browser's own clipboard is not available, through a fallback.
- **A login over HTTP works after one over HTTPS.** The session cookie of the page served over HTTPS now has its own name (`armor_session_tls`); with one shared name, the browser kept the "Secure" cookie of the HTTPS login and refused to let a plain-HTTP page replace it, so the login over HTTP looked as if it did nothing (it worked in a private window).

## [0.2.4] - Switch between the two firmware slots from the panel

- **Firmware slots in the panel** (*Firmware and log -> Update*): a new card shows the two application slots (ota_0 and ota_1) with the version each one holds and which one runs, and a button boots the other one at the next restart - the way back to the version that ran before an update, or forward to the one just installed. It asks for confirmation, needs an administrator, refuses an empty slot or a firmware of another project, and the settings are kept. The same card exists in the radar, solar, electrical and touch-panel nodes, in the seven languages (`POST /api/v1/ota/switch`).

## [0.2.3] - Date and time, flash overview, hints everywhere and a node that can always be reached

- **Date and time:** a new *Date and time* card on the Overview page shows the node's local time and where it comes from (time server, set by hand, or not set yet). The Network page has a new clock card: the time zone (a list of common zones, with summer time handled by itself), the time server on or off, and, with the server off, a button that sets the node's clock from the browser. The clock now starts by itself as soon as the node has an address - before, it only started together with the broker connection, so a node with no broker never had a time. The time server moved out of the broker settings into a `time` section of the settings file; older files that still carry `mqtt.ntp` load as before.
- **Flash memory:** a new card on the Overview page with the flash size, how much the partitions reserve, what is left unassigned and, for each of the two firmware slots, how much of it the image uses, how much is free, which version it holds and which one is running or boots next.
- **A hint on everything:** pausing the pointer over a field, a button or a menu entry now shows a short explanation in the panel's language (all seven).
- **"Load a configuration file"** now lights up under the pointer like the other buttons.
- **Real bug, found on a bench:** a backup Wi-Fi network or broker lost its password the next time the settings were saved, because the form never holds a stored password and an entry that arrived without one replaced the stored one with an empty password. An entry sent without a password now keeps the one it had (same network name; same user on the same slot or address); a different name starts empty. A secured network tried with an empty password answers "no network with compatible security"; that reason is now shown as a wrong password instead of an anonymous failure.
- **Real bug, found on a bench:** the choice of language made in the panel was never written to the node's settings, so the exported file said English while the panel was in Spanish. Choosing a language now stores it at once, with no restart.
- **Real bug, found on a bench:** the node's rescue network disappeared right after signing in to the panel through it. While the node kept looking for the Wi-Fi network it could not find, every attempt scanned all channels, and the access point shares the radio, so the phone lost the network. While someone is connected to the rescue network the node now pauses those attempts and resumes them ten seconds after the last person leaves.
- **Real bug, found on a bench:** after the first set-up by Wi-Fi the node's own network was never seen again. The set-up code was erased the moment the administrator was created, and that code is the password of the node's own network, which could then not start. The code is now always kept (the one derived from the fleet secret, the one built into the image, or a random one stored in flash so it is the same at every start), and the set-up screen accepts a Wi-Fi network for the node to join on every board, also on those with a cable.

## [0.2.2] - About and Help pages; removing a backup actually removes it

- **About:** the node's own identity, firmware version, author and licence.
- **Help:** a tabbed page explaining each part of the menu in plain language, in all seven languages.
- **Real bug (found on ARMOR-RADAR, fixed here too):** saving only ever added the document's backup Wi-Fi networks or brokers to the ones already stored, never replacing the list - so removing one and saving brought it straight back. Saving now replaces the list exactly as sent. Code only in this release - there is no board of this kind to flash yet.

## [0.2.1] - Staying signed in

- **"Keep me signed in on this browser"** at login: unchecked, nothing changes (30 minutes idle still signs out); checked, the session survives closing the browser and lasts 30 days of actual use. Code only in this release - there is no board of this kind to flash yet.

## [0.2.0] - Chip, flash, PSRAM and bootloader version on the Overview page

- **A new "Hardware" card:** the chip and its revision, how many cores, the flash and PSRAM sizes, and the IDF version of both the running firmware and the bootloader (not the same thing - a mismatch there usually means the wrong board file was built). Read straight from the chip and the bootloader's own descriptor, not stored anywhere. Needed `spi_flash` added to the component's own dependencies; built clean with the real toolchain.

## [0.1.9] - An eye on the login and set-up passwords

- **Sign-in, the admin password and the Wi-Fi password at set-up** now have an eye button that shows what was typed - the one place a mistyped password locks someone out with no other field to check it against.

## [0.1.8] - Download and load a whole configuration

- **An admin can now download this node's whole configuration** (Network page, secrets included) as a .json file - the same document the flash keeps - and load one back into the form before saving. Built for setting up a batch of identical boards from a single bench node instead of retyping Wi-Fi, broker and the rest by hand. The node's own identity (its node ID) is never overwritten by an import: every board keeps its own. `GET /api/v1/config/export` is a new, admin-only route; loading a file changes nothing until the existing Save button is pressed.

## [0.1.7] - A periodic restart the panel can turn on

- **A new System setting:** "do not apply" or every 1, 2, 3, 4, 6, 12, 24 or 48 hours. It is the same clean restart as every other one (Wi-Fi told it is leaving before the radio powers down), just timed instead of triggered by a save or a firmware update - for a board nobody is going to touch for weeks. Settings-schema change only (`system.auto_restart_hours`, 0 by default): a node left at "do not apply" behaves exactly as before.

## [0.1.6] - Backup Wi-Fi networks and backup brokers

- **Up to 3 backup Wi-Fi networks and 2 backup brokers**, in the panel's Wi-Fi and Broker pages: tried in order, the same way a phone or laptop remembers more than one network, only after the one above has failed to connect for a while - never while it still works. Settings-schema change only (`sta.backup[]`, `mqtt.backup[]`, both empty by default): a node with none configured behaves exactly as before.

## [0.1.5] - A proper goodbye to the access point before restarting

- **The node never told the access point it was leaving before a restart:** `esp_restart()` just cuts the radio, with no deauthentication frame sent; some access points get stuck holding the old association and need restarting themselves before the node can rejoin. It now calls `esp_wifi_disconnect()` and gives it a moment before restarting, on every restart path (the panel, a firmware update, the factory reset held on BOOT).
- **The "Restarting..." screen never appeared after a firmware update:** `S.rebooting = true` was set without calling `render()` on that one path - the panel just sat on the old screen until the auto-reload kicked in on its own six seconds later. Now it shows immediately.
- **The default HTTP header limit (512 bytes) was too small for a real browser:** a session cookie plus a modern browser's own request headers can exceed it, which the panel refused outright - a blank page saying "Header fields are too long". Raised to 2048 bytes.

## [0.1.4] - The node finder can tell this is a solar node

- **The panel's own title now says what it is** ("A.R.M.O.R. solar" instead of a generic "A.R.M.O.R. node"). ARMOR-STUDIO's "find nodes on the network" reads this when it probes a candidate, so the Solar menu no longer lists a radar or an electrical node found on the network.
- Built with ESP-IDF 5.5.5.

## [0.1.3] - A login that actually leaves you signed in

- **The session cookie was garbage:** it was built in a function's own local variable, and the HTTP server only keeps a pointer to a header's text, not a copy of it; by the time the response was really sent, that memory had already been reused for something else. The login or the first-time set-up answered "ok", but no browser ever kept a real session - re-entering the panel always looked like a fresh sign-in. The cookie is now kept alive until the response goes out.
- Built with ESP-IDF 5.5.5.

## [0.1.2] - Saving the settings over Bluetooth no longer restarts the node

- **Bluetooth task stack:** the task that serves the Bluetooth channel (`ble-worker`) ran out of stack while saving a Wi-Fi network with a fixed address, and the node restarted in the middle of the set-up (the app said the connection was lost). Its stack is now 16 KB instead of 8 KB.
- **Bluetooth is on at every start while there is no address:** a node set up with a cable that is not plugged in, or whose Wi-Fi was not joined, did not advertise at all and could not be found by the app; it now advertises at every start and stops a couple of minutes after it has an address.
- Built with ESP-IDF 5.5.5.

## [0.1.1] - A node that has just been set up can always be reached

- **A rescue access point.** A node that is set up (it has a user) but has no address 90 seconds after it starts - no cable, no Wi-Fi network it can join, no access point of its own - opens the set-up Wi-Fi `ARMOR-SETUP-xxxxxx` again, protected with the set-up code. Before, it closed its set-up Wi-Fi when the first administrator was created and vanished.
- **Why it did not join the Wi-Fi.** The `hello` of the Bluetooth channel and the panel's status carry `sta_error` (`network_not_found`, `wrong_password` or `failed`).
- **Bluetooth stays on while there is no address.** A node set up to join a Wi-Fi network keeps advertising until it has an address, and stops two minutes after it has one, so the app can come back and read the outcome.
- **A fixed address on the Wi-Fi too.** DHCP or a fixed address is valid whichever connection the node uses (it was only honoured on the cable); the panel offers it for both.
- Same change as ARMOR-RADAR 0.3.2; built with ESP-IDF 5.5.5.

## [0.1.0] - A board that boots

- **Boot loop fixed (shared firmware base):** generating the TLS certificate overflowed the main task's stack on the first boot of a board; the buffer is on the heap and the stack is 16 KB. Both images (Wi-Fi and Ethernet) build.

## [0.0.9]

- A GitHub Actions CI baseline (`.github/workflows/ci.yml`): validates the manifest, the version, CHANGELOG.md's heading, the seven README translations' structure and its own local Markdown links, then runs this project's real build/test through `tools/armor_project_tool.py build-test .` (vendored from ARMOR-COMMON, alongside `tools/armor_ci_validate.py` and `tools/_armor_readme_parity.py`, which do the manifest/docs checking).

## [0.0.8] - The base board with multiplexers, the cells in rotation, a second PV input and parallel units

- **The mux profile** for the base board with eight RJ45 sockets: a 74HC4052 multiplexer in front of each of the three hardware UARTs (groups of 4, 2 and 2 ports), the pins of the groups and of the LEDs' 74HC595 in the settings and the panel, and a new setting *Ports of the node* (`profile`: `direct` as before, or `mux`). The ports of a group take turns on the line (`core/mux_group.hpp`): each gets the multiplexer, its own speed and polarity and an empty UART, and runs until its cycle is over; a silent port costs its own timeouts and nobody else's. One LED per port through the 74HC595: a pulse for a good reading, fast blinking for a failed one, dark when the port is off. **Signals arrive inverted** per port, for a TTL device wired through a MAX3232.
- **The cells of a tall stack in rotation** (`cells_per_cycle`): each cycle reads the cells of that many Pylontech modules, in turn, and the others keep those of their last turn.
- **The second PV input (`QPIGS2`) and the units of a parallel system (`QPGS0`...)** for an inverter in the standard dialect, both off by default and never at the cost of the reading: `pv2_*` in the message (and `pv_w` as the sum of both inputs), `units` and the totals of the whole system. The contract of ARMOR-COMMON, the server and Studio know them.
- **Not read: `pwrsys`.** Its output is not described in any public document that could be found, so no parser was written from a guess. Instead, the panel's **Ask the battery** sends one question to a Pylontech console from an allow-list of commands that only read (`pwrsys`, `pwr`, `help`, `bat`/`info`/`stat`/`soh`/`data` with a module) and shows the answer as it came, so that it can be captured from a real battery (`core/console_probe.hpp`, 96 checks; the text on the wire is rebuilt from the list, never copied from the input).
- **Tests:** 2,548 host checks in all: 1,630 of the mux profile (the settings, who has the line, the LEDs, the rotation), 57 of the two optional readings, 96 of the console question, and the messages with them are accepted by ARMOR-COMMON; the panel was exercised in a real browser against a stand-in node (`tools/panel_browser_test.mjs`, 16 checks).
- **Not done:** none of it has run on a board.
- The hardware notes say why the eight-port base board has no isolation (the Pylontech ports and the inverter's RS232 are isolated at the equipment) and what would still need it (a port that shares the pack's negative, like the ANT-BMS one).

## [0.0.7] - Configuration from a phone over Bluetooth

- **Bluetooth Low Energy (NimBLE), the same channel as ARMOR-RADAR's node:** a phone running the ARMOR app finds the node as `ARMOR-XXXXXX` and sets up its name, Wi-Fi station, address, broker and the rest, with the same set-up code and the same users as the panel (`docs/BLE_PROVISIONING.md`). The link asks for encryption before anything is written; every operation but `hello` needs the set-up code or a login, changing anything needs an administrator, and wrong passwords are throttled.
- **A new setting, `ble.mode`:** `setup` (the default: the node listens only while it has no user), `always` or `off` (the Bluetooth stack is not even started). It is in the panel's Network page, in seven languages.
- **Tests:** 68 more host checks (the framing, the requests, who may ask what, and the setting).
- **Not done:** the radio side has never run on a board and no phone has talked to it.

## [0.0.6] - More inverters and batteries: three inverter dialects and the real Pylontech console formats

- **Three dialects for the inverters, read only.** The port of an inverter now has a *Protocol* setting: **auto** (the default: the node asks in PI30, then in PI18, and keeps to the one that answers; it looks again after three silences), **PI30** (Axpert, PIP, MKS and clones: what the port always spoke), **REVO** (the same frames with another arrangement of `QPIGS`, no warning list, and replies that end with a one-byte checksum instead of the CRC: a reply that fits only the checksum makes an *auto* port REVO) and **PI18** (InfiniSolar V, LV5048, SunGoldPower and clones: `^P005GS`, `^P006MOD`, `^P005FWS`, replies `^D...` with the length in front and a CRC). A whole reply of the other dialect (even a refusal) makes a searching port change at once. The port says which one it found; a dialect chosen by hand is never given up.
- **PI18 readings:** the 28 fields of the general status (grid, output, battery, both PV inputs added up, temperatures, the charger states, the direction of the battery's power), the working mode (power on, standby, bypass, battery, fault, hybrid) and the fault code and 16 warnings by name. **Only reading requests exist**: the frames that set something (`^S...`, `PCP`, `POP`, `MUCHGC`...) are refused by the builder and built nowhere.
- The warning list of the older dialect now names the bits the reference projects list past the 32nd (PV loss, battery derating, the three *battery weak* bits, battery equalisation), and accepts 32 to 36 flags.
- **Pylontech consoles are read by the names in their header.** The newer firmwares (the US5000 V2.3 among them) put an *Id* column after Tlow, Thigh, Vlow and Vhigh, add MosTempr, M.T.St and SysAlarm.St and can leave out others: the columns are found by name, the Time column is read as its two words, and the older layout is assumed when there is no header. The MOSFET temperature joins the module's temperatures and every state column (cells, temperature, MOSFET, system alarm) can raise the alarm.
- `bat <n>` gives the remaining charge (the *Coulomb* column in mAH) and the cells being balanced; `info <n>` gives the model and the rated capacity from its *Specification* line (`48V/74AH`); the cycles come from `stat <n>`. The identity and the history hardly change, so they are asked once and kept for half an hour: a cycle asks only `pwr` and `bat <n>`.
- **Tests:** 684 host checks (121 + 480 of the node + 83 of the Ethernet profile). The new ones take the console text real batteries printed (`info` and `stat` of a US2000C of four firmware versions, an US2KBPL, an US3000C and a 24 V unit, captured with the maker's own program), the samples the samples the mpp-solar and esphome-pipsolar projects publish, build the frames with their CRC or checksum, and run the whole exchange against stand-in inverters that answer in one dialect, in another, with a refusal, garbled, or not at all; and a US5000 V2.3 console with its Id columns.
- **Not done:** any of this against a real inverter or battery; the parallel and second-PV requests of the PI30 family (`QPGS`, `QPIGS2`); the rating commands (`QPIRI`, `PIRI`); the stack's own summary of the newer Pylontech firmwares (`pwrsys`); anything that writes.
- **What real batteries print, from the maker's own program's logs** (`info` and `stat` of more than twenty units: US2000C with four firmware versions, US2KBPL, US3000C and a 24 V one; every console speed is 115200): the console echoes every line followed by an empty one, some fields are empty, the prompt is `pylon>` or `pylon_debug>`, and a battery calls what is sold as US2000B *US2KBPL*. All of it is now a test.
- **An answer is whole when the console's prompt comes back** (`$$` and `pylon>` or `pylon_debug>`), not when the text says "Command completed": a command the battery does not know (`Invalid command or fail to excute.`) is over at once instead of after the timeout, and nothing of one answer is left to be taken for the next.
- **Battery health.** `stat`'s *Pwr Coulomb* is the capacity the battery has learned, in milliampere-seconds (a new 50 Ah module says 180000000); against the rated capacity of its *Specification* it is the module's `health_percent`, and the stack's is their mean. In the logs, some modules were at 56 % and 69 %.
- `bat <n>` rows with fewer columns (index, voltage, temperature and two states) are read too.
- **Tests:** 697 host checks (129 + 485 of the node + 83 of the Ethernet profile).

## [0.0.5] - Reading the settings of an ANT-BMS (read only)

- **The node can read a BMS's settings, and only read them.** On the *Serial ports* page an ANT-BMS port has a button *Read the BMS settings* (an administrator's): the node asks the BMS for its **model and software version** and for its **56 protection, warning, balancing and capacity registers** (cell and pack over- and under-voltage with their recoveries and second levels, currents and delays, short-circuit, state-of-charge warnings, balancing voltages and currents, number of cells, capacities), one read request at a time, and shows them as a table with their units. The port's ordinary readings wait meanwhile; a register that does not answer is counted and left out; three silences in a row end it.
- **Nothing is ever written.** The only request built is function 0x02 (read); the write frames of the BMS and its password are not built anywhere. Changing a BMS's or an inverter's settings is a separate step for later, to be studied with the equipment and tried in isolation. Only a BMS of the newer protocol answers; the older one has no settings request here (the panel says so).
- It runs as an operation of the port's own task (`POST /api/v1/ports/ant-settings?port=N` starts it, `GET` says how it goes and gives the result), so the web server is never blocked. Seven languages.
- **Tests:** 600 host checks (100 + 417 of the node + 83 of the Ethernet profile). The 56 read requests are compared byte for byte with the ones a real app sent (captured by the esphome-ant-bms project), the documented answer's CRC is reproduced, the device information decodes from a real capture (a 48-byte frame whose length byte says 32: it is read by position, without its CRC), and the reader is run against stand-in BMSs that answer everything, only some registers, nothing, no device information, and answers for another register.
- **Not done:** any of this against a real BMS; the old protocol's settings; anything that writes.

## [0.0.4] - One firmware for two boards: Wi-Fi and Ethernet

- **The Waveshare ESP32-S3-ETH is supported** next to the ESP32-S3-WROOM-1 N16R8. The firmware is one code base with two **board profiles** chosen when the image is built: `tools/build_node.sh generic s3-wifi` (the default) writes `dist/generic-s3-wifi.bin`, `tools/build_node.sh generic s3-eth` writes `dist/generic-s3-eth.bin`. An image is for ONE board: flash it only to that one.
- **The `s3-eth` profile** brings the W5500 Ethernet driver (the same code as ARMOR-RADAR's, on the same board), the Ethernet way in with DHCP or a fixed address (checked as a whole: address, mask, gateway, DNS), the layouts *ethernet* and *ethernet + access point* (the access point is a network of its own for reaching the node from a phone, not bridged to the cable), and the pin table of that board: GPIO 9 to 14 (the W5500) and 8 (the camera connector) are never offered, GPIO 4 to 7 are the microSD socket. The ports' default pins move to free pins; the tenth port has no free pair left and sits on strapping pins (the panel warns).
- **The panel** shows the connection card (cable or Wi-Fi, DHCP or a fixed address) and the board's name only on the board that has a cable; the set-up needs no Wi-Fi there. The `s3-wifi` image is unchanged for the user and refuses an Ethernet setting (`not_available`).
- **Tests:** 349 host checks (100 of the protocol library, 166 of the node on the `s3-wifi` profile, 83 of the `s3-eth` profile: its pin table, that every default pin of the ten ports is usable and none is shared, the address checks, the settings document, the layouts).
- **Not done:** a run on either board. The Ethernet path (W5500 on SPI, the link, DHCP and a fixed address, the interrupt service shared with the emulated UARTs, whose edges may be delayed during a flash write on this board) is untried. `docs/NODE_FIRMWARE.md` has the bench checklist for both boards.

## [0.0.3] - The firmware of the solar node

- **The firmware of an ESP32-S3-WROOM-1 N16R8 (Wi-Fi only), with its web panel.** It reads equipment through **ten serial ports**: three hardware UARTs (any pins) and seven emulated ones (an interrupt on every edge of the RX pin; one hardware timer shared by all of them shifts out the TX bits, one port sending at a time; up to 19200 baud), each independent and each an *inverter* (Voltronic / MPP Solar), a *battery* (Pylontech console), an *ANT-BMS* battery or a *raw monitor* that only listens and shows what arrives, for a protocol that is not decoded. A node may read only inverters, only batteries or a mix; RS485 through a driver-enable pin.
- **A reading is made only when whole:** an inverter needs `QPIGS`, `QMOD` and `QPIWS` all answered (a missing warning list must not look like no warnings), a battery is `pwr` plus, for each module that is there, `bat` and `info` (the extras may go missing without losing the reading). A silent port, a refused request (NAK), a wrong checksum and an answer that is not the expected one are counted and named, and the port says *waiting*, *reporting*, *silent* or *garbled*.
- **ANT-BMS batteries, over the cable at 19200 baud (3.3 V UART).** Both protocols of its firmware are decoded (`core/ant_bms.hpp`): the old 140-byte frame with its sum and the new `7E A1` frame with its CRC-16; the port asks in the old one and, when nothing answers, in the new one, keeps to the one that answered and looks again after three misses. The reading has the cells, the temperature sensors, the state of charge, the current, the capacities (only when the BMS knows them), an estimate of the cycles, and an alarm while a MOSFET is off for a protection; the ports page shows the protocol found and the MOSFET and balancer codes. Tested with frames captured on four real models by the AntBms-Arduino and esphome-ant-bms projects; nothing has been read from a battery by this project. Reading only: the write commands are left out on purpose. The battery message gains `temperatures_c` per module (already in the shared contract).
- **The panel of the radar nodes, with two pages of its own:** set-up with a code shown on the USB console (and the Wi-Fi network the node is to join; it keeps its own network `ARMOR-SOLAR-xxxxxx` with the setup code as password so it can always be reached), users, Wi-Fi station and access point, broker, update over the air with rollback, log, HTTPS; **Serial ports** (ten of them: each port's equipment, speed, pins, live counters and *what it hears* as hexadecimal) and **Readings** (the last message of every port, with the cells and the capacities). Seven languages.
- **Tests:** 254 host checks (100 of the protocol library, 154 of the node: settings and their checks, the ten ports, the network plan, the emulated UART with jittered edges at four speeds, the exchange with stand-in equipment including every fault above, the ANT-BMS decoder against frames captured on four real models of both protocols) and what the ports make is accepted by ARMOR-COMMON. `tools/panel_mock.mjs` is a stand-in node for the panel; `tools/build_node.sh generic` builds the image.
- **Not done:** a run on a board, any real inverter or battery, the Wi-Fi, the TLS panel and the update on the chip, the timing of the emulated UARTs under Wi-Fi load and with several of them receiving at once; the ANT-BMS's Bluetooth link (the node uses its cable, the more stable one) and its write commands; the Android solar tab. `docs/NODE_FIRMWARE.md` has the bench checklist.

## [0.0.2] - Cells, capacities and the messages in the shared contract

- **Each cell of each module.** `bat <n>` is decoded into the voltage and temperature of every cell, and `info <n>` into the model, the remaining and the full capacity (mAh), the cycles and the state; a stack reports the sum of its capacities, its energy in kWh and its cycles.
- **The messages are now part of ARMOR-COMMON** (schemas and conformance vectors), and the samples this library prints are validated against them.
- Tests: 100 checks. Still not run against a real battery or inverter, and ANT-BMS is still missing (it arrives in 0.0.3).

## [0.0.1] - The protocols and the design

- **Voltronic / MPP Solar inverters** (Axpert, PIP, InfiniSolar and clones), RS232 at 2400 baud: the frames with their CRC-16/XMODEM (and the rule for a CRC byte that would be a delimiter), a framer that finds replies in a byte stream, and the readings `QPIGS` (grid, output, battery, PV, status bits), `QMOD` (mode), `QPIWS` (warnings and faults by name) and `QPIRI` (ratings). Only reading commands can be built.
- **Pylontech batteries** (US2000, US3000, US5000): the table of the console's `pwr` command, summarised as one stack, and the RS485 frame with its length check and checksum.
- **The messages of a gateway node**, contract version 0 (`armor/solar/<node>/<device>/state`), serialised and checked by `tests/check_samples.py`.
- **The design of the rest**, in `docs/`: the protocols and their limits, the boards and the safe wiring of RS232 and RS485, the messages, and the plan for Studio's solar menus.
- **Tests:** 83 checks, including the CRCs of the reading commands and of `(NAK` that the protocol document lists on the wire.
- **Honest limits:** nothing has been connected to a real inverter or battery; there is no node firmware, the server does not read these messages and Studio has no solar menu; ANT-BMS is not decoded because its protocol document is not in the project.

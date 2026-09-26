# The protocols: what is decoded, from where, and what is not

Everything here is written from the public description of each device's protocol and tested on a computer with typical replies written by hand.
**No inverter or battery has been connected.** A row moves from "decoded" to "verified" only with a capture from the real device (see the
capability matrix in ARMOR-DOCS).

| Device | Port and speed | Decoded | Not decoded |
| --- | --- | --- | --- |
| Voltronic / MPP Solar inverters (Axpert, PIP, InfiniSolar and clones) | RS232, 2400 baud 8N1 | `QPIGS` (status), `QMOD` (mode), `QPIWS` (warnings and faults), `QPIRI` (ratings and settings), the CRC and the frames | Every setting command (`PCP`, `POP`, `PBT`...) on purpose; `QPIGS2` and parallel-system commands; `QVFW`, `QID` |
| Pylontech US2000, US3000, US5000 (and the C versions) | Console RS232 115200 8N1 | The table of `pwr`: every module's voltage, current, temperatures, cell range, state of charge and states | The detail of one module (`pwr 1`, the cell voltages), `bat`, `info`, the event log |
| Pylontech, RS485 | RS485, 9600 or 115200 | The frame: start, version, address, command, the length and its check, the checksum | The meaning of `INFO`, which differs between protocol versions: not decoded until a capture exists |
| ANT-BMS (home-made batteries) | 3.3 V TTL UART, 19200 baud 8N1 | The status frame of both protocols (old and new): the cells, sensors, state of charge, current, capacities, MOSFET and balancer states, checked with frames captured on four real models | Every write command (on purpose), the settings registers, the device information, the Bluetooth link |

## Voltronic / MPP Solar

- A command is text, a CRC-16 and a carriage return; a reply is `(`, text, a CRC-16 and a carriage return. The CRC is CRC-16/XMODEM, and a CRC byte
  that would be `(`, CR or LF is increased by one. The CRCs of `QPIGS`, `QPIRI`, `QMOD` and `QPIWS` (`B7A9`, `F854`, `49C1`, `B4DA`) and the well-known one of
  `(NAK` (`7373`) are tests.
- Only reading commands can be built by the library: a setting changes how the inverter charges and feeds the house.
- **The USB port of these inverters is not a serial port** (it is an HID device): a microcontroller reads the **RS232** port, through a level converter.
- `QPIGS` fields: grid voltage and frequency, output voltage, frequency, apparent and active power and load percentage, bus voltage, battery voltage,
  charging current, capacity, heat-sink temperature, PV current, PV voltage, battery voltage from the charger, discharging current, eight status bits, and,
  on newer models, the PV charging power. An older reply without the power gets it from voltage times current.
- The names of the `QPIWS` flags follow the public document (32 to 36 flags: PV loss, battery derating and the *battery weak* and equalisation bits included); a bit that the document calls reserved has no name and is never reported.

### Dialects (chosen by the port's *Protocol* setting; read only in all of them)

- **PI30** is the one above. **auto** asks in PI30 and, after a silence, in PI18, and keeps the one that answered; a whole reply of the other dialect (even `^0` or `(NAK`) switches at once, and three silences make it look again.
- **REVO** (Revo VM III and others that esphome-pipsolar marks so): the same frames and the same `QPIGS` fields up to the twelfth, then the PV **power** (watts), the PV voltage, the charger's battery voltage and the energy made today; no discharge current, and the PV current is worked out as power over voltage. Its replies may end with **one byte** (the sum of every byte before it, plus one) and CR instead of the CRC; a reply that fits only that makes an *auto* port REVO (a REVO that answers with the CRC has to be chosen by hand). No `QPIWS` is asked: the reading has no warning list.
- **PI18** (InfiniSolar V, LV5048, SunGoldPower and clones), same 2400 baud: request `^P<command length + 3, three digits><command><CRC><CR>`, reply `^D<payload length + 3, three digits><payload><CRC><CR>`, `^0` refuses and `^1` accepts. The CRC is the same as PI30's. Read: `GS` (28 comma-separated fields: grid V x10, Hz x10, output V x10, Hz x10, VA, W, load %, battery V x10, the charger's battery V x10 (two), discharge and charge current, capacity %, temperatures, PV1 and PV2 power and voltage x10, then the codes: configuration changed, the two chargers' states, load connected, the direction of the battery's power, of the DC/AC conversion and of the line, and the parallel id), `MOD` (00 power on, 01 standby, 02 bypass, 03 battery, 04 fault, 05 hybrid) and `FWS` (a fault code, then 16 warning flags). The requests of the library are `GS`, `MOD`, `FWS`, `PIRI`, `ID`, `VFW`, `PI`, `FLAG` and `T`, and nothing that sets.
- Sources: the mpp-solar and esphome-pipsolar projects and the sample replies they publish (the tests use them); no unit of any of these families has been connected.

## Pylontech console

Typing `pwr` prints one row per module of the stack (the master answers for all of them); a module that is not there says `Absent`. The parser takes
the rows that begin with a module number, tolerates the prompt and the completion line, and refuses a row with a text where a number should be. **The columns are found
by the names of the header line**, because the firmwares differ: the older ones stop at `B.T.St`, others add `MosTempr` and `M.T.St`, and the newer ones (US5000 V2.3) put an `Id`
column after `Tlow`, `Thigh`, `Vlow` and `Vhigh` and end with `SysAlarm.St`; the `Time` column is two words. Without a header the older layout is assumed.
`bat <n>` prints a row per cell (the newer firmwares add the charge in mAH and a `Y` or `N` for the cell being balanced), `info <n>` the identity (`Device name`, `Specification` such as `48V/74AH` for the rated
capacity, `Cell Number`, versions) and `stat <n>` the history (`CYCLE Times`). The identity and the history are asked once and kept for half an hour.
**Health:** `stat`'s `Pwr Coulomb` is the capacity the battery has learned in milliampere-seconds (a new 50 Ah module says `180000000`, a 74 Ah one `266400000`); over the rated capacity of the `Specification` line it is the module's `health_percent` (the stack's is the mean). An answer ends with the prompt (`$$` and `pylon>` or `pylon_debug>`), for a refusal as well as for a good answer.
Captured on real units (US2000C of four firmware versions, an US2KBPL, an US3000C and a 24 V unit), the console echoes every line followed by an empty one, `info` and `stat` answer as described
(`Device name` says US2KBPL for what is sold as US2000B, and a few fields such as `Manufacturer` or `Barcode` can be empty), the console speed is 115200 (`Console Port rate` in `info`) and the prompt
can be `pylon>` or `pylon_debug>`. The `pwr` and `bat` texts were not in those captures: their layouts remain those of the public descriptions.
Models the firmwares are known under: US2000 / US2000B, US2000C, US2000B Plus, US2KBPL, US3000, US3000C, US5000, UP2500, UP5000, Force L1, Force L2 and the Pytes E-Box 48100R (its console is a relative of Pylontech's).
 The stack summary uses what physics says of a parallel stack: the same voltage (so the mean), currents that add, the extreme temperatures and cell
voltages, the mean state of charge. Any state column other than `Normal` raises `alarm`.

## ANT-BMS

The ANT battery management system (the black board of many home-made LiFePO4 packs, 7S to 32S, from 100 A to 320 A) has a 4-pin JST connector with **3.3 V TTL
UART at 19200 baud, 8N1**. **The board must be powered from the connector's VCC (3.3 V) too**: with only TX, RX and ground it neither starts nor answers.
Its firmware speaks one of **two protocols**, so the node knows both: it asks in the old one and, when nothing answers, in the new one, keeps to the one that
answered, and looks again after three misses. Both are polled with one request that returns one whole frame.

| | Old protocol | New protocol |
| --- | --- | --- |
| Seen on | ANT 16S / 24S 100 to 200 A (2020), 16ZMB-TB-7-16S-300A, 24AHA-TB-24S-200A | ANT-BLE16ZMUB, ANT-BLE24BHUB and later (newer boards, including the 32S) |
| Request | `5A 5A 00 00 01 01` (an older firmware also takes `5A 5A 00 00 00 00`) | `7E A1 01 00 00 BE 18 55 AA 55` (`18 55` is the CRC) |
| Answer | 140 bytes, starts `AA 55 AA FF` | 6 + length + 4 bytes, starts `7E A1 11 00 00 <length>` (152 bytes for 16 cells and 2 sensors), ends `AA 55` |
| Numbers | big-endian | little-endian |
| Check | the sum of bytes 4 to 137, big-endian in the last two bytes | CRC-16 (Modbus) of everything between the first byte and the last four, little-endian before `AA 55` |

**Old frame** (offsets from the first byte): 4 total voltage (u16, 0.1 V); 6 to 69 the cells (32 x u16, mV); 70 current (s32, 0.1 A, negative while discharging);
74 state of charge (u8, %); 75 physical capacity (u32, 1e-6 Ah); 79 remaining capacity (u32, 1e-6 Ah); 83 cycle capacity (u32, 0.001 Ah); 87 seconds since start
(u32); 91 six temperatures (s16, degrees; -40 means no sensor); 103 charge MOSFET state, 104 discharge MOSFET state, 105 balancer state (u8 each); 111 power (s32, W);
115 and 118 the highest and the lowest cell numbers, 116 and 119 their voltages, 121 the mean; 123 the number of cells (u8); 132 the balancing bitmask (u32).

**New frame:** 7 battery state (0 unknown, 1 idle, 2 charging, 3 discharging, 4 standby, 5 error); 8 the number of temperature sensors; 9 the number of cells;
10, 18 and 26 three 8-byte bitmasks (protections, warnings, balancing); 34 the cells (u16, mV); then, after the cells and the sensors (`offset` = 2 x cells + 2 x sensors):
the sensors (s16), the MOSFETs' and the balancer's temperatures at 34 + offset and 36 + offset, total voltage (u16, 0.01 V) at 38 + offset, current (s16, 0.1 A) at 40 + offset,
state of charge and state of health (u16, %) at 42 and 44, the MOSFET and balancer states at 46, 47 and 48, then the capacities and the power (u32) from 50 + offset.

The node makes **one module** of the reading: the message has the cells, the sensors (`temperatures_c`), the state of charge, the current (positive while charging, as
everywhere in A.R.M.O.R.), the remaining and the full capacity (only when the BMS knows its capacity: it says 0 Ah when it was never set), and `cycles`, which is
**an estimate**: the BMS's total cycle capacity divided by its capacity. The `alarm` flag is raised while a MOSFET is off **because of a protection**.

| Code | Charge MOSFET | Discharge MOSFET |
| --- | --- | --- |
| 0, 1 | off, on | off, on |
| 2 | overcharge protection | over-discharge protection |
| 3 | over-current protection | over-current protection |
| 4 | battery full (not an alarm) | second-level over-current (new protocol) |
| 5 | total overvoltage | total undervoltage |
| 6, 7 | battery over-temperature, MOSFET over-temperature | the same |
| 8, 9, 10 | abnormal current, a balance wire dropped, mainboard over-temperature | the same |
| 11 | reserved | charge MOSFET on (not an alarm) |
| 12 | open failed (new protocol) | short-circuit protection |
| 13 | discharge MOSFET abnormal | discharge MOSFET abnormal |
| 14 | waiting (not an alarm) | start exception (old) / open failed (new) |
| 15 | manually turned off (not an alarm) | manually turned off (not an alarm) |
| 16 to 20 | new protocol only: second-level overvoltage, low-temperature protection, voltage difference exceeded, self-detect error | second-level low voltage, low-temperature protection, voltage difference exceeded, self-detect error |

The ports page of the node shows these codes as *what the equipment reported* (for example `new charge=1 discharge=1 balancer=0 cells=16`); the balancer state is 0
off, 1 over the limit, 2 charge differential, 3 over-temperature, 4 automatic, 10 mainboard over-temperature.

### Reading the settings (newer protocol, read only)

The node can also **read** a BMS's device information and settings (the button *Read the BMS settings* of its port): a read request is `7E A1 02 <address lo> <address hi> <bytes> <crc> AA 55`, and a register answers
`7E A1 12 <address> <bytes> <value, little-endian> <crc> AA 55`. The device information (address `0x026C`, 32 bytes: 16 of model and 16 of software version, as text) answers with six bytes more than its length byte says,
so it is read by position and its CRC is not checked. The 56 registers are asked one at a time, two bytes each (four for the three capacities); their addresses, names, scales and units are the table of the esphome-ant-bms project
(`core/ant_registers.hpp`), whose captured requests are the tests' vectors. **Nothing is written**: the BMS's write requests and its password are not built anywhere in this project.

The protocol is written from the serial-protocol description of the VBMS project's wiki and from the AntBms-Arduino and esphome-ant-bms projects, whose captured
frames of four models (two protocols) are the test vectors of the decoder. **Only reading is implemented**: the BMS's write commands (switching the MOSFETs, the
parameters, the password) can disconnect a battery under load and are left out on purpose. The BMS also has a Bluetooth link (the one the vendor's phone app uses to
read and configure it); the node uses the cable, which is the more stable of the two.

## Honest limits

- The exact text of the console differs between firmware versions: the columns are found by the header's names, but a row that lacks a column the reading needs (voltage, current, temperatures, cell extremes, the four states, the charge) is refused rather than guessed. The `pwrsys` command of the newer firmwares (the stack's own totals) is not read.
- The scaling of the RS485 analogue values differs between versions of Pylontech's document; the frame is built and checked here, the values are not read.
- Nothing has been read from a real device by this project: the ANT-BMS frames in the tests were captured by other people on their own units; the first thing to do with a real one is to capture its replies and add them to the tests.
- Writing to a BMS or to an inverter (their settings, the MOSFET switches, the charge priorities) is not implemented and will not be until it has been studied with the real equipment and tried in isolation: a wrong value can disconnect a battery under load or change how an inverter feeds the house, and an ANT-BMS asks for a password to accept a write.
- The ANT-BMS cycle count is an estimate and the MOSFET codes past 15 (new protocol) are as the esphome-ant-bms project lists them; a BMS that does not know its capacity reports none.

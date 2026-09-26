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
- The names of the `QPIWS` flags follow the public document; a bit that the document calls reserved has no name and is never reported.

## Pylontech console

Typing `pwr` prints one row per module of the stack (the master answers for all of them); a module that is not there says `Absent`. The parser takes
the rows that begin with a module number, tolerates the header, the prompt and the completion line, and refuses a row with a text where a number should
be. The stack summary uses what physics says of a parallel stack: the same voltage (so the mean), currents that add, the extreme temperatures and cell
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

The protocol is written from the serial-protocol description of the VBMS project's wiki and from the AntBms-Arduino and esphome-ant-bms projects, whose captured
frames of four models (two protocols) are the test vectors of the decoder. **Only reading is implemented**: the BMS's write commands (switching the MOSFETs, the
parameters, the password) can disconnect a battery under load and are left out on purpose. The BMS also has a Bluetooth link (the one the vendor's phone app uses to
read and configure it); the node uses the cable, which is the more stable of the two.

## Honest limits

- The exact text of the console differs a little between firmware versions; a row with fewer columns is refused rather than guessed.
- The scaling of the RS485 analogue values differs between versions of Pylontech's document; the frame is built and checked here, the values are not read.
- Nothing has been read from a real device by this project: the ANT-BMS frames in the tests were captured by other people on their own units; the first thing to do with a real one is to capture its replies and add them to the tests.
- The ANT-BMS cycle count is an estimate and the MOSFET codes past 15 (new protocol) are as the esphome-ant-bms project lists them; a BMS that does not know its capacity reports none.

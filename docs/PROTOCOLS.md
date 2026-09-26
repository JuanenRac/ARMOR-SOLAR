# The protocols: what is decoded, from where, and what is not

Everything here is written from the public description of each device's protocol and tested on a computer with typical replies written by hand.
**No inverter or battery has been connected.** A row moves from "decoded" to "verified" only with a capture from the real device (see the
capability matrix in ARMOR-DOCS).

| Device | Port and speed | Decoded | Not decoded |
| --- | --- | --- | --- |
| Voltronic / MPP Solar inverters (Axpert, PIP, InfiniSolar and clones) | RS232, 2400 baud 8N1 | `QPIGS` (status), `QMOD` (mode), `QPIWS` (warnings and faults), `QPIRI` (ratings and settings), the CRC and the frames | Every setting command (`PCP`, `POP`, `PBT`...) on purpose; `QPIGS2` and parallel-system commands; `QVFW`, `QID` |
| Pylontech US2000, US3000, US5000 (and the C versions) | Console RS232 115200 8N1 | The table of `pwr`: every module's voltage, current, temperatures, cell range, state of charge and states | The detail of one module (`pwr 1`, the cell voltages), `bat`, `info`, the event log |
| Pylontech, RS485 | RS485, 9600 or 115200 | The frame: start, version, address, command, the length and its check, the checksum | The meaning of `INFO`, which differs between protocol versions: not decoded until a capture exists |
| ANT-BMS (home-made batteries) | UART or RS485 | **Nothing**: the protocol document is not in the project | Everything. Put its document in the `solar` folder and it can be added like the others |

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

## Honest limits

- The exact text of the console differs a little between firmware versions; a row with fewer columns is refused rather than guessed.
- The scaling of the RS485 analogue values differs between versions of Pylontech's document; the frame is built and checked here, the values are not read.
- Nothing has been read from a real device: the first thing to do with a real one is to capture its replies and add them to the tests.

# Changelog

All notable changes to this project are documented here.

## [0.0.2] - Cells, capacities and the messages in the shared contract

- **Each cell of each module.** `bat <n>` is decoded into the voltage and temperature of every cell, and `info <n>` into the model, the remaining and the full capacity (mAh), the cycles and the state; a stack reports the sum of its capacities, its energy in kWh and its cycles.
- **The messages are now part of ARMOR-COMMON** (schemas and conformance vectors), and the samples this library prints are validated against them.
- Tests: 100 checks. Still not run against a real battery or inverter, and ANT-BMS is still missing.

## [0.0.1] - The protocols and the design

- **Voltronic / MPP Solar inverters** (Axpert, PIP, InfiniSolar and clones), RS232 at 2400 baud: the frames with their CRC-16/XMODEM (and the rule for a CRC byte that would be a delimiter), a framer that finds replies in a byte stream, and the readings `QPIGS` (grid, output, battery, PV, status bits), `QMOD` (mode), `QPIWS` (warnings and faults by name) and `QPIRI` (ratings). Only reading commands can be built.
- **Pylontech batteries** (US2000, US3000, US5000): the table of the console's `pwr` command, summarised as one stack, and the RS485 frame with its length check and checksum.
- **The messages of a gateway node**, contract version 0 (`armor/solar/<node>/<device>/state`), serialised and checked by `tests/check_samples.py`.
- **The design of the rest**, in `docs/`: the protocols and their limits, the boards and the safe wiring of RS232 and RS485, the messages, and the plan for Studio's solar menus.
- **Tests:** 83 checks, including the CRCs of the reading commands and of `(NAK` that the protocol document lists on the wire.
- **Honest limits:** nothing has been connected to a real inverter or battery; there is no node firmware, the server does not read these messages and Studio has no solar menu; ANT-BMS is not decoded because its protocol document is not in the project.

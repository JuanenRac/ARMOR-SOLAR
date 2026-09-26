# The solar menus of Studio (a plan)

Nothing of this exists yet. It is the order in which the rest is meant to be built, so that each step can be tested before the next.

## What the menus show

- **Solar (the energy flow):** one picture of the house: the panels, the batteries, the inverter, the grid and the load, with the power on each arrow (PV watts in,
  battery watts in or out, grid watts, load watts), the state of charge in the middle and the inverter's mode. Below, today's curves: PV power, load, battery charge.
- **Inverters:** one card per inverter: mode, grid, output and battery numbers, PV voltage and current, temperature, the active warnings in words, and (later,
  read-only) its ratings and settings.
- **Batteries:** one card per stack: state of charge, voltage, current, temperatures, the range of cell voltages, and each module; a module that is missing is said so.
- **Alarms:** the existing alarm centre gets new causes: battery low, inverter fault, battery in alarm, a device that stops answering. They are confirmed like any other.
- **Android:** the same numbers as a tile on the Status screen and a Solar tab, with the same icon-first look.

## What has to exist first

1. **The gateway firmware** on a node (the same family as ARMOR-RADAR): reads the serial ports with this library and publishes the messages.
2. **The contract:** the two messages move into ARMOR-COMMON with a schema and conformance vectors.
3. **The server:** reads `armor/solar/#`, keeps the latest state of every device and a history of the numbers (a small time series with a retention), exposes
   `/api/v1/solar/*` to Studio and the phone, and raises the alarms.
4. **Studio and Android:** the menus above.
5. **Settings** (charging priority, currents) are a separate, later decision: they change how the house is fed and should be confirmed, logged and reversible.

## Decisions taken

- The sources of truth are the inverter's and the battery's own readings; the server does not compute the energy of the house beyond simple sums.
- No control at first: read only. A command to an inverter is a safety matter and gets its own design.

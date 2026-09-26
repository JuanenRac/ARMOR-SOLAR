# The gateway nodes: what board, what wiring

A gateway node is an ESP32 that talks to inverters and batteries over their serial ports and publishes what they say to A.R.M.O.R.'s broker. It is
the same idea as ARMOR-RADAR's field nodes, and the plan is the same firmware family: one image for every board, told apart by its MAC, with its own
web panel, users, updates over the air, and the panel over HTTPS. The firmware for **both boards** (the Wi-Fi-only ESP32-S3 N16R8 and the Waveshare ESP32-S3-ETH with Ethernet) is built (see `NODE_FIRMWARE.md`: its ten
serial ports, its first start and its bench checklist) and has never run on a board.

## One board or two kinds

| | Wi-Fi only (a plain ESP32-S3 or ESP32 board) | Ethernet with PoE (the Waveshare ESP32-S3-ETH the radar nodes use) |
| --- | --- | --- |
| Network | Wi-Fi to the house router or to a radar node's access point | A cable, powered from the PoE switch: no extra supply |
| Serial ports | The ESP32-S3 has three UARTs; two are enough (an inverter and a battery stack) | The same three UARTs |
| Good for | A node near the router, or where the Wi-Fi is good | A node in a garage or a plant room, far from the router, or where the metal cabinet ruins the Wi-Fi |
| Cost | Much cheaper (check the current prices: the difference is the Ethernet chip, the PoE module and the bigger board) | Dearer, and it needs a cable to the switch |

**Recommendation:** decide by where the equipment stands, not by price alone. The firmware supports both, so nothing is lost by buying the cheap board for
the nodes that see the router well and the PoE one for the rest. A stack of batteries and an inverter that stand together need only one node.

## What connects to what

- **Voltronic / MPP Solar inverter:** its **RS232** port (a DB9 or an RJ45, depending on the model) at 2400 baud. The USB port cannot be used: it is HID.
- **Pylontech US2000 / US3000 / US5000:** the **console** RJ45 (RS232 levels, 115200 baud) for the `pwr` table; the other RJ45 is RS485 for the
  battery link to the inverter. Do not put the node on the RS485 bus the inverter is using unless a capture shows what is safe to share.
- **ANT-BMS:** the 4-pin JST 1.25 mm connector: **TX, RX, ground and 3.3 V VCC** (it does not start on TX, RX and ground alone). It is 3.3 V TTL, so it goes straight to the
  pins of a port (RX of the node to TX of the BMS, TX to RX), no level converter; keep the wire short and the ground common. Its battery ground is the pack's negative:
  if the node is powered from anything else, use an isolated UART link. The two protocols of its firmware are both read (see `PROTOCOLS.md`).
- **Level conversion:** RS232 signals swing about ±10 V; the ESP32 pins are 3.3 V and are damaged by that. Use a MAX3232 module (3.3 V version) for RS232
  and an RS485 transceiver module (with automatic direction control) for RS485. **Never connect an RS232 line straight to a pin.**
- **Isolation:** these devices share ground with a battery bank and, on some inverters, with the mains. Use an **isolated** RS232 or RS485 converter
  between the equipment and the node, and keep the node's supply apart from the battery ground. A fault on the battery side then cannot reach the network side.
- **Cable length:** RS232 is meant for a few metres; for more, use RS485, which reaches hundreds of metres, and put the node near the equipment.

## What a node reads, and how often

Every 5 to 10 seconds it asks the inverter `QPIGS` and `QMOD`, and less often `QPIWS` (and `QPIRI` once, at start); every 10 to 30 seconds it types `pwr` at the
battery console. It publishes one message per device (see SOLAR_MESSAGES.md), and stays silent about a device that does not answer, so the server can say so.

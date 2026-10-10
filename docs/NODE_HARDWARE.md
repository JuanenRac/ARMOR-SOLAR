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
- **Isolation:** a serial port can share ground with a battery bank and, on some inverters, with the mains, so find out before connecting: if it does, use an **isolated**
  RS232 or RS485 converter between the equipment and the node, and keep the node's supply apart from the battery ground, so a fault on the battery side cannot reach
  the network side. **The base board has no isolation on purpose**, because of what its equipment is: the serial ports of the Pylontech batteries are already
  isolated from the battery's negative (and the batteries' positives and negatives are tied to each other, so a DC breaker opening one pole does not leave a port
  floating against them), and the RS232 of the inverter is highly isolated from both its positive and its negative. That is the owner's statement about the equipment,
  not something this project checked: a port that is not isolated (the ANT-BMS one is the pack's negative, see above) does not go on that board without an isolator.
- **Cable length:** RS232 is meant for a few metres; for more, use RS485, which reaches hundreds of metres, and put the node near the equipment.

## The base board, version 1.0: ten RJ45 sockets and eight TTL connectors

One ESP32-S3 (the Wi-Fi N16R8 module, or the Waveshare ESP32-S3-ETH) drives up to **eight batteries and two inverters** through three **74HC4052** multiplexers and five **MAX3232** level converters. The mux profile of the firmware (*Ports of the node -> Base board*) is written for exactly this wiring.

| Group | Multiplexer | MAX3232 | RJ45 sockets | TTL connectors | Ports | Meant for |
| --- | --- | --- | --- | --- | --- | --- |
| A | U6, on UART1 (GPIO 15 TX, 16 RX; select GPIO 17, 18) | U2, U3 | ST1, ST2 / ST3, ST4 | ANT1, ANT2 / ANT3, ANT4 | 1 to 4 | batteries |
| B | U7, on UART2 (GPIO 1 TX, 2 RX; select GPIO 38 and the second select line) | U4, U5 | ST5, ST6 / ST7, ST8 | ANT5, ANT6 / ANT7, ANT8 | 5 to 8 | batteries |
| C | U8, on UART0 (GPIO 40 TX, 41 RX; select GPIO 42, the second select line tied to ground) | U10 | ST9, ST10 | none | 9 and 10 | inverters (RS232) |

**One MAX3232 or two TTL connectors, never both.** Each MAX3232 serves two RJ45 sockets and shares its two lines with two TTL connectors (the 3.3 V ones of an ANT-BMS: pin 1 +3V3, 2 ground, 3 the node's transmit, 4 the node's receive). Mount the MAX3232 with its capacitors and the protection diodes of its two sockets for two **Pylontech** batteries (115200 baud, RS232 console), or leave all of that out and plug two **ANT-BMS** into the connectors (19200 baud, TTL, straight to the multiplexer, so no polarity setting is needed). That gives 0, 2, 4, 6 or 8 Pylontech batteries, the rest being ANT-BMS. The pull-up resistors on the transmit lines (the 100 kohm ones next to each connector pair) stay in every variant: without a converter they are what keeps an idle line high.

What the firmware does with it: the ports of a group take turns on the group's UART, each at its own speed (a Pylontech at 115200, an ANT-BMS at 19200, a Voltronic inverter at 2400 unless the panel says otherwise), so a stack of batteries and an inverter on different groups are read at the same time. A port reads a few seconds at a time; the whole round of four ports in a group takes as long as its slowest batteries.

**Points of this version of the board** (found reading its netlist; none has been tried on a board):

- **GPIO 37 cannot be the second select line of group B.** On both boards GPIO 33 to 37 belong to the octal PSRAM (35, 36 and 37 are on the header, but the chip uses them), so the firmware refuses them as ports' pins and the node would never switch above two ports in that group. The firmware's default is GPIO 46: it is on the header of both boards (GPIO 4 to 7 are the microSD socket of the Ethernet one and do not reach its header), and as a strapping pin that is read low at start it wants a 100 kohm to ground, which the select lines want anyway. The board has to wire that pin, or any other free one on both headers, and the panel then says which.
- **The module's 5 V pin is not connected**, so the board is powered through the module's own USB socket (its 3.3 V regulator feeds everything here, with the eight ANT connectors' supply on top). For an installation without a USB supply, bring the 5 V pin to a connector, with a diode so that a USB cable and the supply do not feed each other.
- **The eight TTL connectors are an open door**: their supply and their two lines go out to cables. A series resistor on each receive line (100 ohm to 1 kohm), a resettable fuse or a ferrite on their 3.3 V and a protection diode array are worth their cost; the RS232 sides already have a bidirectional 15 V diode on each of the two lines.
- **The select lines have no pull resistor**: until the firmware starts they float and the multiplexers point at any channel. Harmless (every transmit line idles high through its pull-up), but a 100 kohm to ground on each select line makes it tidy.
- **Only 100 nF next to each chip**: the five charge pumps and the LEDs make current spikes; a 10 uF electrolytic or ceramic where the module's 3.3 V enters the board is cheap.
- **LEDs 9 and 10 (the inverter ports) go straight on a GPIO each.** The 74HC595's QH' output (pin 9) is *not* a ninth output (it only repeats the eighth stage), and a whole second register for two LEDs is not worth it. Put LED 9 on GPIO 45 and LED 10 on GPIO 3 (the defaults, changeable in the panel), each through its own 470 ohm to the LED and then to ground. Both are on the header of both boards (GPIO 8 is not: it is the ETH board's camera connector). They are strapping pins read only at reset (GPIO 3 chooses the JTAG source, GPIO 45 the flash voltage on chips that do not fix it): an LED behind a resistor to ground does not pull them the wrong way. Leave QH' unconnected.
- The RJ45 wiring to a DB9 RS232 inverter needs its own cable; on the sockets, pin 3 receives (it is the equipment's transmit), pin 6 transmits and pin 8 is ground: check the equipment's pinout before making it.

## The LEDs of the base board (one per port: eight through a 74HC595, two on their own pins)

The firmware's *mux* profile lights one LED per port through a **74HC595** shift register on three GPIO (the defaults are settings in the panel):

| 74HC595 pin | Name | Goes to |
| --- | --- | --- |
| 16 | VCC | +3V3, with 100 nF to GND next to the chip |
| 8 | GND | GND |
| 14 | SER (data) | GPIO 21 |
| 11 | SRCLK (shift clock) | GPIO 39 |
| 12 | RCLK (latch) | GPIO 47 |
| 13 | OE, active low | GND (outputs always on) |
| 10 | SRCLR, active low | +3V3 (never clears) |
| 9 | QH' (serial out) | nothing (it only repeats LED 8) |
| 15, 1, 2, 3, 4, 5, 6, 7 | QA, QB, QC, QD, QE, QF, QG, QH | LED 1 to LED 8 (port 1 to port 8), each through its own resistor |

Each output drives **one resistor and one LED in series, to ground**: output, then a resistor of about 470 ohm (about 2.8 mA from 3.3 V for an ordinary LED; the register may give about 6 mA per output and 70 mA in all), then the LED's anode; the cathode goes to GND. A high output lights the LED (the firmware sends bit 0 to QA, so QA is port 1 and QH is port 8; QA is pin 15, apart from QB to QH on pins 1 to 7). A resistor is never shared between LEDs. The outputs may flicker for a moment when the board is powered, until the node sends its first byte. Not built, not run on a board.

## What a node reads, and how often

Every 5 to 10 seconds it asks the inverter `QPIGS` and `QMOD`, and less often `QPIWS` (and `QPIRI` once, at start); every 10 to 30 seconds it types `pwr` at the
battery console. It publishes one message per device (see SOLAR_MESSAGES.md), and stays silent about a device that does not answer, so the server can say so.

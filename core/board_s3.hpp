// ARMOR-SOLAR - which GPIO pins of the ESP32-S3-DevKitC-1 style board (ESP32-S3-WROOM-1 N16R8, two USB-C sockets) may be handed to a serial port.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
//
// From the pin map printed for the board (photographs of it are in the project's notes) and the chip's datasheet:
//   - GPIO 22..25 do not exist, and 26..32 are the external flash;
//   - GPIO 33..37 belong to the octal PSRAM of the N16R8 module (35, 36 and 37 are on the header: never use them on this module);
//   - GPIO 19 and 20 are the native USB pair ("USB & OTG" socket): the log and the flashing come out of it;
//   - GPIO 43 and 44 are UART0's TX and RX, wired to the CH343P of the other USB-C socket ("USB to serial"): usable, but a cable in that socket drives them;
//   - GPIO 48 drives the on-board WS2812 RGB LED: usable, and the LED flickers;
//   - GPIO 0 (BOOT), 3, 45 and 46 are strapping pins read at reset: usable, but whatever is wired there must not pull them the wrong way while the chip starts;
//   - GPIO 39..42 are the JTAG pins of the chip; they are free unless a JTAG probe is used (ports 8 to 10 use 38 to 42 and GPIO 1).
// Nothing here has been checked on a board.
#pragma once
#include <array>
#include <cstdint>

namespace armor::board {

enum class PinUse : std::uint8_t {
  kFree,      // no special role
  kCaution,   // a strapping pin, the boot button, the USB-serial pair or the LED: usable with care
  kReserved,  // taken by the board or by the chip: never offered
};

enum class Reserved : std::uint8_t { kNone, kNoSuchPin, kFlash, kPsram, kUsb };

struct PinInfo {
  int gpio;
  PinUse use;
  Reserved reason;   // only meaningful for kReserved
  const char* note;  // "" or a short code the panel translates: "strapping", "boot", "usb_serial", "led"
  bool on_header;
};

constexpr int kFirstGpio = 0;
constexpr int kLastGpio = 48;

constexpr PinInfo pin_info(int gpio) {
  if (gpio < kFirstGpio || gpio > kLastGpio || (gpio >= 22 && gpio <= 25)) return {gpio, PinUse::kReserved, Reserved::kNoSuchPin, "", false};
  if (gpio >= 26 && gpio <= 32) return {gpio, PinUse::kReserved, Reserved::kFlash, "", false};
  if (gpio >= 33 && gpio <= 37) return {gpio, PinUse::kReserved, Reserved::kPsram, "", gpio >= 35};
  if (gpio == 19 || gpio == 20) return {gpio, PinUse::kReserved, Reserved::kUsb, "", true};
  if (gpio == 0) return {gpio, PinUse::kCaution, Reserved::kNone, "boot", true};
  if (gpio == 3 || gpio == 45 || gpio == 46) return {gpio, PinUse::kCaution, Reserved::kNone, "strapping", true};
  if (gpio == 43 || gpio == 44) return {gpio, PinUse::kCaution, Reserved::kNone, "usb_serial", true};
  if (gpio == 48) return {gpio, PinUse::kCaution, Reserved::kNone, "led", true};
  return {gpio, PinUse::kFree, Reserved::kNone, "", true};
}

constexpr bool assignable(int gpio) { return pin_info(gpio).use != PinUse::kReserved; }

// The serial ports of a node: three hardware UARTs (any pins, through the chip's GPIO matrix) and seven emulated ones. The console is on the native USB, so
// UART0 is free for a device.
constexpr int kPortCount = 10;
constexpr int kHardwarePorts = 3;
constexpr int kSoftPorts = 7;

// Where the ports are wired by default: (rx, tx, de). All are free header pins, none is a strapping pin. -1: no direction pin.
struct DefaultPins { int rx, tx, de; };
constexpr std::array<DefaultPins, kPortCount> kDefaultPins{{
    {16, 15, 7},    // port 1: UART1
    {18, 17, 8},    // port 2: UART2
    {4, 5, 6},      // port 3: UART0 (not on the pins of the USB-serial socket)
    {9, 10, -1},    // port 4: emulated
    {11, 12, -1},   // port 5: emulated
    {13, 14, -1},   // port 6: emulated
    {21, 47, -1},   // port 7: emulated
    {38, 39, -1},   // port 8: emulated
    {40, 41, -1},   // port 9: emulated
    {42, 1, -1},    // port 10: emulated
}};

}  // namespace armor::board

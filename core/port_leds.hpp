// ARMOR-SOLAR - one LED per port, driven by a 74HC595: what each one shows, and the bits that make it.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
//
// A port that is read correctly gives its LED one short pulse; a port that does not answer, or answers wrongly, makes it blink fast for a moment (so a failing port can be told at
// a glance); a port switched off in the panel never lights it. The 74HC595 takes its eight bits over three wires (data, clock, latch): `shift_out` makes those edges through any
// object that can set the three pins, so the order of the bits and the edges are tested on a computer. LED 1 is output QA (the first of the register), LED 8 output QH.
// Nothing here has run on a board.
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace armor::solar {

class PortLeds {
 public:
  static constexpr std::size_t kCount = 8;
  static constexpr std::uint64_t kPulseMs = 100;         // one good reading
  static constexpr std::uint64_t kBlinkPhaseMs = 100;    // a failing port: on for this long, off for this long
  static constexpr std::uint64_t kBlinkTotalMs = 1500;   // ... for this long after the failure

  void set_enabled(std::size_t port, bool enabled) { if (port < kCount) leds_[port].enabled = enabled; }
  void good(std::size_t port, std::uint64_t now_ms) { if (port < kCount) { leds_[port].blink_until = 0; leds_[port].pulse_until = now_ms + kPulseMs; } }
  void bad(std::size_t port, std::uint64_t now_ms) { if (port < kCount) { leds_[port].pulse_until = 0; leds_[port].blink_from = now_ms; leds_[port].blink_until = now_ms + kBlinkTotalMs; } }

  // The eight bits to show at `now_ms`: bit n is the LED of port n + 1.
  std::uint8_t pattern(std::uint64_t now_ms) const {
    std::uint8_t bits = 0;
    for (std::size_t i = 0; i < kCount; ++i) {
      const Led& led = leds_[i];
      if (!led.enabled) continue;
      bool on = now_ms < led.pulse_until;
      if (!on && now_ms >= led.blink_from && now_ms < led.blink_until) on = ((now_ms - led.blink_from) / kBlinkPhaseMs) % 2 == 0;
      if (on) bits = static_cast<std::uint8_t>(bits | (1u << i));
    }
    return bits;
  }

 private:
  struct Led {
    bool enabled = false;
    std::uint64_t pulse_until = 0, blink_from = 0, blink_until = 0;
  };
  std::array<Led, kCount> leds_;
};

// Sends one byte to a 74HC595, most significant bit first (so bit 0 ends on the first output), and latches it. `Pins` has set_data(bool), set_clock(bool) and set_latch(bool).
template <class Pins>
void shift_out(Pins& pins, std::uint8_t byte) {
  pins.set_latch(false);
  pins.set_clock(false);
  for (int bit = 7; bit >= 0; --bit) {
    pins.set_data(((byte >> bit) & 1) != 0);
    pins.set_clock(true);
    pins.set_clock(false);
  }
  pins.set_latch(true);
  pins.set_latch(false);
}

}  // namespace armor::solar

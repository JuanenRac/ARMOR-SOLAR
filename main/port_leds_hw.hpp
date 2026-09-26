// ARMOR-SOLAR - the LEDs of the ports on the base board: one 74HC595 behind three GPIO, shown from core/port_leds.hpp.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
#pragma once
#include <cstddef>

#include "core/solar_config.hpp"

namespace armor::leds {

// Starts the task that keeps the register showing what the ports did (a port that is switched off never lights). Does nothing, and says so, when the pins are unset.
bool start(const config::LedPins& pins, const config::Settings& settings);

void good(std::size_t port);   // a port was read correctly
void bad(std::size_t port);    // a port did not answer, or answered wrongly

}  // namespace armor::leds

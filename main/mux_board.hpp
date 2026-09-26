// ARMOR-SOLAR - the base board with multiplexers: a hardware UART and the two select pins of the 74HC4052 in front of it, as one MuxLine (core/mux_group.hpp).
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
#pragma once
#include <cstddef>
#include <memory>
#include <string>

#include "core/mux_group.hpp"
#include "core/solar_config.hpp"

namespace armor::muxboard {

// Opens the UART of group `group` (0 to 2: UART1, UART2 and UART0) on the group's pins and sets the select pins as outputs, low. Returns nullptr with the reason in `error`
// ("busy", "driver", "pins", "baud").
std::unique_ptr<solar::MuxLine> open_group(std::size_t group, const config::MuxGroup& pins, int first_baud, std::string& error);

}  // namespace armor::muxboard

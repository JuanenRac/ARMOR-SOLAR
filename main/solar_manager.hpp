// ARMOR-SOLAR - the ports at work: one task per enabled port opens it, asks its equipment (core/poller.hpp) and hands every whole reading to the broker link.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

#include "core/solar_config.hpp"

namespace armor::manager {

using Publish = std::function<void(const std::string& topic, const std::string& payload)>;

// Starts a task for every enabled port. `publish` gets each reading (topic, JSON).
void start(const config::Settings& settings, Publish publish);

struct PortStatus {
  int number = 0;             // 1 to 10
  bool enabled = false;
  bool soft = false;
  std::string kind, name;
  int baud = 0, rx = -1, tx = -1, de = -1, poll_s = 0;
  std::string state;          // disabled, starting, error, waiting, reporting, silent, garbled, listening
  std::string error;          // why the port did not open, or the last error of an exchange
  std::string detail;   // what the equipment said besides the readings (an ANT-BMS: the protocol and its MOSFET codes)
  std::uint32_t bytes_rx = 0, bytes_tx = 0, replies_ok = 0, replies_bad = 0, timeouts = 0, readings = 0, overruns = 0, framing_errors = 0;
  std::string last_payload;   // the last whole reading (JSON), or ""
};
PortStatus status(std::size_t index);

// The last bytes a port received, as hexadecimal lines with the printable characters beside them.
std::string raw_dump(std::size_t index);

// The last reading of every port that has one, as a JSON array of the messages the node publishes.
std::string readings_json();

}  // namespace armor::manager

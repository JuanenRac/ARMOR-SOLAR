// ARMOR-SOLAR - the ANT-BMS battery management system, over its serial (UART) port at 19200 baud, 8N1.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
//
// Two protocols exist on the same connector, and which one a BMS speaks depends on its firmware, so the node knows both and finds out which answers:
//
//   "old"  (ANT 16S/24S 2020, 16ZMB-TB-7-16S-300A, 24AHA-TB-24S-200A ...):  request `5A 5A 00 00 01 01`; the answer is 140 bytes, `AA 55 AA FF` first, every number
//          big-endian, a check sum (the plain sum of bytes 4 to 137) in the last two bytes.
//   "new"  (ANT-BLE16ZMUB, 24BHUB, newer 32S ...):  request `7E A1 01 00 00 BE <crc> AA 55`; the answer is `7E A1 11 00 00 <length> ...` `<crc> AA 55`, every number
//          little-endian, a CRC-16 (Modbus) of everything between the first and the last two bytes.
//
// Sources: the BMS's serial protocol as written up by the VBMS project's wiki and the AntBms-Arduino and esphome-ant-bms projects, which also published frames
// captured from real BMSs; those frames are the test vectors of tests/test_node.cpp. Nothing here has run against a BMS on a board.
//
// Sign of the current: negative while the battery is discharging, as in the rest of A.R.M.O.R. (positive while charging).
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "pylontech.hpp"

namespace armor::solar::ant {

enum class Protocol { kOld, kNew };

constexpr std::size_t kOldFrameSize = 140;
constexpr std::size_t kNewMaxFrameSize = 200;   // 6 + a length byte (at most 190 in practice) + 4
constexpr int kMaxCells = 32;
constexpr std::size_t kMaxTemperatures = 8;   // what one module of the message may list

// CRC-16 (Modbus: polynomial 0xA001, start 0xFFFF).
inline std::uint16_t crc16(const std::uint8_t* data, std::size_t length) {
  std::uint16_t crc = 0xFFFF;
  for (std::size_t i = 0; i < length; ++i) {
    crc ^= data[i];
    for (int bit = 0; bit < 8; ++bit) crc = (crc & 1) ? static_cast<std::uint16_t>((crc >> 1) ^ 0xA001) : static_cast<std::uint16_t>(crc >> 1);
  }
  return crc;
}

// The request for the readings.
inline std::vector<std::uint8_t> request(Protocol protocol) {
  if (protocol == Protocol::kOld) return {0x5A, 0x5A, 0x00, 0x00, 0x01, 0x01};
  std::vector<std::uint8_t> frame = {0x7E, 0xA1, 0x01, 0x00, 0x00, 0xBE};
  const std::uint16_t crc = crc16(frame.data() + 1, 5);
  frame.push_back(static_cast<std::uint8_t>(crc & 0xFF));
  frame.push_back(static_cast<std::uint8_t>(crc >> 8));
  frame.push_back(0xAA);
  frame.push_back(0x55);
  return frame;
}

// What a BMS reported, beyond the module the message is made of.
struct Reading {
  Protocol protocol = Protocol::kOld;
  pylontech::Module module;       // number 1, present: the whole BMS is one module
  int charge_mos = 0;             // the status code of the charge MOSFET (0 off, 1 on, the rest a protection or a state: see docs/PROTOCOLS.md)
  int discharge_mos = 0;
  int balancer = 0;               // 0 off, 4 automatic ...
  double power_w = 0;             // negative while discharging
  double cycle_ah = -1;           // the total charge that went through the battery (the BMS's cycle capacity), -1 unknown
  int cells = 0;
  bool protecting = false;        // a MOSFET is off because of a protection (not because it is simply off, full or waiting)
};

// A MOSFET status that means the BMS is protecting the battery (or is faulty): charge 2 3 5 6 7 8 9 10 12 13 16 17 18 20, discharge 2 3 4 5 6 7 8 9 10 12 13 14 16 17 18 19.
inline bool charge_protects(int code) {
  return code == 2 || code == 3 || (code >= 5 && code <= 10) || code == 12 || code == 13 || code == 16 || code == 17 || code == 18 || code == 20;
}
inline bool discharge_protects(int code) {
  return (code >= 2 && code <= 10) || code == 12 || code == 13 || code == 14 || (code >= 16 && code <= 19);
}

namespace detail {
inline unsigned u16_be(const std::uint8_t* p) { return (static_cast<unsigned>(p[0]) << 8) | p[1]; }
inline unsigned u16_le(const std::uint8_t* p) { return (static_cast<unsigned>(p[1]) << 8) | p[0]; }
inline std::uint32_t u32_be(const std::uint8_t* p) { return (static_cast<std::uint32_t>(p[0]) << 24) | (static_cast<std::uint32_t>(p[1]) << 16) | (static_cast<std::uint32_t>(p[2]) << 8) | p[3]; }
inline std::uint32_t u32_le(const std::uint8_t* p) { return (static_cast<std::uint32_t>(p[3]) << 24) | (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[1]) << 8) | p[0]; }
inline int s16(unsigned v) { return static_cast<int>(static_cast<std::int16_t>(static_cast<std::uint16_t>(v))); }
inline std::int32_t s32(std::uint32_t v) { return static_cast<std::int32_t>(v); }

// A temperature is a sensor's when it is not the "no sensor" value (-40) and is a believable number.
inline bool temperature_ok(int value) { return value > -40 && value < 150; }

// The module's fields that both protocols share, from the cells, the temperatures, the current and the capacities already read.
inline void finish(Reading& r, const std::vector<int>& temperatures, double current_a, int soc, double remaining_ah, double nominal_ah) {
  pylontech::Module& m = r.module;
  m.number = 1;
  m.present = true;
  m.current_a = current_a;
  bool have_temperature = false;
  double sum = 0;
  std::size_t counted = 0;
  for (int t : temperatures) {
    if (!temperature_ok(t)) continue;
    if (m.temperatures_c.size() < kMaxTemperatures) m.temperatures_c.push_back(t);   // the message carries at most eight (the server refuses a longer list)
    sum += t;
    ++counted;
    if (!have_temperature) { m.temperature_low_c = m.temperature_high_c = t; have_temperature = true; }
    else { if (t < m.temperature_low_c) m.temperature_low_c = t; if (t > m.temperature_high_c) m.temperature_high_c = t; }
  }
  if (have_temperature) m.temperature_c = sum / static_cast<double>(counted);
  if (!m.cells_v.empty()) {
    m.cell_low_v = m.cell_high_v = m.cells_v[0];
    for (double v : m.cells_v) { if (v < m.cell_low_v) m.cell_low_v = v; if (v > m.cell_high_v) m.cell_high_v = v; }
  }
  m.soc_percent = soc >= 0 && soc <= 100 ? soc : -1;
  if (nominal_ah > 0) m.full_capacity_ah = nominal_ah;
  // a BMS that does not know its capacity says 0: then the remaining capacity is not a number either
  if (nominal_ah > 0 && remaining_ah >= 0 && remaining_ah <= nominal_ah * 1.5) m.capacity_ah = remaining_ah;
  if (nominal_ah > 0 && r.cycle_ah >= 0) m.cycles = static_cast<int>(r.cycle_ah / nominal_ah + 0.5);   // full-cycle equivalents, an estimate
  m.model = "ANT-BMS";
  r.protecting = charge_protects(r.charge_mos) || discharge_protects(r.discharge_mos);
  m.has_power = true; m.power_w = r.power_w;
  m.charge_mos = r.charge_mos; m.discharge_mos = r.discharge_mos;
  m.protecting = r.protecting;
  m.voltage_state = r.protecting ? "Protection" : "Normal";
  m.current_state = "Normal";
  m.temperature_state = "Normal";
  m.base_state = r.protecting ? "Protect" : current_a > 0.5 ? "Charge" : current_a < -0.5 ? "Dischg" : "Idle";
}
}  // namespace detail

// The old protocol's 140-byte answer. False when it is not one: a wrong length or header, a wrong check sum, a cell count that cannot be.
inline bool decode_old(const std::uint8_t* d, std::size_t length, Reading& out) {
  if (length != kOldFrameSize || d[0] != 0xAA || d[1] != 0x55 || d[2] != 0xAA || d[3] != 0xFF) return false;
  unsigned sum = 0;
  for (std::size_t i = 4; i < kOldFrameSize - 2; ++i) sum += d[i];
  if ((sum & 0xFFFF) != detail::u16_be(d + 138)) return false;
  const int cells = d[123];
  if (cells < 1 || cells > kMaxCells) return false;
  Reading r;
  r.protocol = Protocol::kOld;
  r.cells = cells;
  r.module.voltage_v = detail::u16_be(d + 4) * 0.1;
  for (int i = 0; i < cells; ++i) r.module.cells_v.push_back(detail::u16_be(d + 6 + 2 * i) * 0.001);
  const double current = detail::s32(detail::u32_be(d + 70)) * 0.1;
  const double nominal = detail::u32_be(d + 75) * 0.000001;
  const double remaining = detail::u32_be(d + 79) * 0.000001;
  r.cycle_ah = detail::u32_be(d + 83) * 0.001;
  std::vector<int> temperatures;
  for (int i = 0; i < 6; ++i) {
    const int t = detail::s16(detail::u16_be(d + 91 + 2 * i));
    if (i >= 4 && t == 0) continue;   // the two last slots (the MOSFET's and the balancer's) read 0 when there is no sensor
    temperatures.push_back(t);
  }
  r.charge_mos = d[103];
  r.discharge_mos = d[104];
  r.balancer = d[105];
  r.power_w = detail::s32(detail::u32_be(d + 111));
  detail::finish(r, temperatures, current, d[74], remaining, nominal);
  out = r;
  return true;
}

// The new protocol's answer to the status request (function 0x11).
inline bool decode_new(const std::uint8_t* d, std::size_t length, Reading& out) {
  if (length < 10 + 6 || d[0] != 0x7E || d[1] != 0xA1 || d[2] != 0x11) return false;
  if (length != static_cast<std::size_t>(6 + d[5] + 4) || d[length - 2] != 0xAA || d[length - 1] != 0x55) return false;
  if (crc16(d + 1, length - 5) != detail::u16_le(d + length - 4)) return false;
  const int temperature_sensors = d[8];
  const int cells = d[9];
  if (cells < 1 || cells > kMaxCells || temperature_sensors > 8) return false;
  const std::size_t offset = static_cast<std::size_t>(cells) * 2 + static_cast<std::size_t>(temperature_sensors) * 2;
  if (112 + offset > length) return false;   // the last field read is a 32-bit number at 108 + offset
  Reading r;
  r.protocol = Protocol::kNew;
  r.cells = cells;
  r.module.voltage_v = detail::u16_le(d + 38 + offset) * 0.01;
  for (int i = 0; i < cells; ++i) r.module.cells_v.push_back(detail::u16_le(d + 34 + 2 * i) * 0.001);
  std::vector<int> temperatures;
  for (int i = 0; i < temperature_sensors; ++i) temperatures.push_back(detail::s16(detail::u16_le(d + 34 + static_cast<std::size_t>(cells) * 2 + 2 * i)));
  temperatures.push_back(detail::s16(detail::u16_le(d + 34 + offset)));   // the MOSFETs'
  temperatures.push_back(detail::s16(detail::u16_le(d + 36 + offset)));   // the balancer's
  const double current = detail::s16(detail::u16_le(d + 40 + offset)) * 0.1;
  const int soc = static_cast<int>(detail::u16_le(d + 42 + offset));
  r.charge_mos = d[46 + offset];
  r.discharge_mos = d[47 + offset];
  r.balancer = d[48 + offset];
  const double nominal = detail::u32_le(d + 50 + offset) * 0.000001;
  const double remaining = detail::u32_le(d + 54 + offset) * 0.000001;
  r.cycle_ah = detail::u32_le(d + 58 + offset) * 0.001;
  r.power_w = detail::s32(detail::u32_le(d + 62 + offset));
  detail::finish(r, temperatures, current, soc, remaining, nominal);
  out = r;
  return true;
}

// Cuts the bytes that arrive into whole frames: it looks for `AA 55 AA FF` (old) or `7E A1` (new), takes the length of the frame from the protocol, and lets
// go of bytes that start nothing. It does not check the sum: decode_old / decode_new do.
class Framer {
 public:
  // True when `frame` holds a complete frame of the protocol `protocol`.
  bool feed(std::uint8_t byte, std::vector<std::uint8_t>& frame, Protocol& protocol) {
    buffer_.push_back(byte);
    for (;;) {
      if (buffer_.empty()) return false;
      int verdict = check(protocol);
      if (verdict == 0) return false;                       // a frame that is still coming
      if (verdict > 0) { frame.swap(buffer_); buffer_.clear(); return true; }
      buffer_.erase(buffer_.begin());                       // it starts nothing: let go of its first byte and look at the rest
    }
  }
  void clear() { buffer_.clear(); }
 private:
  // 1: complete, 0: still coming, -1: not a frame
  int check(Protocol& protocol) const {
    const std::size_t n = buffer_.size();
    if (buffer_[0] == 0xAA) {
      static const std::uint8_t kHead[4] = {0xAA, 0x55, 0xAA, 0xFF};
      for (std::size_t i = 0; i < n && i < 4; ++i) if (buffer_[i] != kHead[i]) return -1;
      if (n < kOldFrameSize) return 0;
      protocol = Protocol::kOld;
      return 1;
    }
    if (buffer_[0] == 0x7E) {
      if (n >= 2 && buffer_[1] != 0xA1) return -1;
      if (n < 6) return 0;
      const std::size_t total = 6 + static_cast<std::size_t>(buffer_[5]) + 4;
      if (total > kNewMaxFrameSize) return -1;
      if (n < total) return 0;
      protocol = Protocol::kNew;
      return 1;
    }
    return -1;
  }
  std::vector<std::uint8_t> buffer_;
};

}  // namespace armor::solar::ant

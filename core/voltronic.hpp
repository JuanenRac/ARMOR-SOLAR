// ARMOR-SOLAR - the serial protocol of Voltronic and MPP Solar inverters (Axpert, PIP, InfiniSolar and the many clones that use it).
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
//
// Source: the public "communication protocol" document of these inverters, written from what it says; NOTHING here has been connected to an inverter.
//
//   Serial 2400 baud 8N1 on the RS232 port (the USB port is a HID device, which a microcontroller cannot read: use the RS232 one).
//   Command:  ASCII text (QPIGS, QPIRI, QMOD, QPIWS ...), then a CRC-16 of two bytes, then CR.
//   Reply:    '(' + text + CRC-16 of two bytes + CR. "(NAK" + CRC + CR when the command is refused, "(ACK" for a setting that was accepted.
//   CRC-16:   XMODEM (polynomial 0x1021, start 0) over every byte before the CRC; if either byte of the result is 0x28, 0x0D or 0x0A it is
//             increased by one, so the CRC can never be taken for a delimiter.
//
// Dialects of this protocol (see the poller): the standard one (PI30: Axpert, PIP, MKS, InfiniSolar clones ...), the REVO one (the same frames, another arrangement of QPIGS, and
// replies that may end with a one-byte checksum instead of the CRC) and PI18 (core/voltronic_pi18.hpp: other frames altogether). Sources: the mpp-solar and esphome-pipsolar projects.
//
// Only reading commands are built here (Q...): a setting command (PCP, POP, PBT ...) changes how the inverter charges and feeds the house, and is
// not something to send from an untested library.
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace armor::solar::voltronic {

// CRC-16/XMODEM.
inline std::uint16_t crc16_xmodem(const std::uint8_t* data, std::size_t length) {
  std::uint16_t crc = 0;
  for (std::size_t i = 0; i < length; ++i) {
    crc = static_cast<std::uint16_t>(crc ^ (static_cast<std::uint16_t>(data[i]) << 8));
    for (int bit = 0; bit < 8; ++bit) crc = static_cast<std::uint16_t>((crc & 0x8000) ? ((crc << 1) ^ 0x1021) : (crc << 1));
  }
  return crc;
}

// The two CRC bytes as they go on the wire (high first), with the delimiter rule applied.
inline std::array<std::uint8_t, 2> wire_crc(const std::uint8_t* data, std::size_t length) {
  const std::uint16_t crc = crc16_xmodem(data, length);
  std::array<std::uint8_t, 2> bytes{static_cast<std::uint8_t>(crc >> 8), static_cast<std::uint8_t>(crc & 0xFF)};
  for (std::uint8_t& b : bytes) if (b == 0x28 || b == 0x0D || b == 0x0A) ++b;
  return bytes;
}

// A reading command as bytes: the text, the CRC and CR. Only the commands that read are accepted (the letter Q, then capitals and digits, at most 12 characters).
inline bool build_command(std::string_view text, std::vector<std::uint8_t>& out) {
  out.clear();
  if (text.size() < 2 || text.size() > 12 || text[0] != 'Q') return false;
  for (char c : text) if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))) return false;
  out.assign(text.begin(), text.end());
  const std::array<std::uint8_t, 2> crc = wire_crc(out.data(), out.size());
  out.push_back(crc[0]);
  out.push_back(crc[1]);
  out.push_back('\r');
  return true;
}

// A reply checked and cut open: the text between '(' and the CRC. False when the frame is not a whole reply, starts wrong, or its CRC is wrong.
inline bool parse_reply(const std::uint8_t* frame, std::size_t length, std::string& text) {
  text.clear();
  if (length < 4 || frame[0] != '(' || frame[length - 1] != '\r') return false;
  const std::size_t body = length - 3;   // '(' ... text, then two CRC bytes, then CR
  const std::array<std::uint8_t, 2> crc = wire_crc(frame, body);
  if (frame[body] != crc[0] || frame[body + 1] != crc[1]) return false;
  text.assign(reinterpret_cast<const char*>(frame) + 1, body - 1);
  return true;
}

// A reply of the REVO dialect: the CRC of the standard one, or a one-byte checksum (the sum of every byte before it, plus one) and CR. False when neither fits.
inline bool parse_reply_revo(const std::uint8_t* frame, std::size_t length, std::string& text) {
  if (parse_reply(frame, length, text)) return true;
  text.clear();
  if (length < 4 || frame[0] != '(' || frame[length - 1] != '\r') return false;
  unsigned sum = 1;
  for (std::size_t i = 0; i + 2 < length; ++i) sum += frame[i];
  if ((sum & 0xFF) != frame[length - 2]) return false;
  text.assign(reinterpret_cast<const char*>(frame) + 1, length - 3);
  return true;
}

// Finds replies in a byte stream: bytes go in one at a time, a whole frame (from '(' to CR) comes out. A '(' inside a frame starts over.
class ReplyFramer {
 public:
  bool feed(std::uint8_t byte, std::vector<std::uint8_t>& frame) {
    if (byte == '(') { buffer_.clear(); buffer_.push_back(byte); return false; }
    if (buffer_.empty()) return false;
    if (buffer_.size() >= kMaxReply) { buffer_.clear(); return false; }
    buffer_.push_back(byte);
    if (byte != '\r') return false;
    frame = buffer_;
    buffer_.clear();
    return true;
  }
 private:
  static constexpr std::size_t kMaxReply = 256;
  std::vector<std::uint8_t> buffer_;
};

// ---- the fields of a reply ---------------------------------------------------------------------------------------------------------

inline std::vector<std::string> split_fields(const std::string& text) {
  std::vector<std::string> fields;
  std::string current;
  for (char c : text) {
    if (c == ' ') { if (!current.empty()) { fields.push_back(current); current.clear(); } }
    else current += c;
  }
  if (!current.empty()) fields.push_back(current);
  return fields;
}

namespace detail {
// A plain decimal number ("230.0", "-12", "0161"): digits with at most one point and an optional sign; false for anything else.
inline bool number(const std::string& text, double& out) {
  if (text.empty() || text.size() > 12) return false;
  std::size_t i = 0;
  bool negative = false;
  if (text[0] == '-' || text[0] == '+') { negative = text[0] == '-'; i = 1; }
  if (i >= text.size()) return false;
  double whole = 0, fraction = 0, scale = 1;
  bool point = false, digits = false;
  for (; i < text.size(); ++i) {
    const char c = text[i];
    if (c == '.') { if (point) return false; point = true; continue; }
    if (c < '0' || c > '9') return false;
    digits = true;
    if (point) { scale /= 10; fraction += (c - '0') * scale; } else whole = whole * 10 + (c - '0');
  }
  if (!digits) return false;
  out = negative ? -(whole + fraction) : whole + fraction;
  return true;
}
inline bool bits(const std::string& text, std::size_t count, std::uint32_t& out) {
  if (text.size() != count) return false;
  out = 0;
  for (char c : text) { if (c != '0' && c != '1') return false; out = (out << 1) | (c == '1' ? 1u : 0u); }
  return true;
}
}  // namespace detail

// ---- QPIGS: the general status ---------------------------------------------------------------------------------------------------

struct Status {
  double grid_v = 0, grid_hz = 0, out_v = 0, out_hz = 0;
  double out_va = 0, out_w = 0, load_percent = 0, bus_v = 0;
  double battery_v = 0, battery_charge_a = 0, battery_percent = 0, heatsink_c = 0;
  double pv_a = 0, pv_v = 0, battery_v_scc = 0, battery_discharge_a = 0;
  // The device status bits b7..b0: the last eight characters of the reply's status field, most significant first.
  bool ac_charging = false, scc_charging = false, charging = false, battery_steady = false, load_on = false, config_changed = false, scc_updated = false, sbu_priority = false;
  double pv_w = 0;                 // absent on old models: then pv_v * pv_a
  bool has_pv_w = false;
  // Battery current with sign: positive charging, negative discharging.
  double battery_a() const { return battery_charge_a - battery_discharge_a; }
};

inline bool parse_qpigs(const std::string& text, Status& out) {
  const std::vector<std::string> f = split_fields(text);
  if (f.size() < 17) return false;
  Status s;
  double* into[16] = {&s.grid_v, &s.grid_hz, &s.out_v, &s.out_hz, &s.out_va, &s.out_w, &s.load_percent, &s.bus_v,
                      &s.battery_v, &s.battery_charge_a, &s.battery_percent, &s.heatsink_c, &s.pv_a, &s.pv_v, &s.battery_v_scc, &s.battery_discharge_a};
  for (std::size_t i = 0; i < 16; ++i) if (!detail::number(f[i], *into[i])) return false;
  std::uint32_t bits = 0;
  if (!detail::bits(f[16], 8, bits)) return false;
  s.sbu_priority = bits & 0x80; s.config_changed = bits & 0x40; s.scc_updated = bits & 0x20; s.load_on = bits & 0x10;
  s.battery_steady = bits & 0x08; s.charging = bits & 0x04; s.scc_charging = bits & 0x02; s.ac_charging = bits & 0x01;
  if (f.size() >= 20 && detail::number(f[19], s.pv_w)) s.has_pv_w = true;
  else s.pv_w = s.pv_v * s.pv_a;
  out = s;
  return true;
}

// The REVO arrangement of QPIGS: the same until the twelfth field, then the PV power in watts (not the current into the battery), the PV voltage, the charger's battery voltage and the
// energy made today in Wh (there is no discharge current in it). The status bits are read like the standard ones.
inline bool parse_qpigs_revo(const std::string& text, Status& out) {
  const std::vector<std::string> f = split_fields(text);
  if (f.size() < 17) return false;
  Status s;
  double* into[12] = {&s.grid_v, &s.grid_hz, &s.out_v, &s.out_hz, &s.out_va, &s.out_w, &s.load_percent, &s.bus_v, &s.battery_v, &s.battery_charge_a, &s.battery_percent, &s.heatsink_c};
  for (std::size_t i = 0; i < 12; ++i) if (!detail::number(f[i], *into[i])) return false;
  double energy = 0;
  if (!detail::number(f[12], s.pv_w) || !detail::number(f[13], s.pv_v) || !detail::number(f[14], s.battery_v_scc) || !detail::number(f[15], energy)) return false;
  std::uint32_t bits = 0;
  if (!detail::bits(f[16], 8, bits)) return false;
  s.sbu_priority = bits & 0x80; s.config_changed = bits & 0x40; s.scc_updated = bits & 0x20; s.load_on = bits & 0x10;
  s.battery_steady = bits & 0x08; s.charging = bits & 0x04; s.scc_charging = bits & 0x02; s.ac_charging = bits & 0x01;
  s.has_pv_w = true;
  s.pv_a = s.pv_v > 1.0 ? s.pv_w / s.pv_v : 0.0;
  out = s;
  return true;
}

// ---- QMOD: the mode ----------------------------------------------------------------------------------------------------------------

// The letter of the reply: P power on, S standby, L line, B battery, F fault, H power saving, D shutdown. False for anything else.
inline bool parse_qmod(const std::string& text, char& mode) {
  if (text.size() != 1) return false;
  const char c = text[0];
  if (c != 'P' && c != 'S' && c != 'L' && c != 'B' && c != 'F' && c != 'H' && c != 'D') return false;
  mode = c;
  return true;
}
inline const char* mode_name(char mode) {
  switch (mode) { case 'P': return "power_on"; case 'S': return "standby"; case 'L': return "line"; case 'B': return "battery"; case 'F': return "fault"; case 'H': return "power_saving"; case 'D': return "shutdown"; }
  return "unknown";
}

// ---- QPIWS: warnings and faults ----------------------------------------------------------------------------------------------------

// The names of the flags by bit position (the first character of the reply is bit 0). "" marks a reserved bit. The names follow the public document.
inline const char* warning_name(std::size_t bit) {
  static const char* const kNames[36] = {
      "pv_loss", "inverter_fault", "bus_over", "bus_under", "bus_soft_fail", "line_fail", "opv_short", "inverter_voltage_low",
      "inverter_voltage_high", "over_temperature", "fan_locked", "battery_voltage_high", "battery_low", "", "battery_under_shutdown", "battery_derating",
      "overload", "eeprom_fault", "inverter_over_current", "inverter_soft_fail", "self_test_fail", "op_dc_voltage_over", "battery_open", "current_sensor_fail",
      "battery_short", "power_limit", "pv_voltage_high", "mppt_overload_fault", "mppt_overload_warning", "battery_too_low_to_charge", "", "battery_weak",
      "battery_weak", "battery_weak", "", "battery_equalisation"};
  return bit < 36 ? kNames[bit] : "";
}

// The active flags of a reply of 32 to 36 '0' and '1' (some models send fewer: the missing ones are off). Names that repeat (the three battery_weak bits) are given once.
// False when the text is not flags.
inline bool parse_qpiws(const std::string& text, std::vector<std::string>& active) {
  active.clear();
  if (text.empty() || text.size() > 36) return false;
  for (char c : text) if (c != '0' && c != '1') return false;
  for (std::size_t bit = 0; bit < text.size() && bit < 36; ++bit) {
    if (text[bit] != '1' || warning_name(bit)[0] == '\0') continue;
    const std::string name = warning_name(bit);
    bool seen = false;
    for (const std::string& known : active) if (known == name) seen = true;
    if (!seen) active.push_back(name);
  }
  return true;
}

// ---- QPIRI: the ratings and settings -----------------------------------------------------------------------------------------------

struct Ratings {
  double grid_rating_v = 0, grid_rating_a = 0, out_rating_v = 0, out_rating_hz = 0, out_rating_a = 0, out_rating_va = 0, out_rating_w = 0;
  double battery_rating_v = 0, battery_recharge_v = 0, battery_under_v = 0, battery_bulk_v = 0, battery_float_v = 0;
  int battery_type = -1;                // 0 AGM, 1 flooded, 2 user
  double max_ac_charge_a = 0, max_charge_a = 0;
  int input_range = -1;                 // 0 appliance, 1 UPS
  int output_priority = -1;             // 0 utility first, 1 solar first, 2 SBU
  int charger_priority = -1;            // 0 utility first, 1 solar first, 2 solar and utility, 3 solar only
};

inline bool parse_qpiri(const std::string& text, Ratings& out) {
  const std::vector<std::string> f = split_fields(text);
  if (f.size() < 18) return false;
  Ratings r;
  double* into[12] = {&r.grid_rating_v, &r.grid_rating_a, &r.out_rating_v, &r.out_rating_hz, &r.out_rating_a, &r.out_rating_va, &r.out_rating_w,
                      &r.battery_rating_v, &r.battery_recharge_v, &r.battery_under_v, &r.battery_bulk_v, &r.battery_float_v};
  for (std::size_t i = 0; i < 12; ++i) if (!detail::number(f[i], *into[i])) return false;
  double v = 0;
  if (!detail::number(f[12], v)) return false;
  r.battery_type = static_cast<int>(v);
  if (!detail::number(f[13], r.max_ac_charge_a) || !detail::number(f[14], r.max_charge_a)) return false;
  if (!detail::number(f[15], v)) return false;
  r.input_range = static_cast<int>(v);
  if (!detail::number(f[16], v)) return false;
  r.output_priority = static_cast<int>(v);
  if (!detail::number(f[17], v)) return false;
  r.charger_priority = static_cast<int>(v);
  out = r;
  return true;
}

}  // namespace armor::solar::voltronic

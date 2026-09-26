// ARMOR-SOLAR - Pylontech batteries (US2000, US3000, US5000 and the C versions): the text of the console, and the frames of the RS485 protocol.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
//
// Sources: what the batteries print on their console port and the public description of their RS485 frame; NOTHING here has been connected to a battery.
//
// CONSOLE (RJ45 "console", RS232 levels, 115200 baud 8N1): typing `pwr` and Enter prints a table with one row per module of the stack, the master
// answering for all of them, then "Command completed successfully" and a prompt. Columns, in order:
//     Power  Volt(mV)  Curr(mA)  Tempr(m°C)  Tlow  Thigh  Vlow(mV)  Vhigh(mV)  Base.St  Volt.St  Curr.St  Temp.St  Coulomb(%)  Time(date time)  B.V.St  B.T.St
// A module that is not there has "Absent" in its row. Older firmwares print fewer columns: the parser takes a row that has at least the first
// thirteen fields and leaves the rest empty.
//
// RS485 (the other RJ45, 9600 or 115200 baud): frames of ASCII hexadecimal  '~' VER ADR CID1 CID2 LENGTH INFO CHKSUM CR,
//     LENGTH = the length of INFO in characters (12 bits) with a 4-bit check in front: the check is the sum of the three 4-bit groups of the length,
//              inverted, plus one, in 4 bits; CHKSUM = the sum of the ASCII codes from VER to the end of INFO, inverted, plus one, in 16 bits.
// Only the framing is here; the meaning of INFO differs between versions of the protocol and is not decoded until a capture from a real battery exists.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace armor::solar::pylontech {

// ---- the console ---------------------------------------------------------------------------------------------------------------------

struct Module {
  int number = 0;
  bool present = false;
  double voltage_v = 0, current_a = 0, temperature_c = 0, temperature_low_c = 0, temperature_high_c = 0;
  double cell_low_v = 0, cell_high_v = 0;
  int soc_percent = -1;               // the "Coulomb" column
  double capacity_ah = -1, full_capacity_ah = -1;   // remaining and total capacity in ampere-hours, from the console's `info` text; -1 when it was not read
  int cycles = -1;
  std::string model;                  // the device name from `info` (US3000C ...)
  std::vector<double> cells_v;        // the voltage of each cell, in volts, from the console's `bat` table; empty when it was not read
  std::vector<double> temperatures_c;  // every temperature sensor of the module (a BMS that lists them); empty when the equipment does not
  std::string base_state, voltage_state, current_state, temperature_state;   // Idle, Charge, Dischg, Normal, Absent ...
};

namespace detail {
inline std::vector<std::string> tokens(const std::string& line) {
  std::vector<std::string> out;
  std::string current;
  for (char c : line) {
    if (c == ' ' || c == '\t' || c == '\r') { if (!current.empty()) { out.push_back(current); current.clear(); } }
    else current += c;
  }
  if (!current.empty()) out.push_back(current);
  return out;
}
inline bool integer(const std::string& text, long& out) {
  if (text.empty() || text.size() > 11) return false;
  std::size_t i = (text[0] == '-' || text[0] == '+') ? 1 : 0;
  if (i >= text.size()) return false;
  long value = 0;
  for (std::size_t k = i; k < text.size(); ++k) { if (text[k] < '0' || text[k] > '9') return false; value = value * 10 + (text[k] - '0'); }
  out = text[0] == '-' ? -value : value;
  return true;
}
}  // namespace detail

// The modules of the text a `pwr` command printed (any number of lines, with or without the header and the prompt). Returns how many rows were read.
inline std::size_t parse_pwr(const std::string& text, std::vector<Module>& modules) {
  modules.clear();
  std::string line;
  const auto handle = [&](const std::string& row) {
    const std::vector<std::string> t = detail::tokens(row);
    long number = 0;
    if (t.empty() || !detail::integer(t[0], number) || number < 1 || number > 16) return;
    Module m;
    m.number = static_cast<int>(number);
    bool absent = false;
    for (const std::string& token : t) if (token == "Absent") absent = true;
    if (absent) { m.present = false; m.base_state = "Absent"; modules.push_back(m); return; }
    if (t.size() < 13) return;
    long v[7] = {};
    for (std::size_t i = 0; i < 7; ++i) if (!detail::integer(t[1 + i], v[i])) return;
    m.present = true;
    m.voltage_v = v[0] / 1000.0; m.current_a = v[1] / 1000.0; m.temperature_c = v[2] / 1000.0;
    m.temperature_low_c = v[3] / 1000.0; m.temperature_high_c = v[4] / 1000.0; m.cell_low_v = v[5] / 1000.0; m.cell_high_v = v[6] / 1000.0;
    m.base_state = t[8]; m.voltage_state = t[9]; m.current_state = t[10]; m.temperature_state = t[11];
    {
      const std::string& soc = t[12];
      long percent = 0;
      if (!soc.empty() && soc.back() == '%' && detail::integer(soc.substr(0, soc.size() - 1), percent) && percent >= 0 && percent <= 100) m.soc_percent = static_cast<int>(percent);
    }
    modules.push_back(m);
  };
  for (char c : text) { if (c == '\n') { handle(line); line.clear(); } else line += c; }
  handle(line);
  return modules.size();
}

// The cells of a module from the text a `bat <module>` command printed: one row per cell with the index, the voltage (mV), the current (mA), the temperature
// (m°C), the four states and the charge. Returns how many cells were read; a row that is not one is skipped, and a cell voltage out of 0 to 10 V refuses the whole table.
inline std::size_t parse_bat(const std::string& text, std::vector<double>& cells) {
  cells.clear();
  std::string line;
  bool bad = false;
  const auto handle = [&](const std::string& row) {
    const std::vector<std::string> t = detail::tokens(row);
    long index = 0, millivolts = 0;
    if (t.size() < 8 || !detail::integer(t[0], index) || index < 0 || index > 31 || !detail::integer(t[1], millivolts)) return;
    if (millivolts < 0 || millivolts > 10000) { bad = true; return; }
    if (static_cast<std::size_t>(index) != cells.size()) { bad = true; return; }   // the cells come in order, from 0
    cells.push_back(millivolts / 1000.0);
  };
  for (char c : text) { if (c == '\n') { handle(line); line.clear(); } else line += c; }
  handle(line);
  if (bad) cells.clear();
  return cells.size();
}

// What a module says about itself in the text of the console's `info <module>` command: lines of "Name : value". Read: the device name, the remaining and the total
// capacity (in mAH, or Ah when the unit says so) and the cycle count. Names are matched without regard to case and spaces; a line that is not one is skipped. Returns
// how many of the four were found. The exact names differ between firmware versions, so a missing one is left unset rather than guessed.
inline int parse_info(const std::string& text, Module& module) {
  int found = 0;
  std::string line;
  const auto lower = [](std::string s) { for (char& c : s) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a'); return s; };
  const auto trim = [](std::string s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r')) s.erase(s.begin());
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.pop_back();
    return s;
  };
  const auto amp_hours = [&](const std::string& value, double& out) {
    const std::vector<std::string> t = detail::tokens(value);
    long n = 0;
    if (t.empty() || !detail::integer(t[0], n) || n < 0) return false;
    const std::string unit = t.size() > 1 ? lower(t[1]) : "mah";
    out = unit == "ah" ? static_cast<double>(n) : n / 1000.0;
    return out <= 100000.0;
  };
  const auto handle = [&](const std::string& row) {
    const std::size_t colon = row.find(':');
    if (colon == std::string::npos) return;
    const std::string name = lower(trim(row.substr(0, colon))), value = trim(row.substr(colon + 1));
    long n = 0;
    if (name == "device name" || name == "devicename") { if (!value.empty() && value.size() <= 24) { module.model = value; ++found; } }
    else if (name == "remain capacity" || name == "remaining capacity") { if (amp_hours(value, module.capacity_ah)) ++found; }
    else if (name == "total capacity" || name == "full capacity" || name == "capacity") { if (amp_hours(value, module.full_capacity_ah)) ++found; }
    else if (name == "cycle times" || name == "cycle" || name == "cycles") { const std::vector<std::string> t = detail::tokens(value); if (!t.empty() && detail::integer(t[0], n) && n >= 0 && n <= 1000000) { module.cycles = static_cast<int>(n); ++found; } }
  };
  for (char c : text) { if (c == '\n') { handle(line); line.clear(); } else line += c; }
  handle(line);
  return found;
}

// Puts the cells of `bat <number>` on the module of that number (in the list `parse_pwr` made); false when the module is not in the list or not present.
inline bool attach_cells(std::vector<Module>& modules, int number, const std::vector<double>& cells) {
  for (Module& m : modules) if (m.number == number && m.present) { m.cells_v = cells; return true; }
  return false;
}

// The whole stack in a few numbers: the modules that are present, their mean voltage, the total current, the temperature range and the mean state of charge.
struct Stack {
  int modules = 0;
  double voltage_v = 0, current_a = 0, temperature_min_c = 0, temperature_max_c = 0;
  int soc_percent = -1;
  double cell_low_v = 0, cell_high_v = 0;
  double capacity_ah = -1, full_capacity_ah = -1, energy_kwh = -1;   // the modules' add up (they are in parallel); -1 when no module said
  int cycles = -1;                    // of the most used module
  std::string model;                  // the first module's
  std::string state;                  // "charging", "discharging", "idle"
  bool alarm = false;                 // any state column that is not Normal, Idle, Charge or Dischg
};

inline Stack summarise(const std::vector<Module>& modules) {
  Stack s;
  double voltage = 0, soc = 0, current = 0;
  int with_soc = 0;
  bool first = true;
  for (const Module& m : modules) {
    if (!m.present) continue;
    ++s.modules;
    voltage += m.voltage_v;
    current += m.current_a;
    if (m.soc_percent >= 0) { soc += m.soc_percent; ++with_soc; }
    double cell_low = m.cell_low_v, cell_high = m.cell_high_v;
    if (!m.cells_v.empty()) {   // the cells' own extremes are truer than the module's Vlow and Vhigh columns
      cell_low = m.cells_v[0]; cell_high = m.cells_v[0];
      for (double v : m.cells_v) { if (v < cell_low) cell_low = v; if (v > cell_high) cell_high = v; }
    }
    if (first) { s.temperature_min_c = m.temperature_low_c; s.temperature_max_c = m.temperature_high_c; s.cell_low_v = cell_low; s.cell_high_v = cell_high; first = false; }
    else {
      if (m.temperature_low_c < s.temperature_min_c) s.temperature_min_c = m.temperature_low_c;
      if (m.temperature_high_c > s.temperature_max_c) s.temperature_max_c = m.temperature_high_c;
      if (cell_low < s.cell_low_v) s.cell_low_v = cell_low;
      if (cell_high > s.cell_high_v) s.cell_high_v = cell_high;
    }
    if (m.capacity_ah >= 0) s.capacity_ah = (s.capacity_ah < 0 ? 0 : s.capacity_ah) + m.capacity_ah;
    if (m.full_capacity_ah >= 0) s.full_capacity_ah = (s.full_capacity_ah < 0 ? 0 : s.full_capacity_ah) + m.full_capacity_ah;
    if (m.cycles > s.cycles) s.cycles = m.cycles;
    if (s.model.empty()) s.model = m.model;
    for (const std::string* state : {&m.voltage_state, &m.current_state, &m.temperature_state}) if (*state != "Normal" && !state->empty()) s.alarm = true;
  }
  if (s.modules > 0) {
    s.voltage_v = voltage / s.modules;   // the modules of a stack are in parallel: the same voltage, so the mean is what the bus has
    s.current_a = current;               // and their currents add
    if (s.capacity_ah >= 0) s.energy_kwh = s.capacity_ah * s.voltage_v / 1000.0;
    if (with_soc > 0) s.soc_percent = static_cast<int>(soc / with_soc + 0.5);
    s.state = s.current_a > 0.5 ? "charging" : s.current_a < -0.5 ? "discharging" : "idle";
  }
  return s;
}

// ---- the RS485 frame -----------------------------------------------------------------------------------------------------------------

inline char hex_digit(unsigned value) { return "0123456789ABCDEF"[value & 0xF]; }
inline std::string hex(unsigned value, int digits) {
  std::string out;
  for (int shift = (digits - 1) * 4; shift >= 0; shift -= 4) out += hex_digit(value >> shift);
  return out;
}

// The four hexadecimal characters of LENGTH for an INFO of `info_length` characters: the check nibble and the 12-bit length.
inline std::string length_field(std::size_t info_length) {
  const unsigned len = static_cast<unsigned>(info_length) & 0xFFF;
  const unsigned sum = (len & 0xF) + ((len >> 4) & 0xF) + ((len >> 8) & 0xF);
  const unsigned check = ((~sum) + 1) & 0xF;
  return hex(check, 1) + hex(len, 3);
}

// The checksum characters for the text from VER to the end of INFO.
inline std::string frame_checksum(std::string_view body) {
  unsigned sum = 0;
  for (char c : body) sum += static_cast<unsigned char>(c);
  return hex(((~sum) + 1) & 0xFFFF, 4);
}

// A frame: '~' VER ADR CID1 CID2 LENGTH INFO CHKSUM CR. `info` is the hexadecimal text of INFO (may be empty).
inline std::string build_frame(unsigned version, unsigned address, unsigned cid1, unsigned cid2, std::string_view info) {
  std::string body = hex(version, 2) + hex(address, 2) + hex(cid1, 2) + hex(cid2, 2) + length_field(info.size()) + std::string(info);
  return "~" + body + frame_checksum(body) + "\r";
}

struct Frame {
  unsigned version = 0, address = 0, cid1 = 0, cid2 = 0;   // cid2 of a reply is its return code
  std::string info;
};

inline bool parse_frame(std::string_view text, Frame& out) {
  if (text.size() < 18 || text.front() != '~' || text.back() != '\r') return false;
  const std::string_view body = text.substr(1, text.size() - 6);   // between '~' and the four checksum characters and CR
  for (char c : text.substr(1, text.size() - 2)) if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F'))) return false;
  if (frame_checksum(body) != text.substr(text.size() - 5, 4)) return false;
  const auto number = [&](std::size_t at, std::size_t digits) {
    unsigned value = 0;
    for (std::size_t i = 0; i < digits; ++i) { const char c = body[at + i]; value = value * 16 + (c <= '9' ? static_cast<unsigned>(c - '0') : static_cast<unsigned>(c - 'A' + 10)); }
    return value;
  };
  const std::size_t info_length = number(9, 3);
  if (body.size() != 12 + info_length || length_field(info_length) != body.substr(8, 4)) return false;
  out.version = number(0, 2); out.address = number(2, 2); out.cid1 = number(4, 2); out.cid2 = number(6, 2);
  out.info = std::string(body.substr(12));
  return true;
}

}  // namespace armor::solar::pylontech

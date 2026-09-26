// ARMOR-SOLAR - the messages a gateway node publishes for an inverter and for a battery stack (contract version 0, see docs/SOLAR_MESSAGES.md).
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
//
//   armor/solar/<node>/<device>/state     one message per device, every few seconds and on a change; JSON, no retained flag.
//   kind "inverter": the readings of one inverter (mode, grid, output, battery side, PV, warnings).
//   kind "battery":  the readings of one battery stack (state of charge, voltage, current, temperatures, cell range, modules).
// `node_id` and `device` are lowercase letters, digits, - and _ (the same rule as everywhere in A.R.M.O.R.); `timestamp_ms` is the node's clock.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "json.hpp"
#include "pylontech.hpp"
#include "voltronic.hpp"

namespace armor::solar {

inline bool valid_name(const std::string& text) {
  if (text.empty() || text.size() > 32) return false;
  if (text.front() == '-' || text.front() == '_') return false;
  for (char c : text) if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_')) return false;
  return true;
}

inline std::string topic(const std::string& node_id, const std::string& device) {
  if (!valid_name(node_id) || !valid_name(device)) return "";
  return "armor/solar/" + node_id + "/" + device + "/state";
}

// An inverter's message. `mode` is the letter of QMOD (0 when unknown), `warnings` the names of QPIWS's active flags.
inline std::string inverter_json(const std::string& node_id, const std::string& device, std::uint64_t timestamp_ms, char mode, const voltronic::Status& s,
                                 const std::vector<std::string>& warnings) {
  json::Writer w;
  w.begin_object().field("kind", "inverter").field("node_id", node_id).field("device", device).key("timestamp_ms").integer(static_cast<long long>(timestamp_ms));
  w.field("mode", mode == 0 ? "unknown" : voltronic::mode_name(mode));
  w.key("grid_v").number(s.grid_v, 1).key("grid_hz").number(s.grid_hz, 1).key("out_v").number(s.out_v, 1).key("out_hz").number(s.out_hz, 1);
  w.key("out_va").number(s.out_va, 0).key("out_w").number(s.out_w, 0).key("load_percent").number(s.load_percent, 0);
  w.key("battery_v").number(s.battery_v, 2).key("battery_a").number(s.battery_a(), 1).key("battery_percent").number(s.battery_percent, 0);
  w.key("pv_v").number(s.pv_v, 1).key("pv_a").number(s.pv_a, 1).key("pv_w").number(s.pv_w, 0).key("heatsink_c").number(s.heatsink_c, 0);
  w.field("ac_charging", s.ac_charging).field("pv_charging", s.scc_charging).field("load_on", s.load_on);
  w.key("warnings").begin_array();
  for (const std::string& name : warnings) w.string(name);
  w.end_array().end_object();
  return w.str();
}

// A battery stack's message from the console's table.
inline std::string battery_json(const std::string& node_id, const std::string& device, std::uint64_t timestamp_ms, const std::vector<pylontech::Module>& modules) {
  const pylontech::Stack s = pylontech::summarise(modules);
  json::Writer w;
  w.begin_object().field("kind", "battery").field("node_id", node_id).field("device", device).key("timestamp_ms").integer(static_cast<long long>(timestamp_ms));
  w.field("modules", s.modules);
  if (s.modules > 0 && !s.model.empty()) w.field("model", s.model);
  if (s.modules > 0) {
    w.field("state", s.state).key("voltage_v").number(s.voltage_v, 2).key("current_a").number(s.current_a, 2);
    w.key("temperature_min_c").number(s.temperature_min_c, 1).key("temperature_max_c").number(s.temperature_max_c, 1);
    w.key("cell_min_v").number(s.cell_low_v, 3).key("cell_max_v").number(s.cell_high_v, 3);
    if (s.soc_percent >= 0) w.field("soc_percent", s.soc_percent);   // left out when unknown: the contract has no nulls
    w.field("alarm", s.alarm);
    if (s.capacity_ah >= 0) w.key("capacity_ah").number(s.capacity_ah, 2);
    if (s.full_capacity_ah >= 0) w.key("full_capacity_ah").number(s.full_capacity_ah, 2);
    if (s.energy_kwh >= 0) w.key("energy_kwh").number(s.energy_kwh, 2);
    if (s.cycles >= 0) w.field("cycles", s.cycles);
  }
  w.key("stack").begin_array();
  for (const pylontech::Module& m : modules) {
    w.begin_object().field("n", m.number).field("present", m.present);
    if (m.present) {
      w.key("voltage_v").number(m.voltage_v, 3).key("current_a").number(m.current_a, 3).key("temperature_c").number(m.temperature_c, 1);
      if (m.soc_percent >= 0) w.field("soc_percent", m.soc_percent);
      w.field("state", m.base_state);
      if (m.capacity_ah >= 0) w.key("capacity_ah").number(m.capacity_ah, 2);
      if (m.full_capacity_ah >= 0) w.key("full_capacity_ah").number(m.full_capacity_ah, 2);
      if (m.cycles >= 0) w.field("cycles", m.cycles);
      if (!m.cells_v.empty()) { w.key("cells_v").begin_array(); for (double v : m.cells_v) w.number(v, 3); w.end_array(); }
      if (!m.temperatures_c.empty()) { w.key("temperatures_c").begin_array(); for (double v : m.temperatures_c) w.number(v, 1); w.end_array(); }
    }
    w.end_object();
  }
  w.end_array().end_object();
  return w.str();
}

}  // namespace armor::solar

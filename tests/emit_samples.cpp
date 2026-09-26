// ARMOR-SOLAR - prints the messages a node would publish, one per line ("inverter <json>" or "battery <json>"), for tests/check_samples.py.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
#include <cstdio>

#include "../core/solar_json.hpp"

int main() {
  using namespace armor::solar;
  voltronic::Status s;
  voltronic::parse_qpigs("232.0 50.0 230.0 50.0 0161 0119 003 460 57.50 012 100 0069 0014 103.8 57.45 00000 00110110 00 00 00856", s);
  std::printf("inverter %s\n", inverter_json("solar-1", "axpert-1", 1000, 'L', s, {"line_fail", "battery_low"}).c_str());
  std::printf("inverter %s\n", inverter_json("solar-1", "axpert-2", 2000, 0, s, {}).c_str());
  std::vector<pylontech::Module> modules;
  pylontech::parse_pwr("1 49872 -1280 22000 20000 25000 3330 3348 Dischg Normal Normal Normal 88% 2018-11-14 15:12:04 Normal Normal\n2 -  - - - - - - Absent - - - -\n", modules);
  std::printf("battery %s\n", battery_json("solar-1", "us3000-1", 3000, modules).c_str());
  std::printf("battery %s\n", battery_json("solar-1", "us3000-2", 4000, {}).c_str());
  return 0;
}

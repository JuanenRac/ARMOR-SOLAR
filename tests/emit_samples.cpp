// ARMOR-SOLAR - prints the messages a node would publish, one per line ("armor/solar/<node>/<device>/state <json>"), for tests/check_samples.py.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
#include <cstdio>
#include <string>

#include "../core/solar_json.hpp"

static void emit(const std::string& node, const std::string& device, const std::string& payload) {
  std::printf("%s %s\n", armor::solar::topic(node, device).c_str(), payload.c_str());
}

int main() {
  using namespace armor::solar;
  voltronic::Status s;
  voltronic::parse_qpigs("232.0 50.0 230.0 50.0 0161 0119 003 460 57.50 012 100 0069 0014 103.8 57.45 00000 00110110 00 00 00856", s);
  emit("solar-1", "axpert-1", inverter_json("solar-1", "axpert-1", 1000, 'L', s, {"line_fail", "battery_low"}));
  emit("solar-1", "axpert-2", inverter_json("solar-1", "axpert-2", 2000, 0, s, {}));
  // a stack of two modules and a third that is not there, one with its cells and its capacities
  std::vector<pylontech::Module> modules;
  pylontech::parse_pwr("1 49872 -1280 22000 20000 25000 3330 3348 Dischg Normal Normal Normal 88% 2018-11-14 15:12:04 Normal Normal\n"
                       "2 49870 -1310 21500 19500 24500 3328 3349 Dischg Normal Normal Normal 87% 2018-11-14 15:12:04 Normal Normal\n"
                       "3 - - - - - - - Absent - - - -\n", modules);
  std::string bat = "Battery Volt Curr Tempr Base State Volt. State Curr. State Temp. State Coulomb\n";
  for (int i = 0; i < 15; ++i) bat += std::to_string(i) + " " + std::to_string(3324 + (i * 7) % 25) + " -1281 22000 Dischg Normal Normal Normal 88%\n";
  std::vector<double> cells;
  pylontech::parse_bat(bat, cells);
  pylontech::attach_cells(modules, 1, cells);
  pylontech::parse_info("Device name : US3000C\nRemain Capacity : 65100 mAH\nTotal Capacity : 74000 mAH\nCycle Times : 312\n", modules[0]);
  pylontech::parse_info("Device name : US3000C\nRemain Capacity : 60000 mAH\nTotal Capacity : 74000 mAH\nCycle Times : 340\n", modules[1]);
  emit("solar-1", "us3000-1", battery_json("solar-1", "us3000-1", 3000, modules));
  emit("solar-1", "us3000-2", battery_json("solar-1", "us3000-2", 4000, {}));
  return 0;
}

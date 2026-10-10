// ARMOR-SOLAR - prints the messages a node's PORTS make from stand-in equipment, one per line ("armor/solar/<node>/<device>/state <json>"), for tests/check_samples.py.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
// It is the proof that what the node publishes through the whole chain (request, answer, check, reading, message) is accepted by ARMOR-COMMON's contract.
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include "../core/poller.hpp"
#include "ant_frames.hpp"

using namespace armor;
using namespace armor::solar;

static std::vector<std::uint8_t> reply(const std::string& text) {
  std::vector<std::uint8_t> frame{'('};
  frame.insert(frame.end(), text.begin(), text.end());
  const auto crc = voltronic::wire_crc(frame.data(), frame.size());
  frame.push_back(crc[0]); frame.push_back(crc[1]); frame.push_back('\r');
  return frame;
}
static std::vector<std::uint8_t> bytes(const std::string& text) { return std::vector<std::uint8_t>(text.begin(), text.end()); }

static void run(Poller& poller, std::map<std::string, std::vector<std::uint8_t>>& answers, std::uint64_t until_ms) {
  std::vector<std::pair<std::uint64_t, std::vector<std::uint8_t>>> in_flight;
  for (std::uint64_t now = 0; now < until_ms; now += 10) {
    for (auto it = in_flight.begin(); it != in_flight.end();) {
      if (it->first <= now) { poller.on_rx(it->second.data(), it->second.size(), now); it = in_flight.erase(it); } else ++it;
    }
    const std::vector<std::uint8_t> out = poller.next_tx(now);
    if (!out.empty()) {
      std::string command(out.begin(), out.end());
      if (command.size() > 3 && command[0] == 'Q') command = command.substr(0, command.size() - 3);
      if (!command.empty() && command[0] == '\r') command.erase(0, 1);   // a bare Enter that wakes a console goes before the first `pwr`
      const auto answer = answers.find(command);
      if (answer != answers.end()) in_flight.push_back({now + 40, answer->second});
    }
    std::string topic, payload;
    if (poller.take_message(1700000000000ULL + now, topic, payload)) std::printf("%s %s\n", topic.c_str(), payload.c_str());
  }
}

int main() {
  std::map<std::string, std::vector<std::uint8_t>> inverter;
  inverter["QPIGS"] = reply("232.0 50.0 230.0 50.0 0161 0119 003 460 57.50 012 100 0069 0014 103.8 57.45 00000 00110110 00 00 00856");
  inverter["QMOD"] = reply("L");
  inverter["QPIWS"] = reply("00000100000000000000000000000000");
  Poller inverter_port(config::Kind::kVoltronic, "solar-1", "axpert-1", 5, 0);
  run(inverter_port, inverter, 11000);

  // an inverter with a second PV input, in a parallel system of two units
  std::map<std::string, std::vector<std::uint8_t>> big = inverter;
  big["QPIGS2"] = reply("03.1 327.3 01026");
  big["QPGS0"] = reply("1 92931701100510 B 00 000.0 00.00 230.6 50.00 0275 0141 005 51.4 001 100 083.3 002 00574 00312 003 10100110 1 2 060 120 10 04 000");
  big["QPGS1"] = reply("1 92931701100511 L 00 231.0 50.00 230.6 50.00 0299 0171 006 51.4 001 100 084.0 002 00574 00312 003 10100110 1 2 060 120 10 04 000");
  Poller big_port(config::Kind::kVoltronic, "solar-1", "axpert-big", 5, 0, "pi30");
  big_port.set_optional(true, 2);
  run(big_port, big, 11000);

  std::string bat = "@\r\nBattery  Volt     Curr     Tempr    Base State   Volt. State  Curr. State  Temp. State  Coulomb\r\n";
  for (int i = 0; i < 15; ++i) bat += std::to_string(i) + " " + std::to_string(3324 + (i * 7) % 25) + " -1281 22000 Dischg Normal Normal Normal 88%\r\n";
  bat += "Command completed successfully\r\n$$\r\npylon>";
  std::map<std::string, std::vector<std::uint8_t>> battery;
  battery["pwr\r"] = bytes(
      "@\r\nPower Volt   Curr   Tempr  Tlow   Thigh  Vlow   Vhigh  Base.St  Volt.St  Curr.St  Temp.St  Coulomb  Time                 B.V.St   B.T.St\r\n"
      "1     49872  -1280  22000  20000  25000  3330   3348   Dischg   Normal   Normal   Normal   88%      2018-11-14 15:12:04  Normal   Normal\r\n"
      "2     49870  -1310  21500  19500  24500  3328   3349   Dischg   Normal   Normal   Normal   87%      2018-11-14 15:12:04  Normal   Normal\r\n"
      "3     -      -      -      -      -      -      -      Absent   -        -        -        -        -                    -        -\r\n"
      "Command completed successfully\r\n$$\r\npylon>");
  battery["bat 1\r"] = bytes(bat);
  battery["bat 2\r"] = bytes(bat);
  const std::string info = "@\r\nDevice address      : 1\r\nDevice name         : US3000C\r\nRemain Capacity     : 65100 mAH\r\nTotal Capacity      : 74000 mAH\r\nCycle Times         : 312\r\nCommand completed successfully\r\n$$\r\npylon>";
  battery["info 1\r"] = bytes(info);
  battery["info 2\r"] = bytes(info);
  Poller battery_port(config::Kind::kPylontech, "solar-1", "us3000-1", 10, 0);
  run(battery_port, battery, 3000);

  // ANT-BMS batteries of both protocols, from frames captured on real units
  std::map<std::string, std::vector<std::uint8_t>> ant_new, ant_old;
  const auto key = [](ant::Protocol p) { const auto r = ant::request(p); return std::string(r.begin(), r.end()); };
  ant_new[key(ant::Protocol::kNew)] = kNew16S;
  ant_old[key(ant::Protocol::kOld)] = kOld8S;
  Poller ant_new_port(config::Kind::kAnt, "solar-1", "ant-1", 5, 0);
  run(ant_new_port, ant_new, 8000);
  Poller ant_old_port(config::Kind::kAnt, "solar-1", "ant-2", 5, 0);
  run(ant_old_port, ant_old, 8000);
  return 0;
}

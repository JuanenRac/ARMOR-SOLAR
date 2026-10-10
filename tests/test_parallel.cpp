// ARMOR-SOLAR - host tests of the optional readings of an inverter in the standard dialect: the second PV input (QPIGS2) and the units of a parallel system (QPGS0...).
// The replies are the ones the public mpp-solar project lists as examples; no real inverter has been read, and no inverter with two inputs or in parallel has been met.
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include "../core/poller.hpp"
#include "../core/solar_config.hpp"

static int failures = 0;
static int checks = 0;
#define CHECK(condition)                                                              \
  do {                                                                                \
    ++checks;                                                                         \
    if (!(condition)) {                                                               \
      ++failures;                                                                     \
      std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #condition);                \
    }                                                                                 \
  } while (0)

using namespace armor;
using namespace armor::solar;

static std::vector<std::uint8_t> reply(const std::string& text) {
  std::vector<std::uint8_t> frame{'('};
  frame.insert(frame.end(), text.begin(), text.end());
  const auto crc = voltronic::wire_crc(frame.data(), frame.size());
  frame.push_back(crc[0]); frame.push_back(crc[1]); frame.push_back('\r');
  return frame;
}
static std::vector<std::uint8_t> bad_crc_reply(const std::string& text) {
  std::vector<std::uint8_t> frame = reply(text);
  frame[frame.size() - 3] ^= 0x55;
  return frame;
}

static const char* kQpigs = "232.0 50.0 230.0 50.0 0161 0119 003 460 57.50 012 100 0069 0014 103.8 57.45 00000 00110110 00 00 00856";
static const char* kUnit0 = "1 92931701100510 B 00 000.0 00.00 230.6 50.00 0275 0141 005 51.4 001 100 083.3 002 00574 00312 003 10100110 1 2 060 120 10 04 000";
static const char* kUnit1 = "1 92931701100511 L 00 231.0 50.00 230.6 50.00 0299 0171 006 51.4 001 100 084.0 002 00574 00312 003 10100110 1 2 060 120 10 04 000";
static const char* kUnitOtherFirmware = "1 92912102100033 B 00 000.0 00.00 120.1 59.99 0048 0000 000 53.1 000 059 000.0 000 00154 00016 000 00000110 7 1 060 120 030 00 000 000.0 00";

struct Bench {
  Poller poller;
  std::map<std::string, std::vector<std::uint8_t>> answers;
  std::vector<std::string> asked, messages;
  std::uint64_t now = 0;
  Bench(const std::string& dialect, bool pv2, int parallel) : poller(config::Kind::kVoltronic, "solar-1", "axpert-1", 5, 0, dialect) { poller.set_optional(pv2, parallel); }
  void standard() {
    answers["QPIGS"] = reply(kQpigs);
    answers["QMOD"] = reply("L");
    answers["QPIWS"] = reply("00000100000000000000000000000000");
  }
  void run(std::uint64_t until_ms) {
    std::vector<std::pair<std::uint64_t, std::vector<std::uint8_t>>> in_flight;
    for (; now < until_ms; now += 10) {
      for (auto it = in_flight.begin(); it != in_flight.end();) {
        if (it->first <= now) { poller.on_rx(it->second.data(), it->second.size(), now); it = in_flight.erase(it); } else ++it;
      }
      const std::vector<std::uint8_t> out = poller.next_tx(now);
      if (!out.empty()) {
        std::string command(out.begin(), out.end());
        if (command.size() > 3 && command[0] == 'Q') command = command.substr(0, command.size() - 3);
        else if (command.size() > 8 && command[0] == '^') command = "^" + command.substr(5, command.size() - 8);
        if (!command.empty() && command[0] == '\r') command.erase(0, 1);   // a bare Enter that wakes a console goes before the first `pwr`
        asked.push_back(command);
        const auto answer = answers.find(command);
        if (answer != answers.end()) in_flight.push_back({now + 40, answer->second});
      }
      std::string topic, payload;
      if (poller.take_message(1700000000000ULL + now, topic, payload)) messages.push_back(payload);
    }
  }
  int count(const std::string& command) const { int n = 0; for (const std::string& a : asked) if (a == command) ++n; return n; }
};

static json::Value parse(const std::string& text) {
  json::Value doc;
  if (!json::parse(text, doc)) std::printf("not JSON: %s\n", text.c_str());
  return doc;
}

// ---- the parsers -----------------------------------------------------------------------------------------------------------------------------

static void test_qpigs2() {
  voltronic::Pv2 v;
  CHECK(voltronic::parse_qpigs2("03.1 327.3 01026", v) && v.current_a == 3.1 && v.voltage_v == 327.3 && v.power_w == 1026);
  CHECK(voltronic::parse_qpigs2("00.0 000.0 00000 extra", v) && v.power_w == 0);
  CHECK(!voltronic::parse_qpigs2("", v) && !voltronic::parse_qpigs2("NAK", v) && !voltronic::parse_qpigs2("03.1 327.3", v) && !voltronic::parse_qpigs2("03.1 abc 01026", v));
  CHECK(!voltronic::parse_qpigs2("03.1 1501.0 01026", v) && !voltronic::parse_qpigs2("501.0 327.3 01026", v) && !voltronic::parse_qpigs2("03.1 327.3 100001", v) && !voltronic::parse_qpigs2("-1.0 327.3 01026", v));
}

static void test_qpgs() {
  voltronic::ParallelUnit u;
  CHECK(voltronic::parse_qpgs(kUnit0, u) && u.serial == "92931701100510" && u.mode == 'B' && u.fault == "00" && u.grid_v == 0 && u.out_v == 230.6 && u.out_hz == 50.0);
  CHECK(u.out_va == 275 && u.out_w == 141 && u.load_percent == 5 && u.battery_v == 51.4 && u.charging_a == 1 && u.battery_percent == 100 && u.pv_v == 83.3);
  CHECK(u.total_charging_a == 2 && u.total_out_va == 574 && u.total_out_w == 312 && u.total_load_percent == 3);
  // a firmware with more fields at the end, and another mode
  CHECK(voltronic::parse_qpgs(kUnitOtherFirmware, u) && u.serial == "92912102100033" && u.out_v == 120.1 && u.total_out_w == 16 && u.pv_v == 0);
  CHECK(voltronic::parse_qpgs(kUnit1, u) && u.mode == 'L' && u.grid_v == 231.0);
  // a unit that is not there, an answer that is cut short or absurd
  CHECK(!voltronic::parse_qpgs("0 92931701100510 B 00 000.0 00.00 230.6 50.00 0275 0141 005 51.4 001 100 083.3 002 00574 00312 003 10100110", u));
  CHECK(!voltronic::parse_qpgs("NAK", u) && !voltronic::parse_qpgs("", u) && !voltronic::parse_qpgs("1 92931701100510 B 00 000.0 00.00 230.6 50.00 0275 0141", u));
  CHECK(!voltronic::parse_qpgs("1 92931701100510 BB 00 000.0 00.00 230.6 50.00 0275 0141 005 51.4 001 100 083.3 002 00574 00312 003 10100110", u));
  CHECK(!voltronic::parse_qpgs("1 92931701100510 B 0 000.0 00.00 230.6 50.00 0275 0141 005 51.4 001 100 083.3 002 00574 00312 003 10100110", u));
  CHECK(!voltronic::parse_qpgs("1 92931701100510 B 00 000.0 00.00 230.6 50.00 0275 0141 005 51.4 001 101 083.3 002 00574 00312 003 10100110", u));      // battery 101 %
  CHECK(!voltronic::parse_qpgs("1 92931701100510 B 00 000.0 00.00 230.6 50.00 0275 0141 005 51.4 001 100 083.3 002 00574 00312 xyz 10100110", u));
  CHECK(!voltronic::parse_qpgs("1 9293170110051092931701100510929317 B 00 000.0 00.00 230.6 50.00 0275 0141 005 51.4 001 100 083.3 002 00574 00312 003 10100110", u));
}

// ---- the poller ------------------------------------------------------------------------------------------------------------------------------

static void test_nothing_extra_by_default() {
  Bench b("pi30", false, 0);
  b.standard();
  b.answers["QPIGS2"] = reply("03.1 327.3 01026");
  b.run(12000);
  CHECK(b.count("QPIGS2") == 0 && b.count("QPGS0") == 0 && b.count("QPIGS") >= 2 && b.messages.size() >= 2);
  const json::Value doc = parse(b.messages[0]);
  CHECK(doc.get("pv2_v") == nullptr && doc.get("units") == nullptr && doc.get("pv_w")->number == 856);
}

static void test_second_pv_input() {
  Bench b("pi30", true, 0);
  b.standard();
  b.answers["QPIGS2"] = reply("03.1 327.3 01026");
  b.run(12000);
  // it is asked after the three readings, every cycle
  CHECK(b.asked.size() >= 8 && b.asked[0] == "QPIGS" && b.asked[1] == "QMOD" && b.asked[2] == "QPIWS" && b.asked[3] == "QPIGS2" && b.asked[4] == "QPIGS");
  CHECK(b.messages.size() >= 2);
  const json::Value doc = parse(b.messages[0]);
  // the input is in the message and pv_w is the sum of both inputs (856 W and 1026 W)
  CHECK(doc.get("pv2_v")->number == 327.3 && doc.get("pv2_a")->number == 3.1 && doc.get("pv2_w")->number == 1026 && doc.get("pv_w")->number == 1882);
  CHECK(doc.get("pv_v")->number == 103.8 && doc.get("units") == nullptr && doc.get("total_out_w") == nullptr);
}

static void test_an_inverter_with_one_input() {
  // it answers NAK: the reading is not lost, and after two tries the node stops asking
  Bench b("pi30", true, 0);
  b.standard();
  b.answers["QPIGS2"] = reply("NAK");
  b.run(40000);
  CHECK(b.count("QPIGS2") == 2 && b.count("QPIGS") >= 7 && b.messages.size() >= 7);
  for (const std::string& m : b.messages) CHECK(parse(m).get("pv2_v") == nullptr && parse(m).get("pv_w")->number == 856);
  // one that does not answer at all costs its timeout twice and no reading
  Bench silent("pi30", true, 0);
  silent.standard();
  silent.run(40000);
  CHECK(silent.count("QPIGS2") == 2 && silent.messages.size() >= 6);
  // a garbled answer is a miss too
  Bench garbled("pi30", true, 0);
  garbled.standard();
  garbled.answers["QPIGS2"] = bad_crc_reply("03.1 327.3 01026");
  garbled.run(40000);
  CHECK(garbled.count("QPIGS2") == 2 && garbled.messages.size() >= 7 && parse(garbled.messages[0]).get("pv2_w") == nullptr);
  // one good answer in between resets the count: an inverter that answers now and then is still asked
  Bench flaky("pi30", true, 0);
  flaky.standard();
  flaky.answers["QPIGS2"] = reply("03.1 327.3 01026");
  flaky.run(6000);
  flaky.answers["QPIGS2"] = reply("NAK");
  flaky.run(12000);
  flaky.answers["QPIGS2"] = reply("03.1 327.3 01026");
  flaky.run(40000);
  CHECK(flaky.count("QPIGS2") >= 6 && parse(flaky.messages.back()).get("pv2_w")->number == 1026);
}

static void test_parallel_units() {
  Bench b("pi30", false, 2);
  b.standard();
  b.answers["QPGS0"] = reply(kUnit0);
  b.answers["QPGS1"] = reply(kUnit1);
  b.run(12000);
  CHECK(b.asked.size() >= 10 && b.asked[3] == "QPGS0" && b.asked[4] == "QPGS1" && b.asked[5] == "QPIGS");
  CHECK(b.messages.size() >= 2);
  const json::Value doc = parse(b.messages[0]);
  const json::Value* units = doc.get("units");
  CHECK(units != nullptr && units->items.size() == 2);
  CHECK(units->items[0].get("unit")->number == 0 && units->items[0].get("mode")->text == "battery" && units->items[0].get("serial")->text == "92931701100510" && units->items[0].get("out_w")->number == 141);
  CHECK(units->items[1].get("unit")->number == 1 && units->items[1].get("mode")->text == "line" && units->items[1].get("fault_code")->text == "00" && units->items[1].get("grid_v")->number == 231.0);
  CHECK(doc.get("total_out_w")->number == 312 && doc.get("total_out_va")->number == 574 && doc.get("total_load_percent")->number == 3 && doc.get("total_charging_a")->number == 2);
  CHECK(doc.get("pv_w")->number == 856 && doc.get("out_w")->number == 119);          // the port's own inverter reads as before
  // a unit that is not there is skipped, the ones after it are still asked; a unit that does not answer costs its timeout and no reading
  Bench gap("pi30", false, 3);
  gap.standard();
  gap.answers["QPGS0"] = reply(kUnit0);
  gap.answers["QPGS1"] = reply("NAK");
  gap.answers["QPGS2"] = reply(kUnit1);
  gap.run(12000);
  const json::Value g = parse(gap.messages[0]);
  CHECK(gap.count("QPGS1") >= 1 && g.get("units")->items.size() == 2 && g.get("units")->items[1].get("unit")->number == 2);
  Bench dead("pi30", false, 2);
  dead.standard();
  dead.answers["QPGS0"] = reply(kUnit0);
  dead.run(20000);
  CHECK(dead.messages.size() >= 2 && parse(dead.messages[0]).get("units")->items.size() == 1);
  // nobody answers any unit: no list at all, and the reading is there
  Bench none("pi30", false, 2);
  none.standard();
  none.run(20000);
  CHECK(none.messages.size() >= 2 && parse(none.messages[0]).get("units") == nullptr);
  // both extras together, in the order pv2 then units
  Bench both("pi30", true, 1);
  both.standard();
  both.answers["QPIGS2"] = reply("03.1 327.3 01026");
  both.answers["QPGS0"] = reply(kUnit0);
  both.run(12000);
  CHECK(both.asked[3] == "QPIGS2" && both.asked[4] == "QPGS0" && both.asked[5] == "QPIGS");
  const json::Value d = parse(both.messages[0]);
  CHECK(d.get("pv2_w")->number == 1026 && d.get("units")->items.size() == 1 && d.get("pv_w")->number == 1882);
}

static void test_only_the_standard_dialect() {
  // the other dialects have other commands: nothing extra is asked
  Bench revo("revo", true, 2);
  revo.standard();
  revo.answers["QPIGS2"] = reply("03.1 327.3 01026");
  revo.answers["QPGS0"] = reply(kUnit0);
  revo.run(12000);
  CHECK(revo.count("QPIGS2") == 0 && revo.count("QPGS0") == 0);
  Bench pi18("pi18", true, 2);
  pi18.run(12000);
  CHECK(pi18.count("QPIGS2") == 0 && pi18.count("QPGS0") == 0);
  // while the dialect is still being looked for (auto), nothing extra either; once the standard one answers, the extras start
  Bench autod("auto", true, 0);
  autod.standard();
  autod.answers["QPIGS2"] = reply("03.1 327.3 01026");
  autod.run(20000);
  CHECK(autod.count("QPIGS2") >= 1 && parse(autod.messages.back()).get("pv2_w")->number == 1026);
  // the unit numbers are the inverter's own: at most ten
  Bench many("pi30", false, 99);
  many.standard();
  for (int i = 0; i < 10; ++i) many.answers["QPGS" + std::to_string(i)] = reply(kUnit0);
  many.run(15000);
  CHECK(many.count("QPGS9") >= 1 && many.count("QPGS10") == 0 && parse(many.messages[0]).get("units")->items.size() == 10);
}

// ---- the settings ---------------------------------------------------------------------------------------------------------------------------

static void test_settings() {
  config::Settings s = config::default_settings("a1b2c3");
  s.sta.enabled = true; s.sta.ssid = "casa"; s.sta.password = "una-clave-larga";
  CHECK(!s.ports[0].pv2 && s.ports[0].parallel == 0);
  s.ports[0] = {true, "voltronic", "axpert-1", 0, 16, 15, -1, 0, 0, "auto", false, 0, true, 2};
  CHECK(config::validate(s).empty());
  const std::string stored = config::to_json(s, true);
  config::Settings back;
  config::Problems problems;
  CHECK(config::load(stored, config::default_settings("000000"), back, problems) && back.ports[0].pv2 && back.ports[0].parallel == 2 && config::to_json(back, true) == stored);
  auto has = [&](const std::string& path, const std::string& code) { for (const config::Problem& p : config::validate(s)) if (p.path == path && p.code == code) return true; return false; };
  s.ports[0].parallel = 11;
  CHECK(!config::load("{\"ports\":[{\"parallel\":11}]}", back, back, problems = {}) && !problems.empty());
  s.ports[0].parallel = 2;
  s.ports[0].dialect = "revo";
  CHECK(has("ports.0.pv2", "invalid"));
  s.ports[0].dialect = "pi18"; s.ports[0].pv2 = false;
  CHECK(has("ports.0.parallel", "invalid"));
  s.ports[0].dialect = "auto"; s.ports[0].kind = "pylontech"; s.ports[0].baud = 0;
  CHECK(has("ports.0.pv2", "invalid") || has("ports.0.parallel", "invalid"));
}

int main() {
  test_qpigs2();
  test_qpgs();
  test_nothing_extra_by_default();
  test_second_pv_input();
  test_an_inverter_with_one_input();
  test_parallel_units();
  test_only_the_standard_dialect();
  test_settings();
  std::printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}

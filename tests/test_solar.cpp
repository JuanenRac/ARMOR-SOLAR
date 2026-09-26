// ARMOR-SOLAR - host tests: the Voltronic / MPP Solar protocol, the Pylontech console and frames, and the messages.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
// The replies below are typical ones of the public documents of these devices, written by hand; no real device has been read.
#include <cstdio>
#include <string>
#include <vector>

#include "../core/solar_json.hpp"

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

using namespace armor::solar;

static std::vector<std::uint8_t> reply(const std::string& text) {
  std::vector<std::uint8_t> frame{'('};
  frame.insert(frame.end(), text.begin(), text.end());
  const auto crc = voltronic::wire_crc(frame.data(), frame.size());
  frame.push_back(crc[0]); frame.push_back(crc[1]); frame.push_back('\r');
  return frame;
}

static void test_voltronic_commands() {
  std::vector<std::uint8_t> out;
  // the CRCs of the reading commands, as the protocol document lists them on the wire
  const struct { const char* command; std::uint8_t hi, lo; } known[] = {{"QPIGS", 0xB7, 0xA9}, {"QPIRI", 0xF8, 0x54}, {"QMOD", 0x49, 0xC1}, {"QPIWS", 0xB4, 0xDA}};
  for (const auto& k : known) {
    CHECK(voltronic::build_command(k.command, out));
    CHECK(out.size() == std::string(k.command).size() + 3 && out[out.size() - 3] == k.hi && out[out.size() - 2] == k.lo && out.back() == '\r');
  }
  // the standard check value of CRC-16/XMODEM
  const std::string nine = "123456789";
  CHECK(voltronic::crc16_xmodem(reinterpret_cast<const std::uint8_t*>(nine.data()), nine.size()) == 0x31C3);
  // only commands that read; anything odd is refused
  CHECK(!voltronic::build_command("PCP00", out) && !voltronic::build_command("POP02", out) && !voltronic::build_command("Q", out) && !voltronic::build_command("qpigs", out));
  CHECK(!voltronic::build_command("QPIGS\r", out) && !voltronic::build_command("QPIGS (", out) && !voltronic::build_command("QAAAAAAAAAAAA", out));
  // a byte of the CRC that would be a delimiter is moved by one
  bool moved = false;
  for (int a = 'A'; a <= 'Z'; ++a) for (int b = 'A'; b <= 'Z'; ++b) for (int c = 'A'; c <= 'Z'; ++c) {
    const std::uint8_t data[4] = {'Q', static_cast<std::uint8_t>(a), static_cast<std::uint8_t>(b), static_cast<std::uint8_t>(c)};
    const std::uint16_t raw = voltronic::crc16_xmodem(data, 4);
    const auto wire = voltronic::wire_crc(data, 4);
    for (int i = 0; i < 2; ++i) {
      const std::uint8_t r = i == 0 ? static_cast<std::uint8_t>(raw >> 8) : static_cast<std::uint8_t>(raw & 0xFF);
      if (r == 0x28 || r == 0x0D || r == 0x0A) { moved = true; if (wire[static_cast<std::size_t>(i)] != r + 1) CHECK(false); }
    }
    if (wire[0] == '(' || wire[0] == '\r' || wire[0] == '\n' || wire[1] == '(' || wire[1] == '\r' || wire[1] == '\n') CHECK(false);
  }
  CHECK(moved);
}

static void test_voltronic_replies() {
  const std::string qpigs = "000.0 00.0 230.0 50.0 0161 0119 003 460 57.50 012 100 0069 0014 103.8 57.45 00000 00110110 00 00 00856 010";
  std::string text;
  const std::vector<std::uint8_t> frame = reply(qpigs);
  CHECK(voltronic::parse_reply(frame.data(), frame.size(), text) && text == qpigs);
  voltronic::Status s;
  CHECK(voltronic::parse_qpigs(text, s));
  CHECK(s.grid_v == 0.0 && s.out_v == 230.0 && s.out_hz == 50.0 && s.out_va == 161 && s.out_w == 119 && s.load_percent == 3 && s.bus_v == 460);
  CHECK(s.battery_v == 57.5 && s.battery_charge_a == 12 && s.battery_percent == 100 && s.heatsink_c == 69 && s.pv_a == 14 && s.pv_v > 103.79 && s.pv_v < 103.81);
  CHECK(s.battery_discharge_a == 0 && s.battery_a() == 12.0);
  CHECK(s.pv_w == 856 && s.has_pv_w);
  // 00110110: b5 SCC firmware updated, b4 load on, b2 charging, b1 SCC charging
  CHECK(!s.sbu_priority && !s.config_changed && s.scc_updated && s.load_on && !s.battery_steady && s.charging && s.scc_charging && !s.ac_charging);
  // an older model with fewer fields: the PV power is the product
  voltronic::Status old;
  CHECK(voltronic::parse_qpigs("232.0 50.0 230.0 50.0 0000 0000 000 380 54.00 000 100 0035 00.0 000.0 00.00 00000 00010000", old));
  CHECK(!old.has_pv_w && old.pv_w == 0 && old.load_on && !old.charging);
  CHECK(!voltronic::parse_qpigs("", s) && !voltronic::parse_qpigs("NAK", s) && !voltronic::parse_qpigs("1 2 3", s));
  CHECK(!voltronic::parse_qpigs("000.0 00.0 230.0 50.0 0161 0119 003 460 5x.50 012 100 0069 0014 103.8 57.45 00000 00110110", s));
  CHECK(!voltronic::parse_qpigs("000.0 00.0 230.0 50.0 0161 0119 003 460 57.50 012 100 0069 0014 103.8 57.45 00000 0011011", s));   // 7 status bits
  // the mode and the warnings
  char mode = 0;
  CHECK(voltronic::parse_qmod("B", mode) && mode == 'B' && std::string(voltronic::mode_name(mode)) == "battery");
  CHECK(voltronic::parse_qmod("L", mode) && std::string(voltronic::mode_name(mode)) == "line");
  CHECK(!voltronic::parse_qmod("", mode) && !voltronic::parse_qmod("BB", mode) && !voltronic::parse_qmod("Z", mode));
  std::vector<std::string> warnings;
  CHECK(voltronic::parse_qpiws("00000000000000000000000000000000", warnings) && warnings.empty());
  CHECK(voltronic::parse_qpiws("00000100000010000000000000000000", warnings) && warnings.size() == 2 && warnings[0] == "line_fail" && warnings[1] == "battery_low");
  CHECK(voltronic::parse_qpiws("0000010", warnings) && warnings.size() == 1);   // a short reply: the rest is off
  CHECK(!voltronic::parse_qpiws("0000x", warnings) && !voltronic::parse_qpiws("", warnings));
  // the ratings
  voltronic::Ratings r;
  CHECK(voltronic::parse_qpiri("230.0 21.7 230.0 50.0 21.7 5000 5000 48.0 46.0 42.0 56.4 54.0 0 30 060 0 1 1 01 01 1 0 54.0 0 1", r));
  CHECK(r.out_rating_w == 5000 && r.battery_rating_v == 48.0 && r.battery_bulk_v == 56.4 && r.battery_float_v == 54.0 && r.battery_type == 0);
  CHECK(r.max_ac_charge_a == 30 && r.max_charge_a == 60 && r.input_range == 0 && r.output_priority == 1 && r.charger_priority == 1);
  CHECK(!voltronic::parse_qpiri("230.0 21.7", r));
}

static void test_voltronic_frames() {
  const std::vector<std::uint8_t> good = reply("B");
  std::string text;
  CHECK(voltronic::parse_reply(good.data(), good.size(), text) && text == "B");
  std::vector<std::uint8_t> bad = good;
  bad[1] = 'L';
  CHECK(!voltronic::parse_reply(bad.data(), bad.size(), text));
  CHECK(!voltronic::parse_reply(good.data(), good.size() - 1, text));
  const std::uint8_t nak[] = {'(', 'N', 'A', 'K', 0x73, 0x73, '\r'};
  CHECK(voltronic::parse_reply(nak, sizeof nak, text) && text == "NAK");   // the refusal of a command has this well-known CRC
  const std::uint8_t wrong[] = {'(', 'N', 'A', 'K', 0x73, 0x74, '\r'};
  CHECK(!voltronic::parse_reply(wrong, sizeof wrong, text));               // and one with a wrong CRC is not accepted
  // the framer finds a reply in noise and restarts on a '('
  voltronic::ReplyFramer framer;
  std::vector<std::uint8_t> frame;
  int found = 0;
  const std::vector<std::uint8_t> noise{0x00, 0xFF, 'x', '\r'};
  for (std::uint8_t b : noise) CHECK(!framer.feed(b, frame));
  std::vector<std::uint8_t> stream{'(', 'B', 'B'};
  stream.insert(stream.end(), good.begin(), good.end());
  for (std::uint8_t b : stream) if (framer.feed(b, frame)) { ++found; CHECK(frame == good); }
  CHECK(found == 1);
  // a runaway frame is dropped
  for (int i = 0; i < 400; ++i) framer.feed('a', frame);
  CHECK(!framer.feed('\r', frame));
}

static const char* kPwr =
    "@\r\n"
    "Power Volt   Curr   Tempr  Tlow   Thigh  Vlow   Vhigh  Base.St  Volt.St  Curr.St  Temp.St  Coulomb  Time                 B.V.St   B.T.St\r\n"
    "1     49872  -1280  22000  20000  25000  3330   3348   Dischg   Normal   Normal   Normal   88%      2018-11-14 15:12:04  Normal   Normal\r\n"
    "2     49870  -1310  21500  19500  24500  3328   3349   Dischg   Normal   Normal   Normal   87%      2018-11-14 15:12:04  Normal   Normal\r\n"
    "3     -      -      -      -      -      -      -      Absent   -        -        -        -        -                    -        -\r\n"
    "Command completed successfully\r\n"
    "$$\r\npylon>";

static void test_pylontech_console() {
  std::vector<pylontech::Module> modules;
  CHECK(pylontech::parse_pwr(kPwr, modules) == 3);
  CHECK(modules[0].present && modules[0].number == 1 && modules[0].voltage_v == 49.872 && modules[0].current_a == -1.28 && modules[0].temperature_c == 22.0);
  CHECK(modules[0].temperature_low_c == 20.0 && modules[0].temperature_high_c == 25.0 && modules[0].cell_low_v == 3.33 && modules[0].cell_high_v == 3.348);
  CHECK(modules[0].soc_percent == 88 && modules[0].base_state == "Dischg" && modules[0].voltage_state == "Normal");
  CHECK(modules[1].soc_percent == 87 && modules[1].current_a == -1.31);
  CHECK(!modules[2].present && modules[2].number == 3 && modules[2].base_state == "Absent");
  const pylontech::Stack s = pylontech::summarise(modules);
  CHECK(s.modules == 2 && s.voltage_v > 49.870 && s.voltage_v < 49.872 && s.current_a < -2.58 && s.current_a > -2.60);
  CHECK(s.temperature_min_c == 19.5 && s.temperature_max_c == 25.0 && s.cell_low_v == 3.328 && s.cell_high_v == 3.349);
  CHECK(s.soc_percent == 88 && s.state == "discharging" && !s.alarm);   // the mean of 88 and 87 is 87.5, which rounds up
  // one module reporting a state that is not normal is an alarm
  std::string alarm = kPwr;
  alarm.replace(alarm.find("Normal   Normal   Normal   88%"), 8, "OverVol ");
  CHECK(pylontech::parse_pwr(alarm, modules) == 3 && pylontech::summarise(modules).alarm);
  // nothing useful: no modules, and no crash on odd text
  CHECK(pylontech::parse_pwr("", modules) == 0 && pylontech::parse_pwr("Command completed successfully\r\n$$\r\npylon>", modules) == 0);
  CHECK(pylontech::parse_pwr("1 abc def 3 4 5 6 7 Idle Normal Normal Normal 50%", modules) == 0);
  CHECK(pylontech::parse_pwr("99 1 2 3 4 5 6 7 Idle Normal Normal Normal 50%", modules) == 0);   // not a module number
  CHECK(pylontech::parse_pwr("1 50000 0 20000 20000 20000 3330 3330 Idle Normal Normal Normal 250%", modules) == 1 && modules[0].soc_percent == -1);
  CHECK(pylontech::summarise({}).modules == 0 && pylontech::summarise({}).soc_percent == -1);
}

static void test_pylontech_frames() {
  // the two checks of a frame, worked by hand: a length of 2 has a check of E, and the frame's checksum is the two's complement of the sum of its characters
  CHECK(pylontech::length_field(2) == "E002" && pylontech::length_field(0) == "0000" && pylontech::length_field(0x2A) == "402A");
  const std::string frame = pylontech::build_frame(0x20, 0x02, 0x46, 0x42, "02");
  CHECK(frame.size() == 1 + 14 + 4 + 1 && frame[0] == '~' && frame.back() == '\r' && frame.substr(1, 14) == "20024642E00202");
  unsigned sum = 0;
  for (char c : frame.substr(1, 14)) sum += static_cast<unsigned char>(c);
  CHECK(frame.substr(15, 4) == pylontech::hex((~sum + 1) & 0xFFFF, 4));
  pylontech::Frame parsed;
  CHECK(pylontech::parse_frame(frame, parsed) && parsed.version == 0x20 && parsed.address == 2 && parsed.cid1 == 0x46 && parsed.cid2 == 0x42 && parsed.info == "02");
  const std::string with_info = pylontech::build_frame(0x20, 0x01, 0x46, 0x00, "0102030405AB");
  CHECK(pylontech::parse_frame(with_info, parsed) && parsed.info == "0102030405AB");
  CHECK(pylontech::parse_frame(pylontech::build_frame(0x20, 0x01, 0x46, 0x00, ""), parsed) && parsed.info.empty());
  // damage is refused: a changed character, a changed length, a missing end, lowercase, a wrong start
  std::string damaged = frame;
  damaged[6] = '7';
  CHECK(!pylontech::parse_frame(damaged, parsed));
  std::string wrong_length = frame;
  wrong_length[9] = 'F';
  CHECK(!pylontech::parse_frame(wrong_length, parsed));
  CHECK(!pylontech::parse_frame(frame.substr(0, frame.size() - 1), parsed) && !pylontech::parse_frame("^" + frame.substr(1), parsed) && !pylontech::parse_frame("", parsed));
  std::string lower = with_info;
  lower[13] = 'a';
  CHECK(!pylontech::parse_frame(lower, parsed));
}

static const char* kBat =
    "@\r\n"
    "Battery  Volt     Curr     Tempr    Base State   Volt. State  Curr. State  Temp. State  Coulomb\r\n"
    "0        3324     -1281    22000    Dischg       Normal       Normal       Normal       88%\r\n"
    "1        3325     -1281    22000    Dischg       Normal       Normal       Normal       88%\r\n"
    "2        3323     -1281    22000    Dischg       Normal       Normal       Normal       88%\r\n"
    "3        3330     -1281    22000    Dischg       Normal       Normal       Normal       88%\r\n"
    "4        3329     -1281    22000    Dischg       Normal       Normal       Normal       88%\r\n"
    "5        3326     -1281    22500    Dischg       Normal       Normal       Normal       88%\r\n"
    "6        3327     -1281    22500    Dischg       Normal       Normal       Normal       88%\r\n"
    "7        3325     -1281    22500    Dischg       Normal       Normal       Normal       88%\r\n"
    "8        3324     -1281    22500    Dischg       Normal       Normal       Normal       88%\r\n"
    "9        3348     -1281    22500    Dischg       Normal       Normal       Normal       88%\r\n"
    "10       3326     -1281    23000    Dischg       Normal       Normal       Normal       88%\r\n"
    "11       3325     -1281    23000    Dischg       Normal       Normal       Normal       88%\r\n"
    "12       3324     -1281    23000    Dischg       Normal       Normal       Normal       88%\r\n"
    "13       3327     -1281    23000    Dischg       Normal       Normal       Normal       88%\r\n"
    "14       3326     -1281    23000    Dischg       Normal       Normal       Normal       88%\r\n"
    "Command completed successfully\r\n$$\r\npylon>";

static const char* kInfo =
    "@\r\n"
    "Device address      : 1\r\n"
    "Manufacturer        : Pylon\r\n"
    "Device name         : US3000C\r\n"
    "Board version       : PHILTEC_BOARD_V2\r\n"
    "Remain Capacity     : 65100 mAH\r\n"
    "Total Capacity      : 74000 mAH\r\n"
    "Cycle Times         : 312\r\n"
    "Command completed successfully\r\n$$\r\npylon>";

static void test_pylontech_cells_and_capacity() {
  std::vector<double> cells;
  CHECK(pylontech::parse_bat(kBat, cells) == 15);
  CHECK(cells[0] == 3.324 && cells[9] == 3.348 && cells[14] == 3.326);
  // a table with a gap, a cell out of range or nothing at all gives no cells
  CHECK(pylontech::parse_bat("0 3324 0 0 A B C D 1%\r\n2 3324 0 0 A B C D 1%\r\n", cells) == 0);
  CHECK(pylontech::parse_bat("0 99999 0 0 A B C D 1%\r\n", cells) == 0);
  CHECK(pylontech::parse_bat("", cells) == 0 && pylontech::parse_bat("Command completed successfully", cells) == 0);
  std::vector<pylontech::Module> modules;
  pylontech::parse_pwr(kPwr, modules);
  pylontech::parse_bat(kBat, cells);
  CHECK(pylontech::attach_cells(modules, 1, cells) && modules[0].cells_v.size() == 15);
  CHECK(!pylontech::attach_cells(modules, 3, cells) && !pylontech::attach_cells(modules, 9, cells));   // an absent module and one that is not there
  // the console's `info`: the model, the remaining and the total capacity, the cycles
  CHECK(pylontech::parse_info(kInfo, modules[0]) == 4);
  CHECK(modules[0].model == "US3000C" && modules[0].capacity_ah == 65.1 && modules[0].full_capacity_ah == 74.0 && modules[0].cycles == 312);
  pylontech::Module none;
  CHECK(pylontech::parse_info("", none) == 0 && pylontech::parse_info("Remain Capacity : abc mAH\r\nCycle Times : -4\r\n", none) == 0 && none.capacity_ah < 0 && none.cycles < 0);
  pylontech::Module ah;
  CHECK(pylontech::parse_info("Remain Capacity : 50 Ah\r\n", ah) == 1 && ah.capacity_ah == 50.0);
  // the second module has its own numbers; the stack adds the capacities (the modules are in parallel), takes the most used cycle count and the cells' extremes
  pylontech::parse_info("Device name : US3000C\r\nRemain Capacity : 60000 mAH\r\nTotal Capacity : 74000 mAH\r\nCycle Times : 340\r\n", modules[1]);
  const pylontech::Stack s = pylontech::summarise(modules);
  CHECK(s.capacity_ah > 125.09 && s.capacity_ah < 125.11 && s.full_capacity_ah == 148.0 && s.cycles == 340 && s.model == "US3000C");
  CHECK(s.energy_kwh > 6.2 && s.energy_kwh < 6.3);                     // 125.1 Ah at about 49.87 V
  CHECK(s.cell_low_v == 3.323 && s.cell_high_v == 3.349);              // the cells' own lowest of module 1 (3.323) and the second module's Vhigh column (3.349)
  const std::string json_text = battery_json("solar-1", "us3000-1", 5, modules);
  armor::json::Value doc;
  CHECK(armor::json::parse(json_text, doc) && doc.get("model")->text == "US3000C" && doc.get("cycles")->number == 340 && doc.get("full_capacity_ah")->number == 148.0);
  const armor::json::Value* stack = doc.get("stack");
  CHECK(stack != nullptr && stack->items.size() == 3 && stack->items[0].get("cells_v")->items.size() == 15 && stack->items[0].get("cycles")->number == 312);
  CHECK(stack->items[1].get("cells_v") == nullptr && stack->items[2].get("cells_v") == nullptr);
}

static void test_messages() {
  CHECK(topic("perimetro-1", "axpert-1") == "armor/solar/perimetro-1/axpert-1/state");
  CHECK(topic("Bad", "x").empty() && topic("a", "").empty() && topic("a", "b/c").empty() && topic("-a", "b").empty() && topic("a", std::string(33, 'x')).empty());
  voltronic::Status s;
  CHECK(voltronic::parse_qpigs("232.0 50.0 230.0 50.0 0161 0119 003 460 57.50 012 100 0069 0014 103.8 57.45 00000 00110110 00 00 00856", s));
  const std::string inverter = inverter_json("solar-1", "axpert-1", 123456, 'L', s, {"line_fail"});
  armor::json::Value document;
  CHECK(armor::json::parse(inverter, document) && document.is_object());
  CHECK(document.get("kind")->text == "inverter" && document.get("mode")->text == "line" && document.get("device")->text == "axpert-1");
  CHECK(document.get("pv_w")->number == 856 && document.get("battery_a")->number == 12.0 && document.get("warnings")->items.size() == 1);
  CHECK(document.get("ac_charging")->boolean == false && document.get("pv_charging")->boolean == true);
  CHECK(armor::json::parse(inverter_json("solar-1", "axpert-1", 1, 0, s, {}), document) && document.get("mode")->text == "unknown" && document.get("warnings")->items.empty());
  std::vector<pylontech::Module> modules;
  pylontech::parse_pwr(kPwr, modules);
  const std::string battery = battery_json("solar-1", "us3000-1", 99, modules);
  CHECK(armor::json::parse(battery, document) && document.get("kind")->text == "battery" && document.get("modules")->number == 2 && document.get("soc_percent")->number == 88);
  CHECK(document.get("state")->text == "discharging" && document.get("stack")->items.size() == 3 && document.get("alarm")->boolean == false);
  CHECK(armor::json::parse(battery_json("solar-1", "us3000-1", 99, {}), document) && document.get("modules")->number == 0 && document.get("state") == nullptr);
}

int main() {
  test_voltronic_commands();
  test_voltronic_replies();
  test_voltronic_frames();
  test_pylontech_console();
  test_pylontech_frames();
  test_pylontech_cells_and_capacity();
  test_messages();
  std::printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}

// ARMOR-SOLAR - host tests of the node: its settings, its network plan, the emulated UART and what a port does with the equipment on it.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
// The equipment is a stand-in written from the public documents of the protocols; no real inverter or battery has been read.
#include <cstdio>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "../core/ant_bms.hpp"
#include "../core/ant_settings.hpp"
#include "../core/auth.hpp"
#include "../core/netplan.hpp"
#include "../core/poller.hpp"
#include "../core/soft_uart.hpp"
#include "../core/solar_config.hpp"
#include "../core/web_policy.hpp"
#include "ant_frames.hpp"
#include "ant_settings_frames.hpp"

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

static config::Settings valid_settings() {
  config::Settings s = config::default_settings("a1b2c3");
  s.sta.enabled = true;
  s.sta.ssid = "casa";
  s.sta.password = "una-clave-larga";
  return s;
}
static bool has(const config::Problems& problems, const std::string& path, const std::string& code) {
  for (const config::Problem& p : problems) if (p.path == path && p.code == code) return true;
  return false;
}

// ---- the settings --------------------------------------------------------------------------------------------------------------------

static void test_defaults_and_pins() {
  const config::Settings s = config::default_settings("a1b2c3");
  CHECK(s.node_id == "solar-a1b2c3" && s.ports.size() == 10 && !s.ports[0].enabled);
  CHECK(s.ports[0].rx == 16 && s.ports[0].tx == 15 && s.ports[0].de == 7 && s.ports[6].rx == 21 && s.ports[6].tx == 47 && s.ports[3].de == -1);
  CHECK(s.ports[9].rx == 42 && s.ports[9].tx == 1 && board::kHardwarePorts + board::kSoftPorts == board::kPortCount && board::kSoftPorts == 7);
  CHECK(!board::kHasEthernet && std::string(board::kId) == "s3-wifi" && s.uplink == config::Uplink::kWifi && s.ip.dhcp);
  // a node with no Wi-Fi at all could not be reached: the defaults are not valid until one of the two is on
  CHECK(has(config::validate(s), "sta.enabled", "required"));
  CHECK(config::validate(valid_settings()).empty());
  // the pins the board keeps for itself are never offered
  for (int gpio : {26, 30, 33, 35, 37, 19, 20, 22, 49, -1}) CHECK(!board::assignable(gpio));
  for (int gpio : {0, 3, 43, 44, 45, 46, 48, 1, 16, 47}) CHECK(board::assignable(gpio));
  CHECK(std::string(board::pin_info(45).note) == "strapping" && std::string(board::pin_info(48).note) == "led" && std::string(board::pin_info(43).note) == "usb_serial");
}

static void test_ports() {
  config::Settings s = valid_settings();
  s.ports[0] = {true, "voltronic", "axpert-1", 0, 16, 15, -1, 0, 0};
  s.ports[1] = {true, "pylontech", "us3000-1", 0, 18, 17, 8, 0, 0};
  s.ports[3] = {true, "voltronic", "axpert-2", 2400, 9, 10, -1, 0, 0};
  s.ports[4] = {true, "raw", "sniffer", 9600, 11, -1, -1, 0, 0};
  CHECK(config::validate(s).empty());
  CHECK(config::effective_baud(s.ports[0]) == 2400 && config::effective_baud(s.ports[1]) == 115200 && config::effective_poll_s(s.ports[0]) == 5 && config::effective_poll_s(s.ports[1]) == 10);
  // an emulated port only keeps slow lines: the Pylontech console at 115200 needs a hardware one
  s.ports[5] = {true, "pylontech", "us3000-2", 0, 13, 14, -1, 0, 0};
  CHECK(has(config::validate(s), "ports.5.baud", "too_fast_for_emulated"));
  s.ports[5].baud = 9600;
  CHECK(config::validate(s).empty());
  // two ports cannot share a pin nor a name; a reserved pin, a missing pin and a bad name are refused
  s.ports[5].rx = 16;
  CHECK(has(config::validate(s), "ports.5.rx", "conflict"));
  s.ports[5].rx = 13; s.ports[5].name = "axpert-1";
  CHECK(has(config::validate(s), "ports.5.name", "conflict"));
  s.ports[5].name = "Bad Name";
  CHECK(has(config::validate(s), "ports.5.name", "invalid"));
  s.ports[5].name = "us3000-2"; s.ports[5].tx = 33;
  CHECK(has(config::validate(s), "ports.5.tx", "reserved"));
  s.ports[5].tx = -1;
  CHECK(has(config::validate(s), "ports.5.tx", "required"));            // a port that asks needs a way to ask
  s.ports[5].tx = 14; s.ports[5].rx = -1;
  CHECK(has(config::validate(s), "ports.5.rx", "required"));
  s.ports[5].rx = 13; s.ports[4].kind = "modbus";
  CHECK(has(config::validate(s), "ports.4.kind", "invalid"));
  s.ports[4].kind = "raw"; s.ports[4].poll_s = 1;
  CHECK(has(config::validate(s), "ports.4.poll_s", "range"));
  // a disabled port is not checked at all
  s.ports[4].enabled = false; s.ports[5].enabled = false;
  CHECK(config::validate(s).empty());
  // an ANT-BMS reads at 19200 baud: fine on an emulated port too
  s.ports[6] = {true, "ant", "ant-7", 0, 21, 47, -1, 0, 0};
  CHECK(config::validate(s).empty() && config::effective_baud(s.ports[6]) == 19200 && config::effective_poll_s(s.ports[6]) == 5);
  s.ports[6].baud = 38400;
  CHECK(has(config::validate(s), "ports.6.baud", "too_fast_for_emulated"));
}

static void test_wifi_board_has_no_ethernet() {
  config::Settings s = valid_settings();
  s.uplink = config::Uplink::kEthernet;
  CHECK(has(config::validate(s), "uplink", "not_available"));
  netplan::Plan plan = netplan::plan_network(s, false, "", "a1b2c3", 5);
  CHECK(!plan.wired && plan.layout == netplan::Layout::kStation);   // a stored "ethernet" on a board without a cable still comes up on Wi-Fi
  config::Settings back;
  config::Problems problems;
  CHECK(!config::load("{\"uplink\":\"ethernet\"}", valid_settings(), back, problems) && has(problems, "uplink", "not_available"));
  CHECK(config::to_json(valid_settings(), false).find("\"uplink\":\"wifi\"") != std::string::npos);
  // the pins of the two boards differ where the W5500 sits: on this board GPIO 9 to 14 and 8 are free for a port
  for (int gpio = 8; gpio <= 14; ++gpio) CHECK(board::assignable(gpio));
}

static void test_settings_document() {
  const config::Settings s = valid_settings();
  const std::string stored = config::to_json(s, true), shown = config::to_json(s, false);
  CHECK(stored.find("una-clave-larga") != std::string::npos && shown.find("una-clave-larga") == std::string::npos && shown.find("\"password_set\":true") != std::string::npos);
  config::Settings back;
  config::Problems problems;
  CHECK(config::load(stored, config::default_settings("000000"), back, problems) && back.sta.password == "una-clave-larga" && back.node_id == s.node_id);
  // the panel sends a section without a password: the stored one stays; "_clear" erases it
  config::Settings edited;
  CHECK(config::load("{\"sta\":{\"enabled\":true,\"ssid\":\"otra\"}}", s, edited, problems) && edited.sta.ssid == "otra" && edited.sta.password == "una-clave-larga");
  CHECK(config::load("{\"sta\":{\"password_clear\":true}}", s, edited, problems) && edited.sta.password.empty());
  problems.clear();
  CHECK(!config::load("{\"ports\":[{\"baud\":\"fast\"}]}", s, edited, problems) && has(problems, "ports.0.baud", "invalid"));
  problems.clear();
  CHECK(!config::load("{\"ports\":[{\"rx\":99}]}", s, edited, problems) && has(problems, "ports.0.rx", "range"));
  problems.clear();
  CHECK(!config::load("not json", s, edited, problems) && has(problems, "", "not_json"));
  problems.clear();
  CHECK(!config::load("{\"mqtt\":{\"enabled\":true,\"uri\":\"http://x\"}}", s, edited, problems) && has(problems, "mqtt.uri", "invalid"));
  problems.clear();
  CHECK(config::load("{\"mqtt\":{\"enabled\":true,\"uri\":\"mqtt://192.168.0.180:18883\",\"username\":\"solar-1\",\"password\":\"x\"},\"web\":{\"mode\":\"https\"}}", s, edited, problems) && edited.mqtt.enabled && edited.web == config::WebMode::kHttps);
  problems.clear();
  CHECK(config::load("{\"system\":{\"auto_restart_hours\":12}}", s, edited, problems) && edited.auto_restart_hours == 12);
  problems.clear();
  CHECK(!config::load("{\"system\":{\"auto_restart_hours\":5}}", s, edited, problems) && has(problems, "system.auto_restart_hours", "invalid"));
}

// ---- the network -------------------------------------------------------------------------------------------------------------------------

static void test_network_plan() {
  config::Settings s = valid_settings();
  netplan::Plan plan = netplan::plan_network(s, true, "CODE1234", "a1b2c3", 5);
  CHECK(plan.layout == netplan::Layout::kSetupAp && plan.ap.setup && plan.ap.ssid == "ARMOR-SETUP-A1B2C3" && plan.ap.password == "CODE1234" && !plan.station);
  plan = netplan::plan_network(s, false, "", "a1b2c3", 5);
  CHECK(plan.layout == netplan::Layout::kStation && plan.station && !plan.ap.enabled && plan.hostname == "armor-solar-a1b2c3");
  s.ap.enabled = true; s.ap.password = "otra-clave-larga"; s.ap.ssid = "ARMOR-SOLAR-A1B2C3";
  plan = netplan::plan_network(s, false, "", "a1b2c3", 5);
  CHECK(plan.layout == netplan::Layout::kStationWithAp && plan.ap.channel == 11);     // 5 % 3 = 2 -> channel 11
  s.sta.enabled = false;
  plan = netplan::plan_network(s, false, "", "a1b2c3", 3);
  CHECK(plan.layout == netplan::Layout::kAccessPoint && plan.ap.channel == 1);
  s.hostname = "casa-solar";
  CHECK(netplan::hostname_for(s) == "casa-solar");
  CHECK(webpolicy::serves_https(config::WebMode::kBoth) && !webpolicy::serves_https(config::WebMode::kHttp) && webpolicy::http_redirects(config::WebMode::kHttps));
}

// ---- the emulated UART -------------------------------------------------------------------------------------------------------------------

// The edges of a byte stream on a line, from a start time, with each edge moved by up to `jitter_us` (a pseudo-random but repeatable amount).
static void wire(softuart::Receiver& rx, const std::vector<std::uint8_t>& bytes, unsigned baud, std::uint64_t start_us, int jitter_us, std::uint64_t& end_us, int gap_bits = 0) {
  const double bit = 1000000.0 / baud;
  std::uint64_t seed = 12345;
  const auto jitter = [&]() { seed = seed * 6364136223846793005ULL + 1442695040888963407ULL; return jitter_us == 0 ? 0 : static_cast<int>((seed >> 33) % (2 * jitter_us + 1)) - jitter_us; };
  double t = static_cast<double>(start_us);
  bool level = true;
  for (const std::uint8_t byte : bytes) {
    const auto bits = softuart::frame_bits(byte);
    for (const bool b : bits) {
      if (b != level) { level = b; rx.edge(static_cast<std::uint64_t>(t + jitter()), level); }
      t += bit;
    }
    t += gap_bits * bit;
  }
  end_us = static_cast<std::uint64_t>(t);
}

static void test_soft_uart() {
  const auto bits = softuart::frame_bits(0xA5);       // 1010 0101: least significant bit first
  CHECK(!bits[0] && bits[1] && !bits[2] && bits[3] && !bits[4] && !bits[5] && bits[6] && !bits[7] && bits[8] && bits[9]);
  CHECK(softuart::message_bits(reinterpret_cast<const std::uint8_t*>("ab"), 2).size() == 20);
  for (const unsigned baud : {1200u, 2400u, 9600u, 19200u}) {
    softuart::Receiver rx(baud);
    const std::vector<std::uint8_t> text{'Q', 'P', 'I', 'G', 'S', 0xB7, 0xA9, '\r', 0x00, 0xFF, 0x55, 0x28};
    std::uint64_t end = 0;
    wire(rx, text, baud, 1000, static_cast<int>(1000000 / baud / 12), end);      // each edge off by up to a twelfth of a bit
    std::vector<std::uint8_t> got;
    rx.poll(end + 100000, got);
    CHECK(got == text);
    CHECK(rx.framing_errors() == 0);
  }
  // bytes with gaps between them, and a poll in the middle of the stream: what is not complete waits
  softuart::Receiver rx(2400);
  std::uint64_t end = 0;
  wire(rx, {'H', 'i'}, 2400, 500, 0, end, 7);
  std::vector<std::uint8_t> got;
  rx.poll(500 + 5 * 417, got);
  CHECK(got.empty());
  rx.poll(end + 10000, got);
  CHECK(got.size() == 2 && got[0] == 'H' && got[1] == 'i');
  // a line whose stop bit is low (a wrong speed, or noise) is a framing error, not a byte
  softuart::Receiver bad(9600);
  bad.edge(1000, false);        // a start bit that stays low for the whole frame
  std::vector<std::uint8_t> none;
  bad.poll(1000 + 20000, none);
  CHECK(none.empty() && bad.framing_errors() == 1);
}

// ---- the stand-in equipment ------------------------------------------------------------------------------------------------------------

static std::vector<std::uint8_t> reply(const std::string& text) {
  std::vector<std::uint8_t> frame{'('};
  frame.insert(frame.end(), text.begin(), text.end());
  const auto crc = voltronic::wire_crc(frame.data(), frame.size());
  frame.push_back(crc[0]); frame.push_back(crc[1]); frame.push_back('\r');
  return frame;
}
static std::string as_text(const std::vector<std::uint8_t>& bytes) { return std::string(bytes.begin(), bytes.end()); }

// A stand-in that answers what a port sends, after a latency, and is scripted by a map from the command to the reply bytes (absent: silence).
struct Bench {
  Poller poller;
  std::map<std::string, std::vector<std::uint8_t>> answers;
  std::vector<std::string> asked;
  std::uint64_t now = 0;
  std::vector<std::string> messages;
  std::vector<std::string> topics;
  Bench(config::Kind kind, int poll_s, int modules = 0, const std::string& dialect = "auto") : poller(kind, "solar-1", kind == config::Kind::kVoltronic ? "axpert-1" : "us3000-1", poll_s, modules, dialect) {}
  void run(std::uint64_t until_ms, std::uint64_t latency_ms = 40) {
    std::vector<std::pair<std::uint64_t, std::vector<std::uint8_t>>> in_flight;
    for (; now < until_ms; now += 10) {
      for (auto it = in_flight.begin(); it != in_flight.end();) {
        if (it->first <= now) { poller.on_rx(it->second.data(), it->second.size(), now); it = in_flight.erase(it); } else ++it;
      }
      const std::vector<std::uint8_t> out = poller.next_tx(now);
      if (!out.empty()) {
        std::string command = as_text(out);
        if (command.size() > 3 && command[0] == 'Q') command = command.substr(0, command.size() - 3);   // the CRC and CR are not part of the name
        else if (command.size() > 8 && command[0] == '^') command = "^" + command.substr(5, command.size() - 8);   // "^P005GS" + CRC + CR is "^GS"
        asked.push_back(command);
        const auto answer = answers.find(command);
        if (answer != answers.end()) in_flight.push_back({now + latency_ms, answer->second});
      }
      std::string topic, payload;
      if (poller.take_message(1700000000000ULL + now, topic, payload)) { topics.push_back(topic); messages.push_back(payload); }
    }
  }
};

static const char* kQpigs = "232.0 50.0 230.0 50.0 0161 0119 003 460 57.50 012 100 0069 0014 103.8 57.45 00000 00110110 00 00 00856";
static const char* kPwr =
    "@\r\nPower Volt   Curr   Tempr  Tlow   Thigh  Vlow   Vhigh  Base.St  Volt.St  Curr.St  Temp.St  Coulomb  Time                 B.V.St   B.T.St\r\n"
    "1     49872  -1280  22000  20000  25000  3330   3348   Dischg   Normal   Normal   Normal   88%      2018-11-14 15:12:04  Normal   Normal\r\n"
    "2     49870  -1310  21500  19500  24500  3328   3349   Dischg   Normal   Normal   Normal   87%      2018-11-14 15:12:04  Normal   Normal\r\n"
    "3     -      -      -      -      -      -      -      Absent   -        -        -        -        -                    -        -\r\n"
    "Command completed successfully\r\n$$\r\npylon>";
static std::string bat_table() {
  std::string t = "@\r\nBattery  Volt     Curr     Tempr    Base State   Volt. State  Curr. State  Temp. State  Coulomb\r\n";
  for (int i = 0; i < 15; ++i) t += std::to_string(i) + " " + std::to_string(3324 + (i * 7) % 25) + " -1281 22000 Dischg Normal Normal Normal 88%\r\n";
  return t + "Command completed successfully\r\n$$\r\npylon>";
}
static const char* kPwr5000 =
    "@\r\n"
    "Power Volt   Curr   Tempr  Tlow   Tlow.Id  Thigh  Thigh.Id  Vlow   Vlow.Id  Vhigh  Vhigh.Id  Base.St  Volt.St  Curr.St  Temp.St  Coulomb  Time                 B.V.St   B.T.St   MosTempr  M.T.St  SysAlarm.St\r\n"
    "1     50200  -1000  21000  20000  4        22000  9         3340   3        3350   11        Dischg   Normal   Normal   Normal   88%      2023-05-06 10:11:12  Normal   Normal   23000     Normal  Normal\r\n"
    "Command completed successfully\r\n$$\r\npylon>";
static std::string bat_table_mah() {
  std::string t = "@\r\nBattery  Volt     Curr     Tempr    Base State   Volt. State  Curr. State  Temp. State  SOC      Coulomb      BAL\r\n";
  for (int i = 0; i < 15; ++i) t += std::to_string(i) + " " + std::to_string(3340 + i) + " -1000 22000 Dischg Normal Normal Normal 88% 66550 mAH " + (i == 4 ? "Y" : "N") + "\r\n";
  return t + "Command completed successfully\r\n$$\r\npylon>";
}
static const char* kInfoReal =
    "@\r\nDevice address      : 1\r\nManufacturer        : Pylon\r\nDevice name         : US5000\r\nBoard version       : PHILTEC_BOARD_V2\r\nMain Soft version   : B66.6\r\n"
    "Specification       : 48V/74AH\r\nCell Number         : 15\r\nMax Dischg Curr     : -100000mA\r\nMax Charge Curr     : 100000mA\r\nCommand completed successfully\r\n$$\r\npylon>";
static const char* kStat = "@\r\nDevice address      : 1\r\nData Items          : 100\r\nCHARGE Cnt.         : 400\r\nCYCLE Times         : 312\r\nPwr Coulomb         : 133200000\r\nCommand completed successfully\r\n$$\r\npylon>";
static const char* kInfo = "@\r\nDevice address      : 1\r\nDevice name         : US3000C\r\nRemain Capacity     : 65100 mAH\r\nTotal Capacity      : 74000 mAH\r\nCycle Times         : 312\r\nCommand completed successfully\r\n$$\r\npylon>";

static std::vector<std::uint8_t> bytes_of(const std::string& text) { return std::vector<std::uint8_t>(text.begin(), text.end()); }

static void test_voltronic_port() {
  Bench b(config::Kind::kVoltronic, 5);
  b.answers["QPIGS"] = reply(kQpigs);
  b.answers["QMOD"] = reply("L");
  b.answers["QPIWS"] = reply("00000100000000000000000000000000");     // bit 5: line_fail
  b.run(12000);
  // it asks in order, waits for each answer, and again at every poll (5 s)
  CHECK(b.asked.size() >= 6 && b.asked[0] == "QPIGS" && b.asked[1] == "QMOD" && b.asked[2] == "QPIWS" && b.asked[3] == "QPIGS");
  CHECK(b.messages.size() == 3);
  armor::json::Value doc;
  CHECK(armor::json::parse(b.messages[0], doc) && doc.get("kind")->text == "inverter" && doc.get("mode")->text == "line" && doc.get("pv_w")->number == 856 && doc.get("warnings")->items.size() == 1);
  CHECK(b.topics[0] == "armor/solar/solar-1/axpert-1/state" && doc.get("timestamp_ms")->number > 1700000000000.0);
  const PortStats& st = b.poller.stats();
  CHECK(st.readings == 3 && st.replies_ok == 9 && st.timeouts == 0 && st.replies_bad == 0 && st.bytes_rx > 300 && st.bytes_tx == 3 * (8 + 7 + 8));
  CHECK(b.poller.state(b.now) == "reporting" && b.poller.raw().total() == st.bytes_rx);
  CHECK(b.poller.last_payload() == b.messages.back());
}

static void test_voltronic_faults() {
  // silence: it keeps asking, counts the timeouts, publishes nothing and says "silent"
  Bench silent(config::Kind::kVoltronic, 5);
  silent.run(30000);
  CHECK(silent.messages.empty() && silent.poller.stats().timeouts >= 4 && silent.poller.stats().last_error == "timeout" && silent.poller.state(silent.now) == "silent");
  CHECK(silent.poller.state(1000) == "waiting");
  // the inverter refuses (NAK): no reading, and the error is named
  Bench refuses(config::Kind::kVoltronic, 5);
  refuses.answers["QPIGS"] = reply("NAK");
  refuses.run(8000);
  CHECK(refuses.messages.empty() && refuses.poller.stats().replies_bad >= 1 && refuses.poller.stats().last_error == "nak" && refuses.poller.state(refuses.now) == "garbled");
  // a wrong CRC is refused
  Bench crc(config::Kind::kVoltronic, 5);
  std::vector<std::uint8_t> broken = reply(kQpigs);
  broken[broken.size() - 2] ^= 0x01;
  crc.answers["QPIGS"] = broken;
  crc.run(8000);
  CHECK(crc.messages.empty() && crc.poller.stats().last_error == "crc");
  // a reading is published only when all three answers came: a missing warning list must not look like "no warnings"
  Bench partial(config::Kind::kVoltronic, 5);
  partial.answers["QPIGS"] = reply(kQpigs);
  partial.answers["QMOD"] = reply("B");
  partial.run(20000);
  CHECK(partial.messages.empty() && partial.poller.stats().timeouts >= 2);
  // an answer that is not the reading of that command
  Bench odd(config::Kind::kVoltronic, 5);
  odd.answers["QPIGS"] = reply("hello");
  odd.run(8000);
  CHECK(odd.messages.empty() && odd.poller.stats().last_error == "format");
}

// ---- the other dialects of the inverters ------------------------------------------------------------------------------------------------

// A reply of the PI18 dialect: ^D<payload length + 3><payload><CRC><CR>, or ^0 / ^1 alone.
static std::vector<std::uint8_t> reply18(const std::string& payload, char kind = 'D') {
  std::string head = "^";
  head += kind;
  if (kind == 'D') {
    const std::size_t length = payload.size() + 3;
    head += std::string(1, static_cast<char>('0' + length / 100)) + static_cast<char>('0' + (length / 10) % 10) + static_cast<char>('0' + length % 10) + payload;
  }
  std::vector<std::uint8_t> frame(head.begin(), head.end());
  const auto crc = voltronic::wire_crc(frame.data(), frame.size());
  frame.push_back(crc[0]); frame.push_back(crc[1]); frame.push_back('\r');
  return frame;
}
// A reply of the REVO dialect: the text, a one-byte checksum (the sum of the bytes before it plus one) and CR.
static std::vector<std::uint8_t> reply_chk(const std::string& text) {
  std::vector<std::uint8_t> frame{'('};
  frame.insert(frame.end(), text.begin(), text.end());
  unsigned sum = 1;
  for (std::uint8_t b : frame) sum += b;
  frame.push_back(static_cast<std::uint8_t>(sum & 0xFF)); frame.push_back('\r');
  return frame;
}

static const char* kGs18 = "2320,500,2300,500,0161,0119,003,575,575,000,000,012,100,030,000,000,0856,0000,1038,0000,0,2,0,1,1,1,1,0";
static const char* kFwsClean = "00,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0";
static const char* kRevoQpigs = "232.0 50.0 230.0 50.0 0161 0119 003 460 57.50 012 100 0069 0856 103.8 57.45 00420 00110110 00 00 00856";

static void test_pi18_pieces() {
  std::vector<std::uint8_t> asked;
  CHECK(voltronic::pi18::build_command("GS", asked) && asked.size() == 10 && asked[0] == '^' && asked[1] == 'P' && asked[2] == '0' && asked[3] == '0' && asked[4] == '5' && asked[5] == 'G' && asked[6] == 'S' && asked[9] == '\r');
  const auto crc = voltronic::wire_crc(asked.data(), 7);
  CHECK(asked[7] == crc[0] && asked[8] == crc[1]);
  CHECK(voltronic::pi18::build_command("MOD", asked) && asked[4] == '6' && voltronic::pi18::build_command("FWS", asked) && asked[4] == '6');
  // only reading commands exist
  CHECK(!voltronic::pi18::build_command("PCP0", asked) && asked.empty());
  CHECK(!voltronic::pi18::build_command("POP0", asked) && !voltronic::pi18::build_command("MUCHGC050", asked) && !voltronic::pi18::build_command("", asked));
  // a reply is cut open, and refused when its CRC or its declared length is wrong
  voltronic::pi18::Kind kind;
  std::string payload;
  std::vector<std::uint8_t> good = reply18(kGs18);
  CHECK(voltronic::pi18::parse_reply(good.data(), good.size(), kind, payload) && kind == voltronic::pi18::Kind::kData && payload == kGs18);
  std::vector<std::uint8_t> bad = good;
  bad[9] ^= 1;
  CHECK(!voltronic::pi18::parse_reply(bad.data(), bad.size(), kind, payload));
  std::vector<std::uint8_t> nak = reply18("", '0'), ack = reply18("", '1');
  CHECK(voltronic::pi18::parse_reply(nak.data(), nak.size(), kind, payload) && kind == voltronic::pi18::Kind::kNak);
  CHECK(voltronic::pi18::parse_reply(ack.data(), ack.size(), kind, payload) && kind == voltronic::pi18::Kind::kAck);
  std::vector<std::uint8_t> lie = good;                       // a declared length that is not the length: re-signed so only the length is wrong
  lie[4] = '9';
  const auto lie_crc = voltronic::wire_crc(lie.data(), lie.size() - 3);
  lie[lie.size() - 3] = lie_crc[0]; lie[lie.size() - 2] = lie_crc[1];
  CHECK(!voltronic::pi18::parse_reply(lie.data(), lie.size(), kind, payload));
  // the framer: noise, then a '^' starts a frame; a second '^' starts over
  voltronic::pi18::ReplyFramer framer;
  std::vector<std::uint8_t> got;
  bool whole = false;
  for (std::uint8_t b : std::vector<std::uint8_t>{'x', 'y', '^', 'D'}) whole = whole || framer.feed(b, got);
  CHECK(!whole);
  for (std::uint8_t b : good) { if (framer.feed(b, got)) whole = true; }
  CHECK(whole && got == good);
  // the general status
  voltronic::Status s;
  CHECK(voltronic::pi18::parse_gs(kGs18, s) && s.grid_v == 232.0 && s.grid_hz == 50.0 && s.out_v == 230.0 && s.out_w == 119 && s.out_va == 161 && s.load_percent == 3);
  CHECK(s.battery_v == 57.5 && s.battery_charge_a == 12 && s.battery_percent == 100 && s.heatsink_c == 30 && s.pv_w == 856 && s.has_pv_w && s.pv_v > 103.7 && s.pv_v < 103.9);
  CHECK(s.pv_a > 8.2 && s.pv_a < 8.3 && s.load_on && s.scc_charging && s.charging && s.ac_charging && !s.config_changed);
  CHECK(!voltronic::pi18::parse_gs("1,2,3", s) && !voltronic::pi18::parse_gs("", s));
  // two PV inputs add up; the stronger one's voltage is shown
  std::string two = kGs18;
  two.replace(two.find("0856,0000,1038,0000"), 19, "0400,0300,0900,1100");
  CHECK(voltronic::pi18::parse_gs(two, s) && s.pv_w == 700 && s.pv_v > 89.9 && s.pv_v < 90.1);
  char mode = 0;
  CHECK(voltronic::pi18::parse_mod("00", mode) && mode == 'P' && voltronic::pi18::parse_mod("01", mode) && mode == 'S' && voltronic::pi18::parse_mod("03", mode) && mode == 'B' && voltronic::pi18::parse_mod("05", mode) && mode == 'L');
  CHECK(voltronic::pi18::parse_mod("04", mode) && mode == 'F' && !voltronic::pi18::parse_mod("06", mode) && !voltronic::pi18::parse_mod("0", mode) && !voltronic::pi18::parse_mod("1A", mode));
  // faults and warnings
  std::vector<std::string> warnings;
  CHECK(voltronic::pi18::parse_fws(kFwsClean, warnings) && warnings.empty());
  CHECK(voltronic::pi18::parse_fws("00,1,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0", warnings) && warnings.size() == 2 && warnings[0] == "line_fail" && warnings[1] == "battery_low");
  CHECK(voltronic::pi18::parse_fws("07,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0", warnings) && warnings.size() == 2 && warnings[0] == "inverter_fault" && warnings[1] == "fault_code_7");
  CHECK(!voltronic::pi18::parse_fws("00,0,0", warnings));
}

static void test_revo_and_warning_bits() {
  voltronic::Status s;
  CHECK(voltronic::parse_qpigs_revo(kRevoQpigs, s) && s.pv_w == 856 && s.has_pv_w && s.pv_v > 103.7 && s.pv_v < 103.9 && s.pv_a > 8.2 && s.pv_a < 8.3 && s.battery_v_scc > 57.4 && s.battery_v_scc < 57.5);
  CHECK(s.grid_v == 232.0 && s.out_w == 119 && s.battery_percent == 100 && s.charging && s.scc_charging && !s.ac_charging);
  CHECK(!voltronic::parse_qpigs_revo("232.0 50.0", s));
  // the checksum reply
  std::vector<std::uint8_t> chk = reply_chk(kRevoQpigs);
  std::string text;
  CHECK(voltronic::parse_reply_revo(chk.data(), chk.size(), text) && text == kRevoQpigs);
  CHECK(!voltronic::parse_reply(chk.data(), chk.size(), text));                     // the standard check refuses it
  chk[chk.size() - 2] ^= 1;
  CHECK(!voltronic::parse_reply_revo(chk.data(), chk.size(), text));
  std::vector<std::uint8_t> crc = reply(kRevoQpigs);
  CHECK(voltronic::parse_reply_revo(crc.data(), crc.size(), text) && text == kRevoQpigs);   // a CRC reply is accepted too
  // 36 warning flags: the three "battery weak" bits give one name; the ones past bit 31 are read
  std::vector<std::string> warnings;
  std::string bits(36, '0');
  bits[0] = '1'; bits[15] = '1'; bits[31] = '1'; bits[32] = '1'; bits[33] = '1'; bits[35] = '1';
  CHECK(voltronic::parse_qpiws(bits, warnings) && warnings.size() == 4 && warnings[0] == "pv_loss" && warnings[1] == "battery_derating" && warnings[2] == "battery_weak" && warnings[3] == "battery_equalisation");
  CHECK(voltronic::parse_qpiws(std::string(32, '0'), warnings) && warnings.empty());
}

static void test_inverter_dialects() {
  // PI18 chosen by hand
  Bench b(config::Kind::kVoltronic, 5, 0, "pi18");
  b.answers["^GS"] = reply18(kGs18);
  b.answers["^MOD"] = reply18("03");
  b.answers["^FWS"] = reply18("00,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0");
  b.run(12000);
  CHECK(b.asked.size() >= 6 && b.asked[0] == "^GS" && b.asked[1] == "^MOD" && b.asked[2] == "^FWS" && b.asked[3] == "^GS");
  CHECK(b.messages.size() >= 2 && b.poller.detail() == "pi18" && b.poller.state(b.now) == "reporting");
  armor::json::Value doc;
  CHECK(armor::json::parse(b.messages[0], doc) && doc.get("kind")->text == "inverter" && doc.get("mode")->text == "battery" && doc.get("pv_w")->number == 856 && doc.get("warnings")->items.size() == 1);
  CHECK(doc.get("warnings")->items[0].text == "battery_low");
  CHECK(b.poller.stats().replies_ok == 3 * b.messages.size() && b.poller.stats().timeouts == 0);

  // "auto" finds PI30 first, with the very first request
  Bench pi30(config::Kind::kVoltronic, 5);
  pi30.answers["QPIGS"] = reply(kQpigs); pi30.answers["QMOD"] = reply("L"); pi30.answers["QPIWS"] = reply(std::string(32, '0'));
  CHECK(pi30.poller.detail().empty());
  pi30.run(6000);
  CHECK(pi30.messages.size() >= 1 && pi30.poller.detail() == "pi30" && pi30.asked[0] == "QPIGS");

  // "auto" on a PI18 inverter that says nothing to the older frames: PI30 times out, then PI18 is tried and kept
  Bench found(config::Kind::kVoltronic, 5);
  found.answers["^GS"] = reply18(kGs18); found.answers["^MOD"] = reply18("05"); found.answers["^FWS"] = reply18(kFwsClean);
  found.run(20000);
  CHECK(found.asked[0] == "QPIGS" && found.asked[1] == "^GS" && found.messages.size() >= 2 && found.poller.detail() == "pi18");
  CHECK(found.poller.stats().timeouts == 1);
  // it goes quiet: after three misses it looks again (and finds the older dialect if that is what comes back)
  found.answers.clear();
  found.answers["QPIGS"] = reply(kQpigs); found.answers["QMOD"] = reply("L"); found.answers["QPIWS"] = reply(std::string(32, '0'));
  const std::size_t before = found.messages.size();
  found.run(found.now + 40000);
  CHECK(found.messages.size() > before && found.poller.detail() == "pi30");

  // "auto" on a PI18 inverter that refuses the older frames with ^0: the reply is a whole PI18 frame, so it switches at once
  Bench nak(config::Kind::kVoltronic, 5);
  nak.answers["QPIGS"] = reply18("", '0');
  nak.answers["^GS"] = reply18(kGs18); nak.answers["^MOD"] = reply18("00"); nak.answers["^FWS"] = reply18(kFwsClean);
  nak.run(6000);
  CHECK(nak.asked[0] == "QPIGS" && nak.asked[1] == "^GS" && nak.messages.size() >= 1 && nak.poller.detail() == "pi18" && nak.poller.stats().timeouts == 0);

  // REVO: checksum replies make it REVO by themselves, and no warning list is asked
  Bench revo(config::Kind::kVoltronic, 5);
  revo.answers["QPIGS"] = reply_chk(kRevoQpigs); revo.answers["QMOD"] = reply_chk("L");
  revo.run(12000);
  CHECK(revo.poller.detail() == "revo" && revo.messages.size() >= 2 && revo.asked[0] == "QPIGS" && revo.asked[1] == "QMOD" && revo.asked[2] == "QPIGS");
  CHECK(armor::json::parse(revo.messages[0], doc) && doc.get("pv_w")->number == 856 && doc.get("mode")->text == "line" && doc.get("warnings")->items.empty());
  // REVO chosen by hand also takes CRC replies
  Bench revo_crc(config::Kind::kVoltronic, 5, 0, "revo");
  revo_crc.answers["QPIGS"] = reply(kRevoQpigs); revo_crc.answers["QMOD"] = reply("L");
  revo_crc.run(8000);
  CHECK(revo_crc.poller.detail() == "revo" && revo_crc.messages.size() >= 1 && armor::json::parse(revo_crc.messages[0], doc) && doc.get("pv_w")->number == 856);

  // a dialect chosen by hand is never given up: silence keeps asking the same thing
  Bench fixed(config::Kind::kVoltronic, 5, 0, "pi30");
  fixed.run(30000);
  bool only_pi30 = !fixed.asked.empty();
  for (const std::string& a : fixed.asked) if (a != "QPIGS") only_pi30 = false;
  CHECK(only_pi30 && fixed.poller.detail() == "pi30" && fixed.messages.empty());
  // a PI18 port hears nothing of the older frames, and a checksum-only reply is not a PI18 reply
  Bench mismatched(config::Kind::kVoltronic, 5, 0, "pi18");
  mismatched.answers["^GS"] = reply(kQpigs);
  mismatched.run(9000);
  CHECK(mismatched.messages.empty() && mismatched.poller.detail() == "pi18");
  // a garbled PI18 reply is named, and a refusal too
  Bench garbled(config::Kind::kVoltronic, 5, 0, "pi18");
  std::vector<std::uint8_t> broken = reply18(kGs18);
  broken[8] ^= 1;
  garbled.answers["^GS"] = broken;
  garbled.run(9000);
  CHECK(garbled.messages.empty() && garbled.poller.stats().last_error == "crc");
  Bench refuses(config::Kind::kVoltronic, 5, 0, "pi18");
  refuses.answers["^GS"] = reply18("", '0');
  refuses.run(9000);
  CHECK(refuses.messages.empty() && refuses.poller.stats().last_error == "nak");
  Bench odd(config::Kind::kVoltronic, 5, 0, "pi18");
  odd.answers["^GS"] = reply18("1,2,3");
  odd.run(9000);
  CHECK(odd.messages.empty() && odd.poller.stats().last_error == "format");
  // the dialect in a stored configuration
  config::Settings s = valid_settings();
  s.ports[0] = {true, "voltronic", "axpert-1", 0, 16, 15, -1, 0, 0, "pi18"};
  CHECK(config::validate(s).empty());
  s.ports[0].dialect = "pi99";
  CHECK(has(config::validate(s), "ports.0.dialect", "invalid"));
  s.ports[0].dialect = "revo";
  config::Settings back;
  config::Problems problems;
  CHECK(config::load(config::to_json(s, true), config::default_settings("000000"), back, problems) && back.ports[0].dialect == "revo");
  CHECK(config::load("{\"ports\":[{\"dialect\":\"pi30\"}]}", s, back, problems) && back.ports[0].dialect == "pi30" && config::default_settings("000000").ports[0].dialect == "auto");
}

static void test_pylontech_port() {
  Bench b(config::Kind::kPylontech, 10);
  b.answers["pwr\r"] = bytes_of(kPwr);
  b.answers["bat 1\r"] = bytes_of(bat_table());
  b.answers["bat 2\r"] = bytes_of(bat_table());
  b.answers["info 1\r"] = bytes_of(kInfo);
  b.answers["info 2\r"] = bytes_of(kInfo);
  b.answers["stat 1\r"] = bytes_of(kStat);
  b.answers["stat 2\r"] = bytes_of(kStat);
  b.run(15000);
  CHECK(b.asked.size() >= 8 && b.asked[0] == "pwr\r" && b.asked[1] == "bat 1\r" && b.asked[2] == "info 1\r" && b.asked[3] == "stat 1\r" && b.asked[4] == "bat 2\r" && b.asked[5] == "info 2\r" && b.asked[6] == "stat 2\r" && b.asked[7] == "pwr\r");
  CHECK(b.messages.size() == 2);        // module 3 is absent and is never asked about
  armor::json::Value doc;
  CHECK(armor::json::parse(b.messages[0], doc) && doc.get("kind")->text == "battery" && doc.get("modules")->number == 2 && doc.get("full_capacity_ah")->number > 147.9);
  CHECK(doc.get("stack")->items[0].get("cells_v")->items.size() == 15 && doc.get("model")->text == "US3000C" && doc.get("cycles")->number == 312);
  CHECK(b.topics[0] == "armor/solar/solar-1/us3000-1/state" && b.poller.state(b.now) == "reporting");
  // only the first module's details when the limit says so
  Bench one(config::Kind::kPylontech, 10, 1);
  one.answers["pwr\r"] = bytes_of(kPwr);
  one.answers["bat 1\r"] = bytes_of(bat_table());
  one.answers["info 1\r"] = bytes_of(kInfo);
  one.answers["stat 1\r"] = bytes_of(kStat);
  one.run(12000);
  CHECK(one.asked.size() >= 5 && one.asked[0] == "pwr\r" && one.asked[1] == "bat 1\r" && one.asked[2] == "info 1\r" && one.asked[3] == "stat 1\r" && one.asked[4] == "pwr\r" && one.messages.size() == 2);

  // a battery of the newer firmware (US5000 V2.3): its own header, the charge in mAH on the cells' rows, the identity in `info` and the cycles in `stat`. The identity and the
  // history are asked once and kept: the next cycle asks `pwr` and `bat` only, and still reports them
  Bench real(config::Kind::kPylontech, 10, 1);
  real.answers["pwr\r"] = bytes_of(kPwr5000);
  real.answers["bat 1\r"] = bytes_of(bat_table_mah());
  real.answers["info 1\r"] = bytes_of(kInfoReal);
  real.answers["stat 1\r"] = bytes_of(kStat);
  real.run(25000);
  CHECK(real.asked.size() >= 8 && real.asked[0] == "pwr\r" && real.asked[1] == "bat 1\r" && real.asked[2] == "info 1\r" && real.asked[3] == "stat 1\r" && real.asked[4] == "pwr\r" && real.asked[5] == "bat 1\r" && real.asked[6] == "pwr\r");
  CHECK(real.messages.size() == 3);
  for (const std::string& message : real.messages) {
    CHECK(armor::json::parse(message, doc) && doc.get("model")->text == "US5000" && doc.get("cycles")->number == 312 && doc.get("full_capacity_ah")->number > 73.9 && doc.get("full_capacity_ah")->number < 74.1);
    CHECK(doc.get("health_percent")->number == 50 && doc.get("stack")->items[0].get("health_percent")->number == 50);   // 133200000 mAs of 74 Ah
    CHECK(doc.get("capacity_ah")->number > 66.5 && doc.get("capacity_ah")->number < 66.6 && doc.get("stack")->items[0].get("temperatures_c")->items.size() == 2);
  }
}

static void test_pylontech_refusals() {
  const std::string refused = "@" + std::string(1, 13) + std::string(1, 10) + "Invalid command or fail to excute." + std::string(1, 13) + std::string(1, 10) + "$$" + std::string(1, 13) + std::string(1, 10) + "pylon>";
  Bench b(config::Kind::kPylontech, 10, 1);
  b.answers["pwr\r"] = bytes_of(kPwr);
  b.answers["bat 1\r"] = bytes_of(bat_table());
  b.answers["info 1\r"] = bytes_of(refused);
  b.answers["stat 1\r"] = bytes_of(refused);
  b.run(4000);
  // the refusals end the exchange at once: no timeout, and the cells came in
  CHECK(b.poller.stats().timeouts == 0 && !b.messages.empty());
  armor::json::Value doc;
  CHECK(armor::json::parse(b.messages[0], doc) && doc.get("stack")->items[0].get("cells_v") != nullptr && doc.get("health_percent") == nullptr && doc.get("cycles") == nullptr);
}

static void test_pylontech_faults() {
  // the extras do not answer: the reading goes out with what `pwr` said
  Bench b(config::Kind::kPylontech, 10);
  b.answers["pwr\r"] = bytes_of(kPwr);
  b.run(20000);
  CHECK(!b.messages.empty());
  armor::json::Value doc;
  CHECK(armor::json::parse(b.messages[0], doc));
  CHECK(doc.get("modules")->number == 2);
  CHECK(doc.get("stack")->items[0].get("cells_v") == nullptr);
  CHECK(doc.get("capacity_ah") == nullptr);
  CHECK(b.poller.stats().timeouts >= 4);
  // nothing answers `pwr`: no message (the server will call the device silent), and the node keeps asking
  Bench silent(config::Kind::kPylontech, 10);
  silent.run(40000);
  CHECK(silent.messages.empty() && silent.poller.stats().timeouts >= 3 && silent.poller.state(silent.now) == "silent");
  // text that is not a table
  Bench junk(config::Kind::kPylontech, 10);
  junk.answers["pwr\r"] = bytes_of("\r\nUnknown command\r\nCommand completed successfully\r\n$$\r\npylon>");
  junk.run(12000);
  CHECK(junk.messages.empty() && junk.poller.stats().last_error == "format" && junk.poller.state(junk.now) == "garbled");
  // a stack that says every module is absent is an honest "nothing there": the message has no modules
  Bench absent(config::Kind::kPylontech, 10);
  absent.answers["pwr\r"] = bytes_of("@\r\nPower Volt Curr Tempr\r\n1     -      -      -      -      -      -      -      Absent   -        -        -        -        -                    -        -\r\nCommand completed successfully\r\n$$\r\npylon>");
  absent.run(5000);
  armor::json::Value none;
  CHECK(!absent.messages.empty() && armor::json::parse(absent.messages[0], none) && none.get("modules")->number == 0);
}

static void test_raw_port() {
  Poller raw(config::Kind::kRaw, "solar-1", "sniffer", 5, 0);
  CHECK(raw.next_tx(1000).empty() && raw.state(1000) == "silent");
  const std::string hello = "Hello, inverter\r\n";
  raw.on_rx(reinterpret_cast<const std::uint8_t*>(hello.data()), hello.size(), 2000);
  CHECK(raw.state(3000) == "listening" && raw.state(20000) == "silent" && raw.stats().bytes_rx == hello.size());
  const std::string dump = raw.raw().dump();
  CHECK(dump.find("48 65 6C 6C 6F 2C") == 0 && dump.find("|Hello, inverter.|") != std::string::npos);
  std::string topic, payload;
  CHECK(!raw.take_message(1, topic, payload));
  // the log keeps the last 2 kB only
  RawLog log;
  std::vector<std::uint8_t> lots(5000, 'A');
  log.add(lots.data(), lots.size());
  const std::string kept = log.dump(1000);
  CHECK(log.total() == 5000 && std::count(kept.begin(), kept.end(), '\n') == 128);
}

// ---- the ANT-BMS ---------------------------------------------------------------------------------------------------------------------

static bool near(double a, double b, double eps) { return a > b - eps && a < b + eps; }

static void test_ant_decoder() {
  // the request frames: the old one is fixed, the new one carries its CRC (0x5518 in the capture that documents it)
  CHECK(ant::request(ant::Protocol::kOld) == std::vector<std::uint8_t>({0x5A, 0x5A, 0x00, 0x00, 0x01, 0x01}));
  CHECK(ant::request(ant::Protocol::kNew) == std::vector<std::uint8_t>({0x7E, 0xA1, 0x01, 0x00, 0x00, 0xBE, 0x18, 0x55, 0xAA, 0x55}));

  // a 16S 280 Ah BMS of the new protocol: the values are the ones its captured frame is documented to hold
  ant::Reading r;
  CHECK(ant::decode_new(kNew16S.data(), kNew16S.size(), r) && r.protocol == ant::Protocol::kNew && r.cells == 16 && r.module.cells_v.size() == 16);
  CHECK(near(r.module.voltage_v, 52.84, 0.001) && near(r.module.current_a, 0.3, 0.001) && r.module.soc_percent == 91 && near(r.power_w, 15, 0.5));
  CHECK(near(r.module.full_capacity_ah, 280.0, 0.01) && near(r.module.capacity_ah, 252.602, 0.001) && near(r.cycle_ah, 4862.65, 0.01) && r.module.cycles == 17);
  CHECK(near(r.module.cells_v[0], 3.300, 0.0005) && near(r.module.cells_v[15], 3.305, 0.0005) && near(r.module.cell_low_v, 3.300, 0.0005) && near(r.module.cell_high_v, 3.305, 0.0005));
  CHECK(r.module.temperatures_c.size() == 4 && r.module.temperatures_c[0] == 1 && r.module.temperatures_c[1] == 2 && r.module.temperatures_c[3] == 7 && r.module.temperature_low_c == 1 && r.module.temperature_high_c == 7);
  CHECK(r.charge_mos == 1 && r.discharge_mos == 1 && r.balancer == 0 && !r.protecting && r.module.base_state == "Idle" && r.module.voltage_state == "Normal");

  // an 8S BMS of the old protocol, discharging at 10.7 A, that does not know its capacity (it says 0 Ah): no capacity and no cycles are made up
  CHECK(ant::decode_old(kOld8S.data(), kOld8S.size(), r) && r.protocol == ant::Protocol::kOld && r.cells == 8);
  CHECK(near(r.module.voltage_v, 26.7, 0.001) && near(r.module.current_a, -10.7, 0.001) && r.module.soc_percent == 41 && near(r.power_w, -285, 0.5));
  CHECK(r.module.full_capacity_ah < 0 && r.module.capacity_ah < 0 && r.module.cycles < 0 && near(r.cycle_ah, 19276.387, 0.01));
  CHECK(near(r.module.cell_low_v, 3.338, 0.0005) && near(r.module.cell_high_v, 3.340, 0.0005) && r.module.base_state == "Dischg");
  CHECK(r.module.temperatures_c.size() == 4 && r.module.temperatures_c[0] == 15 && r.module.temperatures_c[3] == 13);   // the two empty slots (0) are not sensors
  CHECK(r.charge_mos == 1 && r.discharge_mos == 1 && r.balancer == 0 && !r.protecting);

  // captures of three other models: they decode, and the total voltage is the sum of the cells
  for (const std::vector<std::uint8_t>* frame : {&kOld2021, &kOld2019}) {
    CHECK(ant::decode_old(frame->data(), frame->size(), r) && r.cells >= 8);
    double sum = 0;
    for (double v : r.module.cells_v) sum += v;
    CHECK(near(sum, r.module.voltage_v, r.module.voltage_v * 0.02) && r.module.soc_percent >= 0);
  }
  CHECK(ant::decode_old(kOld2021.data(), kOld2021.size(), r) && r.cells == 16 && r.module.temperatures_c.size() == 4);   // -40 is "no sensor"
  CHECK(ant::decode_new(kNew2021.data(), kNew2021.size(), r) && r.cells == 14 && near(r.module.voltage_v, 57.58, 0.001) && r.module.temperatures_c.size() == 5 && r.module.temperature_low_c == 28);
  double sum = 0;
  for (double v : r.module.cells_v) sum += v;
  CHECK(near(sum, r.module.voltage_v, 0.05));

  // whatever is not a whole, checked frame is refused
  std::vector<std::uint8_t> bad = kOld8S;
  bad[20] ^= 0x01;                                       // a bit flipped in a cell: the sum no longer holds
  CHECK(!ant::decode_old(bad.data(), bad.size(), r));
  bad = kOld8S; bad[123] = 0;                            // no cells
  CHECK(!ant::decode_old(bad.data(), bad.size(), r));
  CHECK(!ant::decode_old(kOld8S.data(), 139, r) && !ant::decode_old(kNew16S.data(), kNew16S.size(), r));
  bad = kNew16S; bad[60] ^= 0x10;
  CHECK(!ant::decode_new(bad.data(), bad.size(), r));
  bad = kNew16S; bad[bad.size() - 1] = 0x54;
  CHECK(!ant::decode_new(bad.data(), bad.size(), r) && !ant::decode_new(kNew16S.data(), 100, r) && !ant::decode_new(kOld8S.data(), kOld8S.size(), r));

  // which MOSFET states are a protection (an alarm) and which are only off, full or waiting
  CHECK(ant::charge_protects(2) && ant::charge_protects(3) && !ant::charge_protects(0) && !ant::charge_protects(1) && !ant::charge_protects(4) && !ant::charge_protects(14) && !ant::charge_protects(15));
  CHECK(ant::discharge_protects(2) && ant::discharge_protects(12) && !ant::discharge_protects(0) && !ant::discharge_protects(1) && !ant::discharge_protects(11) && !ant::discharge_protects(15));
  // and the message of a battery in protection has the alarm set
  ant::Reading p;
  CHECK(ant::decode_old(kOld8S.data(), kOld8S.size(), p));
  p.charge_mos = 3;
  std::vector<pylontech::Module> one(1, p.module);
  one[0].voltage_state = "Protection";
  armor::json::Value alarm;
  CHECK(armor::json::parse(battery_json("solar-1", "ant-1", 1700000000000ULL, one), alarm) && alarm.get("alarm")->boolean == true);
}

static void test_ant_framer() {
  ant::Framer framer;
  std::vector<std::uint8_t> frame;
  ant::Protocol protocol = ant::Protocol::kNew;
  int frames = 0;
  std::vector<std::vector<std::uint8_t>> got;
  std::vector<ant::Protocol> kinds;
  const auto feed = [&](const std::vector<std::uint8_t>& bytes) {
    for (std::uint8_t b : bytes) if (framer.feed(b, frame, protocol)) { ++frames; got.push_back(frame); kinds.push_back(protocol); }
  };
  // noise first (including the start of a false frame), then the real one; the frame is found however the noise ends
  feed({0x00, 0x7E, 0x13, 0xAA, 0x55, 0xAA, 0x55});
  feed(kOld8S);
  CHECK(frames == 1 && got[0] == kOld8S && kinds[0] == ant::Protocol::kOld);
  // two frames of the two protocols back to back
  feed(kNew16S);
  feed(kOld2021);
  CHECK(frames == 3 && got[1] == kNew16S && kinds[1] == ant::Protocol::kNew && got[2] == kOld2021);
  // a frame cut short: the port forgets what it had at the start of every request (framer.clear()), and then a whole one comes out
  const std::vector<std::uint8_t> half(kNew16S.begin(), kNew16S.begin() + 60);
  feed(half);
  CHECK(frames == 3);
  framer.clear();
  feed(kNew16S);
  CHECK(frames == 4 && got.back() == kNew16S);
  // a length that cannot be (a 0xFF length byte) is let go
  framer.clear();
  const int before = frames;
  feed({0x7E, 0xA1, 0x11, 0x00, 0x00, 0xFF});
  feed(kOld8S);
  CHECK(frames == before + 1 && got.back() == kOld8S);
}

static void test_ant_port() {
  const std::string old_key = as_text(ant::request(ant::Protocol::kOld)), new_key = as_text(ant::request(ant::Protocol::kNew));
  // a BMS that speaks the new protocol: the port asks in the old one first, hears nothing, asks in the new one, and then keeps to it
  Bench b(config::Kind::kAnt, 5);
  b.answers[new_key] = kNew16S;
  b.run(13000);
  CHECK(b.asked.size() >= 4 && b.asked[0] == old_key && b.asked[1] == new_key && b.asked[2] == new_key && b.asked[3] == new_key);
  CHECK(b.messages.size() >= 2 && b.poller.stats().timeouts == 1 && b.poller.stats().replies_ok >= 2 && b.poller.state(b.now) == "reporting");
  armor::json::Value doc;
  CHECK(armor::json::parse(b.messages[0], doc) && doc.get("kind")->text == "battery" && doc.get("modules")->number == 1 && doc.get("model")->text == "ANT-BMS" && doc.get("soc_percent")->number == 91);
  CHECK(doc.get("stack")->items[0].get("cells_v")->items.size() == 16 && doc.get("stack")->items[0].get("temperatures_c")->items.size() == 4 && doc.get("alarm")->boolean == false);
  CHECK(b.topics[0] == "armor/solar/solar-1/us3000-1/state" && b.poller.detail().compare(0, 4, "new ") == 0 && b.poller.detail().find("cells=16") != std::string::npos);

  // a BMS of the old protocol answers the first request
  Bench o(config::Kind::kAnt, 5);
  o.answers[old_key] = kOld8S;
  o.run(12000);
  CHECK(o.asked.size() >= 3 && o.asked[0] == old_key && o.asked[1] == old_key && o.poller.stats().timeouts == 0 && o.messages.size() >= 3);
  CHECK(armor::json::parse(o.messages[0], doc) && doc.get("state")->text == "discharging" && doc.get("current_a")->number < -10 && o.poller.detail().compare(0, 4, "old ") == 0);

  // nothing answers: it alternates the protocols, counts the misses, publishes nothing and says "silent"
  Bench s(config::Kind::kAnt, 5);
  s.run(30000);
  CHECK(s.messages.empty() && s.poller.stats().timeouts >= 8 && s.asked[0] == old_key && s.asked[1] == new_key && s.asked[2] == old_key && s.poller.state(s.now) == "silent" && s.poller.detail().empty());

  // a frame that does not check is refused and named
  std::vector<std::uint8_t> broken = kOld8S;
  broken[30] ^= 0x04;
  Bench c(config::Kind::kAnt, 5);
  c.answers[old_key] = broken;
  c.run(6000);
  CHECK(c.messages.empty() && c.poller.stats().replies_bad >= 1 && c.poller.stats().last_error == "crc" && c.poller.state(c.now) == "garbled");

  // a BMS that was heard and then goes silent: after three misses the port looks for the protocol again
  Bench g(config::Kind::kAnt, 5);
  g.answers[old_key] = kOld8S;
  g.run(6000);
  CHECK(!g.messages.empty());
  g.answers.clear();
  g.answers[new_key] = kNew16S;
  g.run(40000);
  bool asked_new = false;
  for (const std::string& a : g.asked) if (a == new_key) asked_new = true;
  CHECK(asked_new && g.messages.size() >= 2 && g.poller.detail().compare(0, 4, "new ") == 0);
}

static void test_ten_ports() {
  // all ten ports at once on their default pins: none of the pins is shared, reserved or missing, and the ten names differ
  config::Settings s = valid_settings();
  for (std::size_t i = 0; i < s.ports.size(); ++i) { s.ports[i].enabled = true; s.ports[i].kind = i % 2 ? "ant" : "voltronic"; s.ports[i].name = "device-" + std::to_string(i + 1); }
  CHECK(config::validate(s).empty());
  CHECK(!config::is_soft_port(2) && config::is_soft_port(3) && config::is_soft_port(9));
  // a document with the ten ports round-trips
  config::Settings back;
  config::Problems problems;
  CHECK(config::load(config::to_json(s, true), config::default_settings("000000"), back, problems) && back.ports[9].kind == "ant" && back.ports[9].name == "device-10" && back.ports[9].tx == 1);
  // a document stored by the seven-port firmware still loads: the ports it did not know keep their defaults
  std::string seven = config::to_json(valid_settings(), true);
  const std::size_t tail = seven.find("{\"enabled\":false,\"kind\":\"voltronic\",\"name\":\"port8\"");
  CHECK(tail != std::string::npos);
  const std::size_t close = seven.find("],\"profile\"");
  CHECK(close != std::string::npos && tail < close);
  seven.erase(tail - 1, close - tail + 1);   // the comma before the eighth port, and the eighth to tenth ports
  config::Settings from_seven;
  problems.clear();
  CHECK(config::load(seven, config::default_settings("000000"), from_seven, problems) && from_seven.ports[9].rx == 42 && from_seven.ports[6].rx == 21);
}

// ---- reading the ANT-BMS settings (read only) ---------------------------------------------------------------------------------------------

static std::vector<std::uint8_t> hex_bytes(const std::string& text) {
  std::vector<std::uint8_t> out;
  for (std::size_t i = 0; i + 1 < text.size(); i += 2) out.push_back(static_cast<std::uint8_t>(std::stoi(text.substr(i, 2), nullptr, 16)));
  return out;
}

static std::vector<std::uint8_t> setting_response(std::uint16_t address, std::uint8_t bytes, std::uint32_t raw) {
  std::vector<std::uint8_t> frame = {0x7E, 0xA1, 0x12, static_cast<std::uint8_t>(address & 0xFF), static_cast<std::uint8_t>(address >> 8), bytes};
  for (int i = 0; i < bytes; ++i) frame.push_back(static_cast<std::uint8_t>((raw >> (8 * i)) & 0xFF));
  const std::uint16_t crc = ant::crc16(frame.data() + 1, frame.size() - 1);
  frame.push_back(static_cast<std::uint8_t>(crc & 0xFF)); frame.push_back(static_cast<std::uint8_t>(crc >> 8));
  frame.push_back(0xAA); frame.push_back(0x55);
  return frame;
}

// The device information frame of a real ANT-BLE16ZMUB (captured by the esphome-ant-bms project): 48 bytes although its length byte says 32.
static const char* kDeviceInfoCapture = "7ea1126c022031365a4d00000000000000000000000031365a4d554230302d323131303236417208ff0b000041f2aa55";

static void test_ant_settings_frames() {
  // the table and the captured requests: 56 registers, every request frame equal to the one the app sent
  CHECK(ant::kRegisterCount == 56 && sizeof(kAntSettingsCases) / sizeof(kAntSettingsCases[0]) == 56);
  for (std::size_t i = 0; i < ant::kRegisterCount; ++i) {
    const AntSettingsCase& c = kAntSettingsCases[i];
    const ant::Register& reg = ant::kRegisters[i];
    CHECK(std::string(reg.name) == c.name && reg.address == c.address && reg.bytes == c.data_len);
    const std::vector<std::uint8_t> request = ant::settings_request(reg.address, reg.bytes);
    CHECK(request == std::vector<std::uint8_t>(c.frame.begin(), c.frame.end()));
  }
  // the device information request is the one a real app sent
  CHECK(ant::settings_request(ant::kDeviceInfoAddress, ant::kDeviceInfoBytes) == hex_bytes("7ea1026c022058c4aa55"));
  // nothing that writes is ever built: a request is always function 0x02, and the table holds no write address
  for (std::size_t i = 0; i < ant::kRegisterCount; ++i) CHECK(ant::settings_request(ant::kRegisters[i].address, ant::kRegisters[i].bytes)[2] == 0x02);

  // the answer of the reference project's documented example: CellOvervoltageProtection = 0x1036 = 4.150 V
  const std::vector<std::uint8_t> answer = hex_bytes("7ea1120000023610" "1f14aa55");
  CHECK(setting_response(0x0000, 2, 0x1036) == answer);
  std::uint16_t address = 0xFFFF; std::uint32_t raw = 0;
  CHECK(ant::decode_setting(answer.data(), answer.size(), address, raw) && address == 0 && raw == 0x1036 && near(raw * ant::kRegisters[0].scale, 4.150, 0.0005));
  // a four-byte register
  const std::vector<std::uint8_t> wide = setting_response(0x00A2, 4, 280000000);
  CHECK(ant::decode_setting(wide.data(), wide.size(), address, raw) && address == 0x00A2 && raw == 280000000 && near(raw * 0.000001, 280.0, 0.001));
  // whatever is not a whole, checked answer is refused
  std::vector<std::uint8_t> bad = answer; bad[6] ^= 0x01;
  CHECK(!ant::decode_setting(bad.data(), bad.size(), address, raw));
  bad = answer; bad[2] = 0x11;
  CHECK(!ant::decode_setting(bad.data(), bad.size(), address, raw));
  CHECK(!ant::decode_setting(answer.data(), answer.size() - 1, address, raw));
  bad = setting_response(0x0000, 3, 5);
  CHECK(!ant::decode_setting(bad.data(), bad.size(), address, raw));      // a register is two or four bytes

  // the device information, from a real capture: read by position, its model and version are text
  const std::vector<std::uint8_t> info = hex_bytes(kDeviceInfoCapture);
  CHECK(info.size() == 48);
  std::string model, version;
  CHECK(ant::decode_device_info(info.data(), info.size(), model, version) && model == "16ZM" && version == "16ZMUB00-211026A");
  CHECK(ant::decode_device_info(info.data(), 42, model, version) && model == "16ZM");    // the whole frame is not needed, only its first 38 bytes
  bad = info; bad[7] = 0x01;                                                                 // a control character in the model
  CHECK(!ant::decode_device_info(bad.data(), bad.size(), model, version));
  bad = info; bad[3] = 0x6D;                                                                 // another address
  CHECK(!ant::decode_device_info(bad.data(), bad.size(), model, version));
  CHECK(!ant::decode_device_info(info.data(), 30, model, version));
}

// A stand-in BMS of the newer protocol: it answers a read request after a latency, with a value made from the address, and the device information like the real capture.
struct AntSettingsBench {
  ant::SettingsReader reader;
  std::vector<std::pair<std::uint64_t, std::vector<std::uint8_t>>> in_flight;
  std::vector<std::vector<std::uint8_t>> asked;
  std::uint64_t now = 0;
  bool answers = true, info_answers = true;
  std::size_t skip_from = 1000;          // registers from this index on are not answered
  bool wrong_address = false;            // answers every register with the address of another one
  void run(std::uint64_t until_ms) {
    for (; now < until_ms && !reader.done(); now += 10) {
      for (auto it = in_flight.begin(); it != in_flight.end();) {
        if (it->first <= now) { reader.on_rx(it->second.data(), it->second.size()); it = in_flight.erase(it); } else ++it;
      }
      const std::vector<std::uint8_t> request = reader.next_tx(now);
      if (request.empty()) continue;
      asked.push_back(request);
      if (!answers) continue;
      const std::uint16_t address = static_cast<std::uint16_t>(request[3] | (request[4] << 8));
      if (address == ant::kDeviceInfoAddress) { if (info_answers) in_flight.push_back({now + 40, hex_bytes(kDeviceInfoCapture)}); continue; }
      for (std::size_t i = 0; i < ant::kRegisterCount; ++i) {
        if (ant::kRegisters[i].address != address || i >= skip_from) continue;
        const std::uint16_t reported = wrong_address ? ant::kRegisters[(i + 1) % ant::kRegisterCount].address : address;
        in_flight.push_back({now + 40, setting_response(reported, ant::kRegisters[i].bytes, 1000 + static_cast<std::uint32_t>(i))});
      }
    }
  }
};

static void test_ant_settings_reader() {
  // a BMS that answers everything: 57 requests (the information and 56 registers), all answered, in order, none written
  AntSettingsBench b;
  b.run(60000);
  CHECK(b.reader.done() && !b.reader.failed() && b.reader.answered() == 57 && b.reader.missing() == 0 && b.asked.size() == 57);
  CHECK(b.asked[0] == hex_bytes("7ea1026c022058c4aa55") && b.asked[1] == ant::settings_request(0x0000, 2) && b.asked[56] == ant::settings_request(0x017E, 2));
  for (const std::vector<std::uint8_t>& request : b.asked) CHECK(request.size() == 10 && request[2] == 0x02);
  CHECK(b.reader.model() == "16ZM" && b.reader.version() == "16ZMUB00-211026A" && b.reader.values().size() == 56 && b.now < 5000);
  CHECK(near(b.reader.values()[0].value, 1.000, 0.0005) && std::string(b.reader.values()[0].name) == "CellOvervoltageProtection" && std::string(b.reader.values()[0].unit) == "V");
  armor::json::Value doc;
  CHECK(armor::json::parse(b.reader.result_json(), doc) && doc.get("model")->text == "16ZM" && doc.get("asked")->number == 57 && doc.get("answered")->number == 57 && doc.get("missing")->number == 0);
  CHECK(doc.get("settings")->items.size() == 56 && doc.get("settings")->items[0].get("name")->text == "CellOvervoltageProtection" && doc.get("settings")->items[0].get("unit")->text == "V");

  // a BMS that answers only the first thirty registers: the others are counted as missing and the rest is kept
  AntSettingsBench partial;
  partial.skip_from = 30;
  partial.run(60000);
  CHECK(partial.reader.done() && !partial.reader.failed() && partial.reader.values().size() == 30 && partial.reader.missing() == 26);

  // nothing listening: three silences in a row and it stops, saying so
  AntSettingsBench silent;
  silent.answers = false;
  silent.run(60000);
  CHECK(silent.reader.done() && silent.reader.failed() && silent.reader.error() == "silent" && silent.asked.size() == 3 && silent.now < 3000);

  // the information does not answer but the registers do: it goes on
  AntSettingsBench no_info;
  no_info.info_answers = false;
  no_info.run(60000);
  CHECK(no_info.reader.done() && !no_info.reader.failed() && no_info.reader.values().size() == 56 && no_info.reader.missing() == 1);
  armor::json::Value without;
  CHECK(armor::json::parse(no_info.reader.result_json(), without) && without.get("model") == nullptr);

  // an answer that is for another register is not taken for this one
  AntSettingsBench wrong;
  wrong.wrong_address = true;
  wrong.run(80000);
  CHECK(wrong.reader.done() && wrong.reader.values().empty() && wrong.reader.missing() == 56);
}

int main() {
  test_defaults_and_pins();
  test_ports();
  test_ten_ports();
  test_wifi_board_has_no_ethernet();
  test_settings_document();
  test_network_plan();
  test_soft_uart();
  test_voltronic_port();
  test_voltronic_faults();
  test_pi18_pieces();
  test_revo_and_warning_bits();
  test_inverter_dialects();
  test_pylontech_port();
  test_pylontech_refusals();
  test_pylontech_faults();
  test_raw_port();
  test_ant_decoder();
  test_ant_framer();
  test_ant_port();
  test_ant_settings_frames();
  test_ant_settings_reader();
  std::printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}

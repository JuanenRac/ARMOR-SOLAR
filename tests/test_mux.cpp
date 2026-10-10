// ARMOR-SOLAR - host tests of the mux profile (the base board with multiplexers in front of the UARTs) and of reading the cells of a stack in rotation:
// the settings of the profile, who has the line and for how long, the LEDs, and the rotation. The equipment is a stand-in written from the public descriptions of the
// protocols; no real inverter or battery has been read, and no multiplexer has switched.
#include <cstdio>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "../core/mux_group.hpp"
#include "../core/port_leds.hpp"
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

// ---- the stand-in equipment (the same texts as test_node.cpp) ----------------------------------------------------------------------------

static std::vector<std::uint8_t> voltronic_reply(const std::string& text) {
  std::vector<std::uint8_t> frame{'('};
  frame.insert(frame.end(), text.begin(), text.end());
  const auto crc = voltronic::wire_crc(frame.data(), frame.size());
  frame.push_back(crc[0]); frame.push_back(crc[1]); frame.push_back('\r');
  return frame;
}
static const char* kQpigs = "232.0 50.0 230.0 50.0 0161 0119 003 460 57.50 012 100 0069 0014 103.8 57.45 00000 00110110 00 00 00856";
static std::string pwr_table(int modules) {
  std::string t = "@\r\nPower Volt   Curr   Tempr  Tlow   Thigh  Vlow   Vhigh  Base.St  Volt.St  Curr.St  Temp.St  Coulomb  Time                 B.V.St   B.T.St\r\n";
  for (int i = 1; i <= modules; ++i)
    t += std::to_string(i) + "     49872  -1280  22000  20000  25000  3330   3348   Dischg   Normal   Normal   Normal   88%      2018-11-14 15:12:04  Normal   Normal\r\n";
  return t + "Command completed successfully\r\n$$\r\npylon>";
}
static std::string bat_table(int seed) {
  std::string t = "@\r\nBattery  Volt     Curr     Tempr    Base State   Volt. State  Curr. State  Temp. State  Coulomb\r\n";
  for (int i = 0; i < 15; ++i) t += std::to_string(i) + " " + std::to_string(3324 + (i * 7 + seed) % 25) + " -1281 22000 Dischg Normal Normal Normal 88%\r\n";
  return t + "Command completed successfully\r\n$$\r\npylon>";
}
static const char* kInfo = "@\r\nDevice address      : 1\r\nDevice name         : US3000C\r\nRemain Capacity     : 65100 mAH\r\nTotal Capacity      : 74000 mAH\r\nCycle Times         : 312\r\nCommand completed successfully\r\n$$\r\npylon>";
static const char* kStat = "@\r\nDevice address      : 1\r\nData Items          : 100\r\nCHARGE Cnt.         : 400\r\nCYCLE Times         : 312\r\nPwr Coulomb         : 133200000\r\nCommand completed successfully\r\n$$\r\npylon>";
static std::vector<std::uint8_t> bytes_of(const std::string& text) { return std::vector<std::uint8_t>(text.begin(), text.end()); }
static std::string as_text(const std::vector<std::uint8_t>& bytes) { return std::string(bytes.begin(), bytes.end()); }

// What is wired to one channel: an answer for what it is sent (empty: silence), the speed it needs and whether its signals arrive upside down at the UART.
struct Device {
  int baud = 2400;
  bool inverted = false;
  std::function<std::vector<std::uint8_t>(const std::string&)> answer;
  unsigned latency_ms = 40;
};
static std::string command_of(const std::vector<std::uint8_t>& out) {
  std::string command = as_text(out);
  if (command.size() > 3 && command[0] == 'Q') return command.substr(0, command.size() - 3);
  if (command.size() > 8 && command[0] == '^') return "^" + command.substr(5, command.size() - 8);
  if (!command.empty() && command.front() == '\r') command.erase(0, 1);   // the bare Enter that wakes a console goes before its first `pwr`
  if (!command.empty() && command.back() == '\r') command.pop_back();
  return command;
}

// The base board: a UART behind a multiplexer. What a device says only reaches the UART while the multiplexer points at it and the UART has the device's speed and polarity.
struct Board : MuxLine {
  std::uint64_t now = 10'000;
  std::map<std::size_t, Device> devices;
  std::size_t selected = 0;
  int baud = 0;
  bool invert = false;
  std::vector<std::uint8_t> uart;       // what the UART holds
  struct Flight { std::uint64_t at; std::size_t channel; std::vector<std::uint8_t> bytes; };
  std::vector<Flight> flights;
  std::vector<std::pair<std::size_t, std::string>> asked;   // (channel, command)
  std::size_t configures = 0, discards = 0, selects = 0;
  std::vector<int> bauds_set;
  bool fail_writes = false;
  bool noise_on_select = false;         // switching the multiplexer injects a stray byte, as a real one may

  void select(std::size_t channel) override {
    selected = channel;
    ++selects;
    if (noise_on_select) uart.push_back(0x5A);
  }
  bool configure(int b, bool i) override { baud = b; invert = i; ++configures; bauds_set.push_back(b); return true; }
  void discard() override { uart.clear(); ++discards; }
  std::size_t read(std::uint8_t* out, std::size_t capacity, unsigned wait_ms) override {
    now += wait_ms;
    for (auto it = flights.begin(); it != flights.end();) {
      if (it->at <= now) {
        if (it->channel == selected) uart.insert(uart.end(), it->bytes.begin(), it->bytes.end());   // the multiplexer moved on: the bytes are lost
        it = flights.erase(it);
      } else ++it;
    }
    std::size_t n = 0;
    while (n < capacity && !uart.empty()) { out[n++] = uart.front(); uart.erase(uart.begin()); }
    return n;
  }
  bool write(const std::uint8_t* data, std::size_t length) override {
    if (fail_writes) return false;
    const std::vector<std::uint8_t> out(data, data + length);
    const std::string command = command_of(out);
    asked.push_back({selected, command});
    const auto device = devices.find(selected);
    if (device == devices.end() || !device->second.answer) return true;
    if (device->second.baud != baud || device->second.inverted != invert) return true;   // wrong speed or polarity: nothing intelligible comes back
    const std::vector<std::uint8_t> answer = device->second.answer(command);
    if (!answer.empty()) flights.push_back({now + device->second.latency_ms, selected, answer});
    return true;
  }
  MuxClock clock() {
    MuxClock c;
    c.uptime_ms = [this] { return now; };
    c.wall_clock_is_set = [] { return true; };
    c.wall_clock_ms = [this] { return 1'700'000'000'000ULL + now; };
    return c;
  }
};

static Device voltronic_device() {
  Device d;
  d.baud = 2400;
  d.answer = [](const std::string& c) -> std::vector<std::uint8_t> {
    if (c == "QPIGS") return voltronic_reply(kQpigs);
    if (c == "QMOD") return voltronic_reply("L");
    if (c == "QPIWS") return voltronic_reply("00000100000000000000000000000000");
    return {};
  };
  return d;
}
static Device pylontech_device(int modules) {
  Device d;
  d.baud = 115200;
  d.answer = [modules](const std::string& c) -> std::vector<std::uint8_t> {
    if (c == "pwr") return bytes_of(pwr_table(modules));
    if (c.rfind("bat ", 0) == 0) return bytes_of(bat_table(std::atoi(c.c_str() + 4)));
    if (c.rfind("info ", 0) == 0) return bytes_of(kInfo);
    if (c.rfind("stat ", 0) == 0) return bytes_of(kStat);
    return {};
  };
  return d;
}

static MuxMember member(std::size_t port, std::size_t channel, config::Kind kind, const std::string& name, int poll_s, bool invert = false, int cells_per_cycle = 0) {
  MuxMember m;
  m.port = port;
  m.channel = channel;
  m.baud = config::info_of(kind).default_baud;
  m.invert = invert;
  m.poller = std::make_unique<Poller>(kind, "solar-1", name, poll_s, 0, "auto", cells_per_cycle);
  return m;
}

struct Outcome { int readings = 0, failures = 0; std::map<std::size_t, int> by_port; std::map<std::size_t, int> failed_by_port; std::vector<std::string> payloads; std::vector<std::size_t> payload_ports; };
static Outcome run(MuxGroup& group, Board& board, std::uint64_t seconds, PortLeds* leds = nullptr) {
  Outcome out;
  const MuxClock clock = board.clock();
  const std::uint64_t until = board.now + seconds * 1000ULL;
  while (board.now < until) {
    const MuxTurn turn = group.step(board, clock);
    if (turn.reading) { ++out.readings; ++out.by_port[turn.port]; out.payloads.push_back(turn.payload); out.payload_ports.push_back(turn.port); if (leds) leds->good(turn.port, board.now); }
    if (turn.ended && turn.failed) { ++out.failures; ++out.failed_by_port[turn.port]; if (leds) leds->bad(turn.port, board.now); }
  }
  return out;
}

// ---- the settings of the profile ---------------------------------------------------------------------------------------------------------------

static void test_profile_settings() {
  const config::Settings d = config::default_settings("a1b2c3");
  CHECK(d.profile == "direct" && d.mux[0].tx == 15 && d.mux[0].rx == 16 && d.mux[0].s0 == 17 && d.mux[0].s1 == 18 && d.mux[0].channels == 4);
  CHECK(d.mux[1].tx == 1 && d.mux[1].rx == 2 && d.mux[1].s0 == 38 && d.mux[1].s1 == 46 && d.mux[1].channels == 4);
  CHECK(d.mux[2].tx == 40 && d.mux[2].rx == 41 && d.mux[2].s0 == 42 && d.mux[2].s1 == -1 && d.mux[2].channels == 2);
  CHECK(d.leds.data == 21 && d.leds.clock == 39 && d.leds.latch == 47 && d.leds.any());
  CHECK(config::mux_port_count(d.mux) == 10);   // 4 + 4 + 2: ports 1 to 8 (batteries) and 9 and 10 (inverters)
  // the ports of the groups: 1 to 4 in group A, 5 to 8 in B, 9 and 10 in C
  config::MuxSlot slot;
  const std::size_t expected_group[10] = {0, 0, 0, 0, 1, 1, 1, 1, 2, 2}, expected_channel[10] = {0, 1, 2, 3, 0, 1, 2, 3, 0, 1};
  for (std::size_t i = 0; i < 10; ++i) CHECK(config::mux_slot_of(d.mux, i, slot) && slot.group == expected_group[i] && slot.channel == expected_channel[i]);
  CHECK(!config::mux_slot_of(d.mux, 10, slot));

  // the default node is valid in the mux profile as it is in the direct one, whatever ports it has on
  config::Settings s = valid_settings();
  CHECK(config::validate(s).empty());
  s.profile = "mux";
  CHECK(config::validate(s).empty());
  s.ports[0] = {true, "pylontech", "us3000-a1", 0, -1, -1, -1, 0, 0, "auto", false, 2};
  s.ports[1] = {true, "pylontech", "us3000-a2", 0, -1, -1, -1, 0, 0, "auto", false, 0};
  s.ports[8] = {true, "voltronic", "axpert-1", 0, -1, -1, -1, 0, 0, "auto", false, 0};
  s.ports[4] = {true, "ant", "ant-1", 0, -1, -1, -1, 0, 0, "auto", true, 0};
  CHECK(config::validate(s).empty());      // no pins of their own: the ports' rx and tx are not looked at
  // only the ports the groups serve may be on
  s.mux[1].channels = 2;                   // group B serves two ports (its second select line tied to ground): 8 ports in all, so 9 and 10 do not exist... until C is counted
  s.mux[2].channels = 1;                   // the third group serves only one port now: 7 ports in all
  CHECK(has(config::validate(s), "ports.8.enabled", "not_in_profile"));
  s.ports[9] = {true, "voltronic", "axpert-2", 0, -1, -1, -1, 0, 0, "auto", false, 0};
  CHECK(has(config::validate(s), "ports.9.enabled", "not_in_profile"));
  s.ports[9].enabled = false; s.mux[1].channels = 4; s.mux[2].channels = 2;
  CHECK(config::validate(s).empty());
  // a name is still a name, a speed still a speed
  s.ports[1].name = "us3000-a1";
  CHECK(has(config::validate(s), "ports.1.name", "conflict"));
  s.ports[1].name = "us3000-a2";
  s.ports[1].baud = 300;
  CHECK(has(config::validate(s), "ports.1.baud", "range"));
  s.ports[1].baud = 460800;                // every port of the profile has a hardware UART: no emulated limit
  CHECK(config::validate(s).empty());
  s.ports[1].baud = 0;
  // the pins of the groups
  s.mux[0].tx = 33;
  CHECK(has(config::validate(s), "mux.0.tx", "reserved"));
  s.mux[0].tx = 16;
  CHECK(has(config::validate(s), "mux.0.rx", "conflict"));
  s.mux[0].tx = 15;
  s.mux[0].s1 = -1;
  CHECK(has(config::validate(s), "mux.0.s1", "required"));     // four ports need both select pins
  s.mux[0].channels = 2; s.ports[8].enabled = false;
  CHECK(config::validate(s).empty());                            // two need only one
  s.mux[0].channels = 4; s.mux[0].s1 = 18; s.ports[8].enabled = true;
  s.mux[1].rx = -1;
  CHECK(has(config::validate(s), "mux.1.rx", "required"));
  s.mux[1].rx = 2;
  s.mux[2].s0 = 38;
  CHECK(has(config::validate(s), "mux.2.s0", "conflict"));
  s.mux[2].s0 = 42;
  s.mux[1].channels = 5;
  CHECK(has(config::validate(s), "mux.1.channels", "range"));
  s.mux[1].channels = 4;
  // the LEDs: all three pins or none
  s.leds.clock = -1;
  CHECK(has(config::validate(s), "leds.clock", "required"));
  s.leds.clock = 21;
  CHECK(has(config::validate(s), "leds.clock", "conflict"));
  s.leds = {-1, -1, -1};
  CHECK(config::validate(s).empty());
  s.leds = {21, 39, 47};
  // the profile must be one of the two, and the direct one is checked as before
  s.profile = "other";
  CHECK(has(config::validate(s), "profile", "invalid"));
  s.profile = "direct";
  s.ports[0].rx = -1;
  CHECK(has(config::validate(s), "ports.0.rx", "required"));
  s.ports[0].rx = 16;
  s.ports[0].rx = board::kDefaultPins[0].rx;
  s.ports[8].invert = true;                // an emulated port cannot flip its line
  CHECK(has(config::validate(s), "ports.8.invert", "invalid"));
  s.ports[8].invert = false;
  s.ports[0].cells_per_cycle = 9;
  CHECK(has(config::validate(s), "ports.0.cells_per_cycle", "range"));
}

static void test_profile_document() {
  config::Settings s = valid_settings();
  s.profile = "mux";
  s.mux[1] = {3, 4, 5, -1, 2};
  s.leds = {-1, -1, -1};
  s.ports[0] = {true, "pylontech", "us3000-a1", 0, -1, -1, -1, 0, 0, "auto", true, 3};
  const std::string stored = config::to_json(s, true);
  CHECK(stored.find("\"profile\":\"mux\"") != std::string::npos && stored.find("\"leds\":{\"data\":-1,\"clock\":-1,\"latch\":-1}") != std::string::npos);
  CHECK(stored.find("\"invert\":true") != std::string::npos && stored.find("\"cells_per_cycle\":3") != std::string::npos);
  config::Settings back;
  config::Problems problems;
  CHECK(config::load(stored, config::default_settings("000000"), back, problems) && back.profile == "mux" && back.mux[1].tx == 3 && back.mux[1].s0 == 5 && !back.leds.any());
  CHECK(back.ports[0].invert && back.ports[0].cells_per_cycle == 3 && config::to_json(back, true) == stored);
  // a partial document from the panel changes only what it names
  config::Settings edited;
  CHECK(!config::load("{\"profile\":\"direct\"}", back, edited, problems) && has(problems, "ports.0.rx", "required"));   // the direct profile needs pins of its own, which a port of the mux profile has none of
  problems.clear();
  problems.clear();
  CHECK(config::load("{\"mux\":[{\"channels\":4,\"s1\":18}]}", s, edited, problems) && edited.mux[0].channels == 4 && edited.mux[1].tx == 3);
  problems.clear();
  CHECK(!config::load("{\"mux\":[{\"tx\":99}]}", s, edited, problems) && has(problems, "mux.0.tx", "range"));
  problems.clear();
  CHECK(!config::load("{\"mux\":[{},{},{},{}]}", s, edited, problems) && has(problems, "mux", "invalid"));
  problems.clear();
  CHECK(!config::load("{\"ports\":[{\"cells_per_cycle\":\"all\"}]}", s, edited, problems) && has(problems, "ports.0.cells_per_cycle", "invalid"));
}

// ---- who has the line -------------------------------------------------------------------------------------------------------------------------

static void test_ports_take_turns() {
  Board board;
  board.devices[0] = voltronic_device();
  board.devices[1] = pylontech_device(2);
  // channel 2 is silent (nothing is connected), channel 3 is a second inverter
  board.devices[3] = voltronic_device();
  std::vector<MuxMember> members;
  members.push_back(member(0, 0, config::Kind::kVoltronic, "axpert-1", 5));
  members.push_back(member(1, 1, config::Kind::kPylontech, "us3000-1", 10));
  members.push_back(member(2, 2, config::Kind::kVoltronic, "axpert-silent", 5));
  members.push_back(member(3, 3, config::Kind::kVoltronic, "axpert-2", 5));
  MuxGroup group(std::move(members));
  Outcome out = run(group, board, 90);
  // every port that answers was read, again and again; the silent one did not stop the rest
  CHECK(out.by_port[0] >= 10 && out.by_port[1] >= 5 && out.by_port[3] >= 10 && out.by_port[2] == 0);
  CHECK(out.failed_by_port[2] >= 5 && out.failed_by_port[0] == 0 && out.failed_by_port[1] == 0 && out.failed_by_port[3] == 0);
  // what was asked on a channel is what that channel's equipment speaks: nothing of a battery on an inverter's channel, nothing of an inverter on the battery's
  bool clean = true;
  for (const auto& question : board.asked) {
    const bool console = question.second == "pwr" || question.second.rfind("bat ", 0) == 0 || question.second.rfind("info ", 0) == 0 || question.second.rfind("stat ", 0) == 0;
    if (question.first == 1 && !console) clean = false;
    if (question.first != 1 && console) clean = false;
  }
  CHECK(clean && !board.asked.empty());
  // the messages name the right device and come from the right port
  bool named = true;
  for (std::size_t i = 0; i < out.payloads.size(); ++i) {
    const std::size_t port = out.payload_ports[i];
    const std::string expected = port == 0 ? "axpert-1" : port == 1 ? "us3000-1" : "axpert-2";
    if (out.payloads[i].find("\"device\":\"" + expected + "\"") == std::string::npos) named = false;
  }
  CHECK(named && !out.payloads.empty());
  // the speed of the UART follows the port: it changes only when the next port needs another one, and the log of changes alternates between the two speeds
  bool speeds_ok = !board.bauds_set.empty();
  for (int b : board.bauds_set) if (b != 2400 && b != 115200) speeds_ok = false;
  for (std::size_t i = 1; i < board.bauds_set.size(); ++i) if (board.bauds_set[i] == board.bauds_set[i - 1]) speeds_ok = false;
  CHECK(speeds_ok && board.configures < board.selects);
  // the UART is emptied at the start of every turn
  CHECK(board.discards == group.turns() && group.turns() > 20);
}

static void test_a_stray_byte_does_no_harm() {
  Board board;
  board.noise_on_select = true;
  board.devices[0] = voltronic_device();
  board.devices[1] = pylontech_device(2);
  std::vector<MuxMember> members;
  members.push_back(member(0, 0, config::Kind::kVoltronic, "axpert-1", 5));
  members.push_back(member(1, 1, config::Kind::kPylontech, "us3000-1", 10));
  MuxGroup group(std::move(members));
  Outcome out = run(group, board, 60);
  CHECK(out.by_port[0] >= 8 && out.by_port[1] >= 4 && out.failures == 0);
}

static void test_polarity_and_speed() {
  // a TTL device wired through a MAX3232 arrives upside down: read with the port set to invert, silent without
  Board board;
  Device inverted = voltronic_device();
  inverted.inverted = true;
  board.devices[0] = inverted;
  {
    std::vector<MuxMember> members;
    members.push_back(member(0, 0, config::Kind::kVoltronic, "axpert-1", 5, /*invert=*/false));
    MuxGroup group(std::move(members));
    Outcome out = run(group, board, 30);
    CHECK(out.by_port[0] == 0 && out.failed_by_port[0] >= 3);
  }
  {
    Board other;
    other.devices[0] = inverted;
    std::vector<MuxMember> members;
    members.push_back(member(0, 0, config::Kind::kVoltronic, "axpert-1", 5, /*invert=*/true));
    MuxGroup group(std::move(members));
    Outcome out = run(group, other, 30);
    CHECK(out.by_port[0] >= 4 && out.failures == 0);
  }
  // a device at another speed than the port's is silent as well
  Board slow;
  Device odd = voltronic_device();
  odd.baud = 9600;
  slow.devices[0] = odd;
  std::vector<MuxMember> members;
  members.push_back(member(0, 0, config::Kind::kVoltronic, "axpert-1", 5));
  MuxGroup group(std::move(members));
  CHECK(run(group, slow, 30).by_port[0] == 0);
}

static void test_a_line_that_will_not_send_and_a_group_with_nothing_due() {
  Board board;
  board.fail_writes = true;
  board.devices[0] = voltronic_device();
  std::vector<MuxMember> members;
  members.push_back(member(0, 0, config::Kind::kVoltronic, "axpert-1", 5));
  MuxGroup group(std::move(members));
  Outcome out = run(group, board, 30);
  CHECK(out.by_port[0] == 0 && group.write_failures() >= 3 && out.failed_by_port[0] >= 3);
  // a group whose ports are all raw (or none) never takes the line for a request; an empty group just waits
  Board quiet;
  MuxGroup empty(std::vector<MuxMember>{});
  const MuxClock clock = quiet.clock();
  bool any = false;
  for (int i = 0; i < 100; ++i) any = any || empty.step(quiet, clock).active;
  CHECK(!any && quiet.now > 10'000 && quiet.asked.empty() && quiet.selects == 0);
}

static void test_a_raw_port_only_listens() {
  Board board;
  Device talker;
  talker.baud = 9600;
  std::vector<MuxMember> members;
  members.push_back(member(0, 0, config::Kind::kVoltronic, "axpert-1", 5));
  members.push_back(member(1, 1, config::Kind::kRaw, "sniffer", 0));
  board.devices[0] = voltronic_device();
  MuxGroup group(std::move(members));
  // the raw channel talks by itself now and then
  const MuxClock clock = board.clock();
  int readings = 0;
  std::uint64_t next_word = board.now;
  while (board.now < 40'000 + 10'000) {
    if (board.now >= next_word) { board.flights.push_back({board.now + 5, 1, bytes_of("hello inverter\r\n")}); next_word += 700; }
    const MuxTurn turn = group.step(board, clock);
    if (turn.reading) ++readings;
  }
  CHECK(readings >= 6 && group.members()[1].poller->raw().total() > 100);
  // it never sent anything on the raw channel
  for (const auto& question : board.asked) CHECK(question.first == 0);
  // and it does not hold the line long: about 1.5 s every 6 s, the rest is the inverter's
  CHECK(group.turns() > 10);
}

static void test_the_clock_of_a_reading() {
  Board board;
  board.devices[0] = voltronic_device();
  std::vector<MuxMember> members;
  members.push_back(member(0, 0, config::Kind::kVoltronic, "axpert-1", 5));
  MuxGroup group(std::move(members));
  Outcome out = run(group, board, 12);
  CHECK(out.readings >= 2 && out.payloads[0].find("\"timestamp_ms\":17000") != std::string::npos);
}

// ---- the LEDs ----------------------------------------------------------------------------------------------------------------------------------

struct RecordedPins {
  std::vector<std::string> events;
  void set_data(bool v) { events.push_back(v ? "D1" : "D0"); }
  void set_clock(bool v) { events.push_back(v ? "C1" : "C0"); }
  void set_latch(bool v) { events.push_back(v ? "L1" : "L0"); }
};

static void test_leds() {
  PortLeds leds;
  for (std::size_t i = 0; i < 8; ++i) leds.set_enabled(i, i != 5);          // port 6 is switched off
  CHECK(leds.pattern(1000) == 0);
  leds.good(0, 1000);
  CHECK(leds.pattern(1000) == 0x01 && leds.pattern(1099) == 0x01 && leds.pattern(1100) == 0);      // one pulse of 100 ms
  leds.good(7, 2000);
  CHECK(leds.pattern(2050) == 0x80);
  leds.bad(2, 3000);                                                        // blinks: on 100 ms, off 100 ms
  CHECK(leds.pattern(3000) == 0x04 && leds.pattern(3099) == 0x04 && leds.pattern(3100) == 0 && leds.pattern(3199) == 0 && leds.pattern(3200) == 0x04 && leds.pattern(4499) == 0x04 - (0x04 * ((4499 - 3000) / 100 % 2)));
  CHECK(leds.pattern(4500) == 0);                                            // 1.5 s later it stops
  // a port switched off never lights, even when told to
  leds.good(5, 5000);
  leds.bad(5, 5000);
  CHECK(leds.pattern(5000) == 0 && leds.pattern(5100) == 0);
  // a good reading after a failure ends the blinking; a failure after a good one ends the pulse
  leds.bad(1, 6000); leds.good(1, 6050);
  CHECK(leds.pattern(6150) == 0 && leds.pattern(6050) == 0x02);
  leds.good(3, 7000); leds.bad(3, 7010);
  CHECK(leds.pattern(7010) == 0x08 && leds.pattern(7110) == 0);
  leds.good(9, 1); leds.bad(12, 1); leds.set_enabled(9, true);               // out of range: nothing happens
  CHECK(leds.pattern(100000) == 0);
}

static void test_shift_out16() {
  RecordedPins pins;
  shift_out16(pins, 0x0281);   // LED 1, LED 8 and LED 10
  int clocks = 0, ones = 0;
  std::string data;
  for (const std::string& e : pins.events) { if (e == "C1") ++clocks; if (e == "D1") { ++ones; data += '1'; } else if (e == "D0") data += '0'; }
  CHECK(clocks == 16 && ones == 3 && data == "0000001010000001");   // sixteen bits, most significant first: the second register's far end first
  CHECK(pins.events.front() == "L0" && pins.events[pins.events.size() - 2] == "L1");
  PortLeds leds;
  leds.set_enabled(8, true); leds.set_enabled(9, true);
  leds.good(8, 1000); leds.good(9, 1000);
  CHECK(leds.pattern(1000) == 0x0300 && leds.pattern(1100) == 0);   // ports 9 and 10 are bits 8 and 9
}

static void test_shift_out() {
  RecordedPins pins;
  shift_out(pins, 0x81);   // LED 1 and LED 8
  // latch and clock low, then eight bits most significant first (LED 8's first), each with a clock pulse, then the latch pulse
  const std::vector<std::string> expected = {"L0", "C0", "D1", "C1", "C0", "D0", "C1", "C0", "D0", "C1", "C0", "D0", "C1", "C0", "D0", "C1", "C0", "D0", "C1", "C0", "D0", "C1", "C0", "D1", "C1", "C0", "L1", "L0"};
  CHECK(pins.events == expected);
  RecordedPins zero;
  shift_out(zero, 0x00);
  int ones = 0;
  for (const std::string& e : zero.events) if (e == "D1") ++ones;
  CHECK(ones == 0 && zero.events.size() == 28);
}

static void test_leds_follow_the_turns() {
  Board board;
  board.devices[0] = voltronic_device();          // port 1 answers, port 2 does not, port 3 is off
  std::vector<MuxMember> members;
  members.push_back(member(0, 0, config::Kind::kVoltronic, "axpert-1", 5));
  members.push_back(member(1, 1, config::Kind::kVoltronic, "axpert-2", 5));
  MuxGroup group(std::move(members));
  PortLeds leds;
  leds.set_enabled(0, true);
  leds.set_enabled(1, true);
  const MuxClock clock = board.clock();
  int good_seen = 0, bad_seen = 0;
  while (board.now < 40'000) {
    const MuxTurn turn = group.step(board, clock);
    if (turn.reading) { leds.good(turn.port, board.now); if ((leds.pattern(board.now) & 0x01) != 0) ++good_seen; }
    if (turn.ended && turn.failed) { leds.bad(turn.port, board.now); if ((leds.pattern(board.now) & 0x02) != 0) ++bad_seen; }
    CHECK((leds.pattern(board.now) & 0xFFFC) == 0);
  }
  CHECK(good_seen >= 5 && bad_seen >= 3);
}

// ---- the cells of a stack, in rotation --------------------------------------------------------------------------------------------------------

struct StackBench {
  Poller poller;
  int modules;
  std::uint64_t now = 0;
  std::vector<std::string> asked;
  std::vector<std::vector<int>> cycles;      // the modules whose cells were asked, cycle by cycle
  std::vector<std::string> payloads;
  StackBench(int modules_, int cells_per_cycle) : poller(config::Kind::kPylontech, "solar-1", "us3000-1", 10, 0, "auto", cells_per_cycle), modules(modules_) {}
  void run(std::uint64_t until_ms) {
    std::vector<std::pair<std::uint64_t, std::vector<std::uint8_t>>> in_flight;
    for (; now < until_ms; now += 10) {
      for (auto it = in_flight.begin(); it != in_flight.end();) {
        if (it->first <= now) { poller.on_rx(it->second.data(), it->second.size(), now); it = in_flight.erase(it); } else ++it;
      }
      const std::vector<std::uint8_t> out = poller.next_tx(now);
      if (!out.empty()) {
        const std::string command = command_of(out);
        asked.push_back(command);
        if (command == "pwr") cycles.emplace_back();
        std::string text;
        if (command == "pwr") text = pwr_table(modules);
        else if (command.rfind("bat ", 0) == 0) { const int n = std::atoi(command.c_str() + 4); cycles.back().push_back(n); text = bat_table(n * 3); }
        else if (command.rfind("info ", 0) == 0) text = kInfo;
        else if (command.rfind("stat ", 0) == 0) text = kStat;
        if (!text.empty()) in_flight.push_back({now + 40, bytes_of(text)});
      }
      std::string topic, payload;
      if (poller.take_message(1700000000000ULL + now, topic, payload)) payloads.push_back(payload);
    }
  }
};
static int modules_with_cells(const std::string& payload) {
  json::Value doc;
  if (!json::parse(payload, doc)) return -1;
  const json::Value* modules = doc.get("stack");
  if (modules == nullptr || !modules->is_array()) return -2;
  int n = 0;
  for (const json::Value& m : modules->items) { const json::Value* cells = m.get("cells_v"); if (cells != nullptr && cells->is_array() && !cells->items.empty()) ++n; }
  return n;
}

static void test_cells_in_rotation() {
  // eight modules, two per cycle: the first cycle reads them all (nothing is known yet), then every cycle reads the next two in order
  StackBench rotating(8, 2);
  rotating.run(110'000);
  CHECK(rotating.cycles.size() >= 8);
  CHECK(rotating.cycles[0] == std::vector<int>({1, 2, 3, 4, 5, 6, 7, 8}));
  CHECK(rotating.cycles[1] == std::vector<int>({1, 2}) || rotating.cycles[1] == std::vector<int>({3, 4}));
  // afterwards each cycle asks exactly two modules and they follow one another round the stack
  std::vector<int> order;
  for (std::size_t c = 1; c < rotating.cycles.size(); ++c) {
    CHECK(rotating.cycles[c].size() == 2);
    for (int n : rotating.cycles[c]) order.push_back(n);
  }
  bool consecutive = order.size() >= 8;
  for (std::size_t i = 1; i < order.size(); ++i) if (order[i] != order[i - 1] % 8 + 1) consecutive = false;
  CHECK(consecutive);
  // every message still carries the cells of all eight modules: the ones not read keep those of their last turn
  CHECK(rotating.payloads.size() >= 8);
  for (const std::string& payload : rotating.payloads) {
    CHECK(modules_with_cells(payload) == 8);
    if (modules_with_cells(payload) != 8) { std::printf("cells=%d %.700s\n", modules_with_cells(payload), payload.c_str()); break; }
  }
  // fewer requests per cycle than reading everything: the point of it
  StackBench everything(8, 0);
  everything.run(110'000);
  CHECK(everything.cycles.size() >= 8 && everything.cycles[3].size() == 8);
  std::size_t rotating_bats = 0, all_bats = 0;
  for (const std::string& a : rotating.asked) if (a.rfind("bat ", 0) == 0) ++rotating_bats;
  for (const std::string& a : everything.asked) if (a.rfind("bat ", 0) == 0) ++all_bats;
  CHECK(rotating_bats * 2 < all_bats + 16);
  // a number that covers the stack (or more) is the same as reading everything
  StackBench wide(3, 5);
  wide.run(45'000);
  CHECK(wide.cycles.size() >= 3 && wide.cycles[2].size() == 3);
  // and one module per cycle goes round a stack of three
  StackBench one(3, 1);
  one.run(65'000);
  CHECK(one.cycles.size() >= 5 && one.cycles[0].size() == 3);
  bool ones = true;
  for (std::size_t c = 1; c < one.cycles.size(); ++c) if (one.cycles[c].size() != 1) ones = false;
  CHECK(ones && one.cycles[1][0] % 3 + 1 == one.cycles[2][0] && one.cycles[2][0] % 3 + 1 == one.cycles[3][0]);
}

static void test_poller_says_when_it_is_busy() {
  Poller p(config::Kind::kVoltronic, "solar-1", "axpert-1", 5, 0, "pi30");
  CHECK(!p.busy() && p.due(0) && !p.ready());
  const std::vector<std::uint8_t> first = p.next_tx(0);
  CHECK(!first.empty() && p.busy() && !p.due(10));
  Poller raw(config::Kind::kRaw, "solar-1", "sniffer", 0, 0);
  CHECK(!raw.busy() && !raw.due(100000));
}

int main() {
  test_profile_settings();
  test_profile_document();
  test_ports_take_turns();
  test_a_stray_byte_does_no_harm();
  test_polarity_and_speed();
  test_a_line_that_will_not_send_and_a_group_with_nothing_due();
  test_a_raw_port_only_listens();
  test_the_clock_of_a_reading();
  test_leds();
  test_shift_out();
  test_shift_out16();
  test_leds_follow_the_turns();
  test_cells_in_rotation();
  test_poller_says_when_it_is_busy();
  std::printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}

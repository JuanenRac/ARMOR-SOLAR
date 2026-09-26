// ARMOR-SOLAR - host tests of the s3-eth board profile (the Waveshare ESP32-S3-ETH): its pin table, the ports' default pins, the Ethernet settings and the layouts
// of its network. It is built with ARMOR_BOARD_S3_ETH defined (tests/CMakeLists.txt); test_node.cpp covers the s3-wifi profile.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
#include <cstdio>
#include <string>

#include "../core/netplan.hpp"
#include "../core/solar_config.hpp"

#ifndef ARMOR_BOARD_S3_ETH
#error "this test is for the s3-eth profile: build it with -DARMOR_BOARD_S3_ETH=1"
#endif

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

static bool has(const config::Problems& problems, const std::string& path, const std::string& code) {
  for (const config::Problem& p : problems) if (p.path == path && p.code == code) return true;
  return false;
}

static config::Settings wired_settings() {
  config::Settings s = config::default_settings("a1b2c3");
  return s;   // the s3-eth board starts on its cable, with DHCP: no Wi-Fi needed
}

static void test_profile_and_pins() {
  CHECK(board::kHasEthernet && std::string(board::kId) == "s3-eth");
  // the W5500 (GPIO 9..14) and the camera connector (8) are never offered; the flash, the PSRAM and the USB stay reserved
  for (int gpio = 9; gpio <= 14; ++gpio) CHECK(!board::assignable(gpio) && board::pin_info(gpio).reason == board::Reserved::kEthernet);
  CHECK(!board::assignable(8) && board::pin_info(8).reason == board::Reserved::kCamera);
  for (int gpio : {26, 30, 33, 35, 37, 19, 20, 22, 49, -1}) CHECK(!board::assignable(gpio));
  // the microSD socket, the strapping pins and the boot button come with a warning; the others are free
  for (int gpio = 4; gpio <= 7; ++gpio) CHECK(board::assignable(gpio) && std::string(board::pin_info(gpio).note) == "sdcard");
  CHECK(std::string(board::pin_info(0).note) == "boot" && std::string(board::pin_info(45).note) == "strapping" && std::string(board::pin_info(3).note) == "strapping");
  for (int gpio : {1, 2, 15, 16, 17, 18, 21, 38, 39, 40, 41, 42, 43, 44, 47, 48}) CHECK(board::assignable(gpio) && board::pin_info(gpio).use == board::PinUse::kFree);
  CHECK(board::kPortCount == 10 && board::kHardwarePorts + board::kSoftPorts == board::kPortCount);
}

static void test_default_pins_are_all_usable() {
  // every default pin of every port is assignable, and no two ports share one: all ten ports can be enabled at once
  config::Settings s = wired_settings();
  for (std::size_t i = 0; i < s.ports.size(); ++i) {
    const board::DefaultPins pins = board::kDefaultPins[i];
    CHECK(board::assignable(pins.rx) && board::assignable(pins.tx) && (pins.de < 0 || board::assignable(pins.de)));
    CHECK(s.ports[i].rx == pins.rx && s.ports[i].tx == pins.tx && s.ports[i].de == pins.de);
    s.ports[i].enabled = true;
    s.ports[i].kind = i % 2 ? "ant" : "voltronic";
    s.ports[i].name = "device-" + std::to_string(i + 1);
  }
  CHECK(config::validate(s).empty());
  // the pins the W5500 uses are refused for a port
  s.ports[0].rx = 11;
  CHECK(has(config::validate(s), "ports.0.rx", "reserved"));
  s.ports[0].rx = 16; s.ports[1].de = 8;
  CHECK(has(config::validate(s), "ports.1.de", "reserved"));
}

static void test_ethernet_settings() {
  config::Settings s = wired_settings();
  CHECK(s.uplink == config::Uplink::kEthernet && s.ip.dhcp);
  CHECK(config::validate(s).empty());               // a node on the cable needs no Wi-Fi at all
  // a fixed address is checked as a whole
  s.ip.dhcp = false;
  CHECK(has(config::validate(s), "ip.address", "required") && has(config::validate(s), "ip.gateway", "required"));
  s.ip.address = "192.168.0.181"; s.ip.gateway = "192.168.0.1"; s.ip.dns1 = "192.168.0.1";
  CHECK(config::validate(s).empty());
  s.ip.gateway = "10.0.0.1";
  CHECK(has(config::validate(s), "ip.gateway", "outside_subnet"));
  s.ip.gateway = "192.168.0.181";
  CHECK(has(config::validate(s), "ip.gateway", "conflict"));
  s.ip.gateway = "192.168.0.1"; s.ip.address = "192.168.0.255";
  CHECK(has(config::validate(s), "ip.address", "invalid"));
  s.ip.address = "192.168.0.181"; s.ip.netmask = "255.0.255.0";
  CHECK(has(config::validate(s), "ip.netmask", "invalid"));
  s.ip.netmask = "255.255.255.0"; s.ip.dns2 = "not-an-address";
  CHECK(has(config::validate(s), "ip.dns2", "invalid"));
  s.ip.dns2 = "";
  CHECK(config::validate(s).empty());
  // Wi-Fi as the way in is still possible on this board, and then it needs a station or an access point
  s.uplink = config::Uplink::kWifi;
  CHECK(has(config::validate(s), "sta.enabled", "required"));
  s.sta.enabled = true; s.sta.ssid = "casa"; s.sta.password = "una-clave-larga";
  CHECK(config::validate(s).empty());
  // the settings document carries the way in and the address, and a document from the panel changes them
  config::Settings wired = wired_settings();
  wired.ip.dhcp = false; wired.ip.address = "192.168.0.181"; wired.ip.gateway = "192.168.0.1";
  config::Settings back;
  config::Problems problems;
  CHECK(config::load(config::to_json(wired, true), config::default_settings("000000"), back, problems) && back.uplink == config::Uplink::kEthernet && !back.ip.dhcp && back.ip.address == "192.168.0.181");
  CHECK(config::to_json(wired, false).find("\"uplink\":\"ethernet\"") != std::string::npos);
  problems.clear();
  CHECK(!config::load("{\"uplink\":\"carrier-pigeon\"}", wired, back, problems) && has(problems, "uplink", "invalid"));
}

static void test_layouts() {
  config::Settings s = wired_settings();
  // a node that has no user yet opens its setup network AND keeps the cable: it can be set up from either
  netplan::Plan plan = netplan::plan_network(s, true, "CODE1234", "a1b2c3", 5);
  CHECK(plan.layout == netplan::Layout::kSetupAp && plan.ap.setup && plan.wired && !plan.station);
  // on the cable, with no access point: only the cable
  plan = netplan::plan_network(s, false, "", "a1b2c3", 5);
  CHECK(plan.layout == netplan::Layout::kEthernet && plan.wired && !plan.station && !plan.ap.enabled && std::string(netplan::to_text(plan.layout)) == "ethernet");
  // the cable and an access point of its own
  s.ap.enabled = true; s.ap.ssid = "ARMOR-SOLAR-A1B2C3"; s.ap.password = "otra-clave-larga";
  plan = netplan::plan_network(s, false, "", "a1b2c3", 5);
  CHECK(plan.layout == netplan::Layout::kEthernetWithAp && plan.ap.enabled && plan.ap.channel == 11 && std::string(netplan::to_text(plan.layout)) == "ethernet+ap");
  // a station is ignored while the cable is the way in, even if the settings still hold one
  s.sta.enabled = true; s.sta.ssid = "casa";
  plan = netplan::plan_network(s, false, "", "a1b2c3", 5);
  CHECK(!plan.station && plan.layout == netplan::Layout::kEthernetWithAp);
  // on Wi-Fi the layouts are the ones of the other board
  s.uplink = config::Uplink::kWifi;
  plan = netplan::plan_network(s, false, "", "a1b2c3", 5);
  CHECK(!plan.wired && plan.station && plan.layout == netplan::Layout::kStationWithAp);
}

int main() {
  test_profile_and_pins();
  test_default_pins_are_all_usable();
  test_ethernet_settings();
  test_layouts();
  std::printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}

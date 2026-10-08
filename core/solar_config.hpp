// ARMOR-SOLAR - the settings of a solar gateway node: what the web panel edits, what is stored in flash and what the firmware obeys.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
//
// One document (JSON) holds everything that differs from node to node, so the same firmware image serves every node and nothing secret has to be
// compiled in. This file only reads, checks and writes that document; it touches no hardware, so all of it is tested on a computer. Passwords are never
// written back to the panel: a section sent without a password (or with an empty one) keeps the stored one, and "password_set" tells the panel that
// there is one.
//
// A node has ten serial ports: three hardware UARTs and seven emulated ones (armor::board). A port can read an inverter of the Voltronic / MPP Solar
// family, a Pylontech battery through its console, or just show what arrives ("raw", to look at a protocol that is not decoded yet). A node may read only
// inverters, only batteries or a mix: each port is independent.
#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "board_s3.hpp"
#include "json.hpp"
#include "net_text.hpp"
#include "node_id.hpp"

namespace armor::config {

constexpr int kVersion = 1;
constexpr std::size_t kMaxPasswordText = 64;
constexpr std::size_t kPortCount = static_cast<std::size_t>(board::kPortCount);
// The "mux" profile: a base board with 74HC4052 multiplexers in front of the UARTs, so that up to three hardware UARTs serve up to eight ports (4, 2 and 2 in the default wiring).
constexpr std::size_t kMuxGroups = 3;
constexpr std::size_t kMuxPorts = 8;

enum class WifiSecurity { kOpen, kWpa2, kWpa3, kWpa2Wpa3 };
// The panel over plain HTTP only, over HTTP and HTTPS (a certificate the node made for itself), or over HTTPS only (port 80 sends the browser to HTTPS).
enum class WebMode { kHttp, kBoth, kHttps };
enum class BleMode { kOff, kSetup, kAlways };   // when the node listens to a phone over Bluetooth
// How the node reaches the network: over the Ethernet cable (only the s3-eth board has one) or as a Wi-Fi station.
enum class Uplink { kWifi, kEthernet };

// The address of the Ethernet port: DHCP, or a fixed address, mask, gateway and DNS.
struct IpSettings {
  bool dhcp = true;
  std::string address, netmask = "255.255.255.0", gateway, dns1, dns2;
};

struct AccessPoint {
  bool enabled = false;
  std::string ssid = "ARMOR-SOLAR";
  WifiSecurity security = WifiSecurity::kWpa2;
  std::string password;
  int channel = 0;  // 0: one of 1, 6 or 11 chosen from the node's MAC
  bool hidden = false;
  int max_clients = 8;
  int tx_power_dbm = 15;
  int bandwidth_mhz = 20;
  std::string country = "ES";  // two capital letters: sets which channels and how much power the radio may use
};

struct Network { std::string ssid, password; };
constexpr std::size_t kMaxBackupNetworks = 3;

struct Station {
  bool enabled = false;
  std::string ssid, password;
  // Tried in order, after the network above, whenever the current one cannot be joined for a while (main/network.cpp); never while the
  // network above still works. The same Wi-Fi password rules apply to each.
  std::vector<Network> backup;
};

struct Broker { std::string uri, username, password; };
constexpr std::size_t kMaxBackupBrokers = 2;

struct Mqtt {
  bool enabled = false;  // a node that was never configured has no broker yet
  std::string uri, username, password;
  int heartbeat_s = 10;  // the keep-alive of the connection is twice this
  // Tried in order, after the broker above, whenever it cannot be reached for a while (main/mqtt_link.cpp); never while it still works.
  std::vector<Broker> backup;
};

// What a port reads.
enum class Kind { kVoltronic, kPylontech, kAnt, kRaw };
struct KindInfo {
  Kind kind;
  const char* id;
  int default_baud;
  int default_poll_s;
  bool needs_tx;     // the node has to send a request; a "raw" port only listens
  bool publishes;    // it produces solar messages for the server
};
constexpr KindInfo kKinds[] = {
    {Kind::kVoltronic, "voltronic", 2400, 5, true, true},
    {Kind::kPylontech, "pylontech", 115200, 10, true, true},
    {Kind::kAnt, "ant", 19200, 5, true, true},
    {Kind::kRaw, "raw", 9600, 0, false, false},
};
inline bool kind_from_text(std::string_view text, Kind& out) {
  for (const KindInfo& info : kKinds) if (text == info.id) { out = info.kind; return true; }
  return false;
}
inline const KindInfo& info_of(Kind kind) {
  for (const KindInfo& info : kKinds) if (info.kind == kind) return info;
  return kKinds[0];
}

struct PortConfig {
  bool enabled = false;
  std::string kind = "voltronic";
  std::string name;   // the device name in the topic (armor/solar/<node>/<name>/state); for a raw port only a label
  int baud = 0;       // 0: the kind's own speed
  int rx = -1;        // the GPIO that receives the equipment's TX line
  int tx = -1;        // the GPIO that sends to the equipment's RX line (not needed by a raw port)
  int de = -1;        // an RS485 transceiver's driver-enable (and receiver-enable) pin, high while sending; -1: RS232 or none
  int poll_s = 0;     // seconds between two readings; 0: the kind's own
  int modules = 0;    // Pylontech: how many modules to ask cell by cell (0: all that answer, up to 8)
  std::string dialect = "auto";   // inverter: "auto" (looks for it), "pi30", "revo" or "pi18"; the other kinds ignore it
  bool invert = false;            // the signals of the line arrive upside down (a device with TTL levels wired through a MAX3232 does): the UART flips them
  int cells_per_cycle = 0;        // Pylontech: the cells of this many modules per cycle, in rotation (0: every module every cycle); the others keep their last cells
  bool pv2 = false;               // inverter (standard dialect): also ask the second PV input (QPIGS2)
  int parallel = 0;               // inverter (standard dialect): also ask this many units of a parallel system (QPGS0 to QPGS<n-1>); 0: none
};

// One group of the mux profile: a hardware UART (TX, RX) in front of which a 74HC4052 chooses the port with two select pins.
struct MuxGroup {
  int tx = -1, rx = -1;
  int s0 = -1;
  int s1 = -1;          // -1: tied to ground on the board (at most two ports in the group)
  int channels = 2;     // how many ports the group serves (1 to 4)
};
// The 74HC595 that lights one LED per port: the pin of its serial data, of its shift clock and of its latch. All -1: no LEDs.
struct LedPins {
  int data = -1, clock = -1, latch = -1;
  bool any() const { return data >= 0 || clock >= 0 || latch >= 0; }
};

// What time the node believes it is: a time server on the Internet (or the browser's clock when that is off) and the zone the local time is
// shown in. The zone is a POSIX TZ rule, so summer time changes by itself ("CET-1CEST,M3.5.0,M10.5.0/3" is Spain); "UTC0" is no offset.
struct Clock {
  bool ntp_enabled = true;
  std::string ntp = "pool.ntp.org";
  std::string zone = "UTC0";
};

struct Settings {
  std::string node_id;
  std::string node_name;
  Uplink uplink = board::kHasEthernet ? Uplink::kEthernet : Uplink::kWifi;   // the board's own way in until the panel says otherwise
  IpSettings ip;
  std::string hostname;  // empty: "armor-" + the node id
  AccessPoint ap;
  Station sta;
  Mqtt mqtt;
  Clock time;
  std::array<PortConfig, kPortCount> ports;
  std::string profile = "direct";     // "direct": every port has its own pins; "mux": the eight ports of the base board with multiplexers
  std::array<MuxGroup, kMuxGroups> mux;
  LedPins leds;
  WebMode web = WebMode::kBoth;
  BleMode ble = BleMode::kSetup;   // "setup": only while the node has no user; "always"; "off": the Bluetooth stack is not even started
  std::string language = "en";
  // A periodic, unconditional restart (disconnect_before_restart() then esp_restart()), independent of any fault: 0 means never. One of
  // {0, 1, 2, 3, 4, 6, 12, 24, 48} hours (auto_restart_hours_is_valid()).
  int auto_restart_hours = 0;
};

struct Problem {
  std::string path;  // "ports.2.baud"
  std::string code;  // "required", "too_long", "range", "invalid", "conflict", "reserved" ...
};
using Problems = std::vector<Problem>;

inline const char* to_text(WifiSecurity v) {
  switch (v) { case WifiSecurity::kOpen: return "open"; case WifiSecurity::kWpa2: return "wpa2"; case WifiSecurity::kWpa3: return "wpa3"; case WifiSecurity::kWpa2Wpa3: return "wpa2wpa3"; }
  return "wpa2";
}
inline const char* to_text(Uplink v) { return v == Uplink::kEthernet ? "ethernet" : "wifi"; }
inline const char* to_text(BleMode v) { return v == BleMode::kAlways ? "always" : v == BleMode::kOff ? "off" : "setup"; }
inline const char* to_text(WebMode v) { return v == WebMode::kHttps ? "https" : v == WebMode::kHttp ? "http" : "both"; }

// Whether a port is a hardware UART or an emulated one, from its number (0 to 9).
constexpr bool is_soft_port(std::size_t index) { return index >= static_cast<std::size_t>(board::kHardwarePorts); }

// The speeds a port may use: a hardware UART any common one; an emulated one only the slow ones (it is timed by software: 19200 is the limit).
inline const std::vector<int>& allowed_bauds(bool soft) {
  static const std::vector<int> hardware{1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200, 230400, 460800};
  static const std::vector<int> emulated{1200, 2400, 4800, 9600, 19200};
  return soft ? emulated : hardware;
}
inline bool profile_is_mux(const std::string& profile) { return profile == "mux"; }

// Where port `index` (0 to 7) sits in the mux profile: its group and its channel there. False when the groups have no such port.
struct MuxSlot { std::size_t group = 0, channel = 0; };
inline bool mux_slot_of(const std::array<MuxGroup, kMuxGroups>& groups, std::size_t index, MuxSlot& out) {
  std::size_t first = 0;
  for (std::size_t g = 0; g < kMuxGroups; ++g) {
    const std::size_t size = groups[g].channels >= 1 && groups[g].channels <= 4 ? static_cast<std::size_t>(groups[g].channels) : 0;
    if (index < first + size) { out.group = g; out.channel = index - first; return true; }
    first += size;
  }
  return false;
}
// How many ports the groups serve in all.
inline std::size_t mux_port_count(const std::array<MuxGroup, kMuxGroups>& groups) {
  std::size_t n = 0;
  for (const MuxGroup& g : groups) n += g.channels >= 1 && g.channels <= 4 ? static_cast<std::size_t>(g.channels) : 0;
  return n;
}

inline int effective_baud(const PortConfig& port) {
  Kind kind;
  return port.baud != 0 || !kind_from_text(port.kind, kind) ? port.baud : info_of(kind).default_baud;
}
inline int effective_poll_s(const PortConfig& port) {
  Kind kind;
  return port.poll_s != 0 || !kind_from_text(port.kind, kind) ? port.poll_s : info_of(kind).default_poll_s;
}

// ---- the defaults of a node that has never been configured ---------------------------------------------------------------------

// `mac_tail` is the last three bytes of the node's MAC as six lowercase hexadecimal digits: it makes the first identity unique.
inline Settings default_settings(std::string_view mac_tail) {
  Settings s;
  s.node_id = "solar-" + std::string(mac_tail);
  s.node_name = s.node_id;
  for (std::size_t i = 0; i < kPortCount; ++i) {
    s.ports[i].rx = board::kDefaultPins[i].rx;
    s.ports[i].tx = board::kDefaultPins[i].tx;
    s.ports[i].de = board::kDefaultPins[i].de;
    s.ports[i].name = "port" + std::to_string(i + 1);
  }
  // The wiring of the base board (ESP32-S3-WROOM-1 N16R8 or the Waveshare ESP32-S3-ETH; none of these is a reserved pin on either): three UARTs and their multiplexers, and the LEDs' shift register.
  s.mux[0] = {15, 16, 17, 18, 4};
  s.mux[1] = {1, 2, 38, -1, 2};
  s.mux[2] = {40, 41, 42, -1, 2};
  s.leds = {21, 39, 47};
  return s;
}

inline int effective_channel(const AccessPoint& ap, unsigned mac_sum) {
  if (ap.channel >= 1 && ap.channel <= 13) return ap.channel;
  constexpr int kNonOverlapping[3] = {1, 6, 11};
  return kNonOverlapping[mac_sum % 3];
}

// ---- reading -------------------------------------------------------------------------------------------------------------------

namespace detail {
template <typename E>
bool read_choice(const json::Value& parent, const char* name, std::initializer_list<std::pair<const char*, E>> options, E& target) {
  const json::Value* member = parent.get(name);
  if (member == nullptr) return true;
  if (!member->is_string()) return false;
  for (const auto& option : options) if (member->text == option.first) { target = option.second; return true; }
  return false;
}

inline void bad(Problems& problems, std::string path, const char* code) { problems.push_back({std::move(path), code}); }

inline void read_text(const json::Value& parent, const char* name, std::string& target, std::size_t longest, const std::string& path, Problems& problems) {
  const json::Value* member = parent.get(name);
  if (member == nullptr) return;
  if (!member->is_string()) { bad(problems, path, "invalid"); return; }
  if (member->text.size() > longest) { bad(problems, path, "too_long"); return; }
  target = member->text;
}

// A secret: absent or empty keeps the stored one; "<name>_clear": true erases it.
inline void read_secret(const json::Value& parent, const char* name, std::string& target, const std::string& path, Problems& problems) {
  if (parent.bool_or(std::string(name) + "_clear", false)) { target.clear(); return; }
  const json::Value* member = parent.get(name);
  if (member == nullptr) return;
  if (!member->is_string()) { bad(problems, path, "invalid"); return; }
  if (member->text.empty()) return;
  if (member->text.size() > kMaxPasswordText) { bad(problems, path, "too_long"); return; }
  target = member->text;
}

inline void read_bool(const json::Value& parent, const char* name, bool& target, const std::string& path, Problems& problems) {
  const json::Value* member = parent.get(name);
  if (member == nullptr) return;
  if (!member->is_bool()) { bad(problems, path, "invalid"); return; }
  target = member->boolean;
}

inline void read_int(const json::Value& parent, const char* name, int& target, long long lowest, long long highest, const std::string& path, Problems& problems) {
  const json::Value* member = parent.get(name);
  if (member == nullptr) return;
  if (!member->is_number()) { bad(problems, path, "invalid"); return; }
  const long long value = parent.integer_or(name, lowest - 1, lowest, highest);
  if (value < lowest || value > highest) { bad(problems, path, "range"); return; }
  target = static_cast<int>(value);
}
}  // namespace detail

// Applies the members present in `document` on top of `settings`; whatever the document omits stays as it was. The result is checked
// with validate(): read_settings() only reports a member of the wrong type or size.
inline void read_settings(const json::Value& document, Settings& s, Problems& problems) {
  using namespace detail;
  if (!document.is_object()) { bad(problems, "", "invalid"); return; }
  if (const json::Value* node = document.get("node"); node != nullptr && node->is_object()) {
    read_text(*node, "id", s.node_id, kMaxNodeIdLength, "node.id", problems);
    read_text(*node, "name", s.node_name, 48, "node.name", problems);
    read_text(*node, "hostname", s.hostname, 32, "node.hostname", problems);
  }
  if (document.get("uplink") != nullptr && !read_choice<Uplink>(document, "uplink", {{"wifi", Uplink::kWifi}, {"ethernet", Uplink::kEthernet}}, s.uplink)) bad(problems, "uplink", "invalid");
  if (const json::Value* ip = document.get("ip"); ip != nullptr && ip->is_object()) {
    read_bool(*ip, "dhcp", s.ip.dhcp, "ip.dhcp", problems);
    read_text(*ip, "address", s.ip.address, 15, "ip.address", problems);
    read_text(*ip, "netmask", s.ip.netmask, 15, "ip.netmask", problems);
    read_text(*ip, "gateway", s.ip.gateway, 15, "ip.gateway", problems);
    read_text(*ip, "dns1", s.ip.dns1, 15, "ip.dns1", problems);
    read_text(*ip, "dns2", s.ip.dns2, 15, "ip.dns2", problems);
  }
  if (const json::Value* ap = document.get("ap"); ap != nullptr && ap->is_object()) {
    read_bool(*ap, "enabled", s.ap.enabled, "ap.enabled", problems);
    read_text(*ap, "ssid", s.ap.ssid, 32, "ap.ssid", problems);
    if (!read_choice<WifiSecurity>(*ap, "security", {{"open", WifiSecurity::kOpen}, {"wpa2", WifiSecurity::kWpa2}, {"wpa3", WifiSecurity::kWpa3}, {"wpa2wpa3", WifiSecurity::kWpa2Wpa3}}, s.ap.security)) bad(problems, "ap.security", "invalid");
    read_secret(*ap, "password", s.ap.password, "ap.password", problems);
    read_int(*ap, "channel", s.ap.channel, 0, 13, "ap.channel", problems);
    read_bool(*ap, "hidden", s.ap.hidden, "ap.hidden", problems);
    read_int(*ap, "max_clients", s.ap.max_clients, 1, 10, "ap.max_clients", problems);
    read_int(*ap, "tx_power_dbm", s.ap.tx_power_dbm, 2, 20, "ap.tx_power_dbm", problems);
    read_int(*ap, "bandwidth_mhz", s.ap.bandwidth_mhz, 20, 40, "ap.bandwidth_mhz", problems);
    read_text(*ap, "country", s.ap.country, 2, "ap.country", problems);
  }
  if (const json::Value* sta = document.get("sta"); sta != nullptr && sta->is_object()) {
    read_bool(*sta, "enabled", s.sta.enabled, "sta.enabled", problems);
    read_text(*sta, "ssid", s.sta.ssid, 32, "sta.ssid", problems);
    read_secret(*sta, "password", s.sta.password, "sta.password", problems);
    if (const json::Value* backup = sta->get("backup"); backup != nullptr) {
      if (!backup->is_array()) bad(problems, "sta.backup", "invalid");
      // A "backup" the document sends replaces the stored list, never adds to it - a save after removing one in the panel must not
      // leave the one just removed behind (found for real: deleting backup brokers and saving brought them straight back).
      else {
        // The panel never holds a stored password (it only learns that there is one), so an entry that arrives without one is the same
        // network as before and keeps its password; one with a new name is a different network and starts without.
        const std::vector<Network> before = std::move(s.sta.backup);
        s.sta.backup.clear();
        // More than fit is never fatal: an older or hand-edited document with extra entries loses only the ones past the limit, not
        // the whole node (a broker and the rest of the settings have nothing to do with how many backup networks were once saved).
        for (std::size_t i = 0; i < backup->items.size() && i < kMaxBackupNetworks; ++i) {
          const json::Value& item = backup->items[i];
          const std::string base = "sta.backup." + std::to_string(i) + ".";
          Network network;
          if (!item.is_object()) { bad(problems, base + "ssid", "invalid"); continue; }
          read_text(item, "ssid", network.ssid, 32, base + "ssid", problems);
          for (const Network& old : before) if (old.ssid == network.ssid) { network.password = old.password; break; }
          read_secret(item, "password", network.password, base + "password", problems);
          s.sta.backup.push_back(network);
        }
      }
    }
  }
  if (const json::Value* mqtt = document.get("mqtt"); mqtt != nullptr && mqtt->is_object()) {
    read_bool(*mqtt, "enabled", s.mqtt.enabled, "mqtt.enabled", problems);
    read_text(*mqtt, "uri", s.mqtt.uri, 160, "mqtt.uri", problems);
    read_text(*mqtt, "username", s.mqtt.username, 64, "mqtt.username", problems);
    read_secret(*mqtt, "password", s.mqtt.password, "mqtt.password", problems);
    read_int(*mqtt, "heartbeat_s", s.mqtt.heartbeat_s, 2, 300, "mqtt.heartbeat_s", problems);
    read_text(*mqtt, "ntp", s.time.ntp, 64, "mqtt.ntp", problems);   // where older documents kept it
    if (const json::Value* backup = mqtt->get("backup"); backup != nullptr) {
      if (!backup->is_array()) bad(problems, "mqtt.backup", "invalid");
      // A "backup" the document sends replaces the stored list, never adds to it (see sta.backup above - the same bug, found on the
      // broker page: removing backup brokers and saving brought them straight back).
      else {
        // Same as the backup networks: an entry that arrives without a password is the same account as before (same user on the same
        // slot or the same address) and keeps it.
        const std::vector<Broker> before = std::move(s.mqtt.backup);
        s.mqtt.backup.clear();
        // Same as sta.backup above: more entries than fit just lose the extras, not the rest of the node's settings.
        for (std::size_t i = 0; i < backup->items.size() && i < kMaxBackupBrokers; ++i) {
          const json::Value& item = backup->items[i];
          const std::string base = "mqtt.backup." + std::to_string(i) + ".";
          Broker broker;
          if (!item.is_object()) { bad(problems, base + "uri", "invalid"); continue; }
          read_text(item, "uri", broker.uri, 160, base + "uri", problems);
          read_text(item, "username", broker.username, 64, base + "username", problems);
          for (std::size_t k = 0; k < before.size(); ++k) {
            if (before[k].username == broker.username && (k == i || before[k].uri == broker.uri)) { broker.password = before[k].password; break; }
          }
          read_secret(item, "password", broker.password, base + "password", problems);
          s.mqtt.backup.push_back(broker);
        }
      }
    }
  }
  if (const json::Value* ports = document.get("ports"); ports != nullptr) {
    if (!ports->is_array() || ports->items.size() > kPortCount) bad(problems, "ports", "invalid");
    else for (std::size_t i = 0; i < ports->items.size(); ++i) {
      const json::Value& item = ports->items[i];
      const std::string base = "ports." + std::to_string(i) + ".";
      if (!item.is_object()) { bad(problems, base + "enabled", "invalid"); continue; }
      PortConfig& port = s.ports[i];
      read_bool(item, "enabled", port.enabled, base + "enabled", problems);
      read_text(item, "kind", port.kind, 12, base + "kind", problems);
      read_text(item, "name", port.name, 32, base + "name", problems);
      read_int(item, "baud", port.baud, 0, 921600, base + "baud", problems);
      read_int(item, "rx", port.rx, -1, board::kLastGpio, base + "rx", problems);
      read_int(item, "tx", port.tx, -1, board::kLastGpio, base + "tx", problems);
      read_int(item, "de", port.de, -1, board::kLastGpio, base + "de", problems);
      read_int(item, "poll_s", port.poll_s, 0, 3600, base + "poll_s", problems);
      read_int(item, "modules", port.modules, 0, 8, base + "modules", problems);
      read_text(item, "dialect", port.dialect, 8, base + "dialect", problems);
      read_bool(item, "invert", port.invert, base + "invert", problems);
      read_int(item, "cells_per_cycle", port.cells_per_cycle, 0, 8, base + "cells_per_cycle", problems);
      read_bool(item, "pv2", port.pv2, base + "pv2", problems);
      read_int(item, "parallel", port.parallel, 0, 10, base + "parallel", problems);
    }
  }
  read_text(document, "profile", s.profile, 8, "profile", problems);
  if (const json::Value* mux = document.get("mux"); mux != nullptr) {
    if (!mux->is_array() || mux->items.size() > kMuxGroups) bad(problems, "mux", "invalid");
    else for (std::size_t i = 0; i < mux->items.size(); ++i) {
      const json::Value& item = mux->items[i];
      const std::string base = "mux." + std::to_string(i) + ".";
      if (!item.is_object()) { bad(problems, base + "tx", "invalid"); continue; }
      MuxGroup& group = s.mux[i];
      read_int(item, "tx", group.tx, -1, board::kLastGpio, base + "tx", problems);
      read_int(item, "rx", group.rx, -1, board::kLastGpio, base + "rx", problems);
      read_int(item, "s0", group.s0, -1, board::kLastGpio, base + "s0", problems);
      read_int(item, "s1", group.s1, -1, board::kLastGpio, base + "s1", problems);
      read_int(item, "channels", group.channels, 1, 4, base + "channels", problems);
    }
  }
  if (const json::Value* leds = document.get("leds"); leds != nullptr && leds->is_object()) {
    read_int(*leds, "data", s.leds.data, -1, board::kLastGpio, "leds.data", problems);
    read_int(*leds, "clock", s.leds.clock, -1, board::kLastGpio, "leds.clock", problems);
    read_int(*leds, "latch", s.leds.latch, -1, board::kLastGpio, "leds.latch", problems);
  }
  if (const json::Value* web = document.get("web"); web != nullptr && web->is_object()) {
    if (!read_choice<WebMode>(*web, "mode", {{"http", WebMode::kHttp}, {"both", WebMode::kBoth}, {"https", WebMode::kHttps}}, s.web)) bad(problems, "web.mode", "invalid");
  }
  if (const json::Value* ble = document.get("ble"); ble != nullptr && ble->is_object()) {
    if (!read_choice<BleMode>(*ble, "mode", {{"off", BleMode::kOff}, {"setup", BleMode::kSetup}, {"always", BleMode::kAlways}}, s.ble)) bad(problems, "ble.mode", "invalid");
  }
  if (const json::Value* time = document.get("time"); time != nullptr && time->is_object()) {
    read_bool(*time, "ntp_enabled", s.time.ntp_enabled, "time.ntp_enabled", problems);
    read_text(*time, "ntp", s.time.ntp, 64, "time.ntp", problems);
    read_text(*time, "zone", s.time.zone, 48, "time.zone", problems);
  }
  if (const json::Value* ui = document.get("ui"); ui != nullptr && ui->is_object()) read_text(*ui, "language", s.language, 4, "ui.language", problems);
  if (const json::Value* system = document.get("system"); system != nullptr && system->is_object()) read_int(*system, "auto_restart_hours", s.auto_restart_hours, 0, 48, "system.auto_restart_hours", problems);
}

// ---- checking ------------------------------------------------------------------------------------------------------------------

// A POSIX TZ rule: letters, digits, signs, commas, dots, colons, slashes and angle brackets, nothing else (it goes to setenv()).
inline bool time_zone_is_valid(std::string_view zone) {
  if (zone.empty() || zone.size() > 48) return false;
  for (const char c : zone) {
    const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '+' || c == '-' || c == ',' || c == '.' || c == ':' || c == '/' || c == '<' || c == '>';
    if (!ok) return false;
  }
  return true;
}

inline bool language_is_known(std::string_view code) {
  for (const char* known : {"en", "es", "de", "fr", "it", "ja", "zh"}) if (code == known) return true;
  return false;
}

inline bool auto_restart_hours_is_valid(int hours) {
  for (const int known : {0, 1, 2, 3, 4, 6, 12, 24, 48}) if (hours == known) return true;
  return false;
}

// A device name: it becomes a piece of an MQTT topic (the same rule as the contract's device name).
inline bool valid_device_name(std::string_view name) {
  if (name.empty() || name.size() > 32 || name.front() == '-' || name.front() == '_') return false;
  for (const char c : name) if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_')) return false;
  return true;
}

inline bool broker_uri_is_valid(std::string_view uri) {
  std::string_view rest;
  if (uri.substr(0, 7) == "mqtt://") rest = uri.substr(7);
  else if (uri.substr(0, 8) == "mqtts://") rest = uri.substr(8);
  else return false;
  const std::size_t colon = rest.find(':');
  const std::string_view host = rest.substr(0, colon);
  if (!net::valid_host(host)) return false;
  if (colon == std::string_view::npos) return true;
  const std::string_view port = rest.substr(colon + 1);
  if (port.empty() || port.size() > 5) return false;
  unsigned value = 0;
  for (const char c : port) { if (c < '0' || c > '9') return false; value = value * 10 + static_cast<unsigned>(c - '0'); }
  return value >= 1 && value <= 65535;
}

namespace detail {
// The pins already claimed, to catch two uses of one GPIO.
struct PinClaims {
  std::array<std::string, board::kLastGpio + 1> owner;
  bool claim(int gpio, const std::string& who) {
    if (gpio < 0 || gpio > board::kLastGpio) return true;
    if (!owner[static_cast<std::size_t>(gpio)].empty()) return false;
    owner[static_cast<std::size_t>(gpio)] = who;
    return true;
  }
};
}  // namespace detail

inline Problems validate(const Settings& s) {
  using detail::bad;
  Problems problems;
  if (!node_id_is_valid(s.node_id)) bad(problems, "node.id", "invalid");
  std::size_t name_characters = 0;
  if (s.node_name.empty()) bad(problems, "node.name", "required");
  else if (!net::valid_utf8(s.node_name, &name_characters) || name_characters > 48) bad(problems, "node.name", "invalid");
  if (!language_is_known(s.language)) bad(problems, "ui.language", "invalid");
  if (!auto_restart_hours_is_valid(s.auto_restart_hours)) bad(problems, "system.auto_restart_hours", "invalid");
  if (!s.hostname.empty() && !net::valid_hostname(s.hostname)) bad(problems, "node.hostname", "invalid");

  // the way in: the Ethernet cable (only the s3-eth board has one), with DHCP or a fixed address, or Wi-Fi
  if (s.uplink == Uplink::kEthernet && !board::kHasEthernet) bad(problems, "uplink", "not_available");
  if (!s.ip.dhcp) {   // a fixed address is for whichever connection the node uses, the cable or the Wi-Fi station
    std::uint32_t address = 0, mask = 0, gateway = 0, dns = 0;
    const bool address_ok = net::parse_ipv4(s.ip.address, address), mask_ok = net::parse_ipv4(s.ip.netmask, mask) && net::valid_netmask(mask);
    if (!address_ok || !net::usable_host_address(address)) bad(problems, "ip.address", s.ip.address.empty() ? "required" : "invalid");
    if (!mask_ok) bad(problems, "ip.netmask", s.ip.netmask.empty() ? "required" : "invalid");
    if (address_ok && mask_ok && net::is_network_or_broadcast(address, mask)) bad(problems, "ip.address", "invalid");
    if (!net::parse_ipv4(s.ip.gateway, gateway) || !net::usable_host_address(gateway)) bad(problems, "ip.gateway", s.ip.gateway.empty() ? "required" : "invalid");
    else if (address_ok && mask_ok && !net::same_subnet(address, gateway, mask)) bad(problems, "ip.gateway", "outside_subnet");
    else if (address_ok && gateway == address) bad(problems, "ip.gateway", "conflict");
    if (!s.ip.dns1.empty() && !net::parse_ipv4(s.ip.dns1, dns)) bad(problems, "ip.dns1", "invalid");
    if (!s.ip.dns2.empty() && !net::parse_ipv4(s.ip.dns2, dns)) bad(problems, "ip.dns2", "invalid");
  }

  // Wi-Fi: a node on Wi-Fi has no other way in, so at least one of its two ways in must be on
  if (s.ap.enabled) {
    if (!net::valid_ssid(s.ap.ssid)) bad(problems, "ap.ssid", s.ap.ssid.empty() ? "required" : "invalid");
    if (s.ap.security != WifiSecurity::kOpen && !net::valid_wpa_passphrase(s.ap.password)) bad(problems, "ap.password", s.ap.password.empty() ? "required" : "invalid_key");
  }
  if (s.ap.country.size() != 2 || !(s.ap.country[0] >= 'A' && s.ap.country[0] <= 'Z' && s.ap.country[1] >= 'A' && s.ap.country[1] <= 'Z')) bad(problems, "ap.country", "invalid");
  if (s.sta.enabled) {
    if (!net::valid_ssid(s.sta.ssid)) bad(problems, "sta.ssid", s.sta.ssid.empty() ? "required" : "invalid");
    if (!s.sta.password.empty() && !net::valid_wpa_passphrase(s.sta.password)) bad(problems, "sta.password", "invalid_key");
    for (std::size_t i = 0; i < s.sta.backup.size(); ++i) {
      const std::string base = "sta.backup." + std::to_string(i) + ".";
      const Network& network = s.sta.backup[i];
      if (!net::valid_ssid(network.ssid)) bad(problems, base + "ssid", network.ssid.empty() ? "required" : "invalid");
      if (!network.password.empty() && !net::valid_wpa_passphrase(network.password)) bad(problems, base + "password", "invalid_key");
    }
  }
  if (s.uplink == Uplink::kWifi && !s.ap.enabled && !s.sta.enabled) bad(problems, "sta.enabled", "required");

  // clock
  if (s.time.ntp_enabled && !net::valid_host(s.time.ntp)) bad(problems, "time.ntp", "invalid");
  if (!time_zone_is_valid(s.time.zone)) bad(problems, "time.zone", "invalid");

  // broker
  if (s.mqtt.enabled) {
    if (s.mqtt.uri.empty()) bad(problems, "mqtt.uri", "required");
    else if (!broker_uri_is_valid(s.mqtt.uri)) bad(problems, "mqtt.uri", "invalid");
    for (std::size_t i = 0; i < s.mqtt.backup.size(); ++i) {
      const std::string base = "mqtt.backup." + std::to_string(i) + ".";
      if (!broker_uri_is_valid(s.mqtt.backup[i].uri)) bad(problems, base + "uri", s.mqtt.backup[i].uri.empty() ? "required" : "invalid");
    }
  }

  // ports: the pins of the enabled ones may not be reserved nor shared, the names are unique and the speeds are ones the port can keep
  detail::PinClaims claims;
  std::vector<std::string> names;
  const auto claim = [&](int gpio, const std::string& who, const std::string& path) {
    if (!board::assignable(gpio)) { bad(problems, path, "reserved"); return; }
    if (!claims.claim(gpio, who)) bad(problems, path, "conflict");
  };
  if (s.profile != "direct" && s.profile != "mux") bad(problems, "profile", "invalid");
  const bool mux = profile_is_mux(s.profile);
  if (mux) {
    // the base board: each group's UART pins and select pins, and the LEDs' three pins, are claimed once; the ports have no pins of their own
    static const char* const kGroupNames[kMuxGroups] = {"mux-A", "mux-B", "mux-C"};
    for (std::size_t g = 0; g < kMuxGroups; ++g) {
      const MuxGroup& group = s.mux[g];
      const std::string base = "mux." + std::to_string(g) + ".";
      if (group.channels < 1 || group.channels > 4) { bad(problems, base + "channels", "range"); continue; }
      if (group.tx < 0) bad(problems, base + "tx", "required"); else claim(group.tx, kGroupNames[g], base + "tx");
      if (group.rx < 0) bad(problems, base + "rx", "required"); else claim(group.rx, kGroupNames[g], base + "rx");
      if (group.s0 < 0) bad(problems, base + "s0", "required"); else claim(group.s0, kGroupNames[g], base + "s0");
      if (group.channels > 2) { if (group.s1 < 0) bad(problems, base + "s1", "required"); else claim(group.s1, kGroupNames[g], base + "s1"); }
      else if (group.s1 >= 0) claim(group.s1, kGroupNames[g], base + "s1");
    }
    if (s.leds.any()) {
      if (s.leds.data < 0) bad(problems, "leds.data", "required"); else claim(s.leds.data, "leds", "leds.data");
      if (s.leds.clock < 0) bad(problems, "leds.clock", "required"); else claim(s.leds.clock, "leds", "leds.clock");
      if (s.leds.latch < 0) bad(problems, "leds.latch", "required"); else claim(s.leds.latch, "leds", "leds.latch");
    }
  }
  const std::size_t mux_ports = mux_port_count(s.mux);
  for (std::size_t i = 0; i < kPortCount; ++i) {
    const PortConfig& port = s.ports[i];
    if (!port.enabled) continue;
    const std::string base = "ports." + std::to_string(i) + ".", who = "port" + std::to_string(i + 1);
    if (mux && i >= mux_ports) { bad(problems, base + "enabled", "not_in_profile"); continue; }
    if (port.invert && !mux && is_soft_port(i)) bad(problems, base + "invert", "invalid");
    if (port.cells_per_cycle < 0 || port.cells_per_cycle > 8) bad(problems, base + "cells_per_cycle", "range");
    if ((port.pv2 || port.parallel != 0) && (port.kind != "voltronic" || port.dialect == "revo" || port.dialect == "pi18")) bad(problems, port.pv2 ? base + "pv2" : base + "parallel", "invalid");   // only the standard dialect has QPIGS2 and QPGS
    Kind kind = Kind::kVoltronic;
    const bool kind_ok = kind_from_text(port.kind, kind);
    if (!kind_ok) bad(problems, base + "kind", "invalid");
    if (kind_ok && kind == Kind::kVoltronic && port.dialect != "auto" && port.dialect != "pi30" && port.dialect != "revo" && port.dialect != "pi18") bad(problems, base + "dialect", "invalid");
    if (!valid_device_name(port.name)) bad(problems, base + "name", port.name.empty() ? "required" : "invalid");
    else if (std::find(names.begin(), names.end(), port.name) != names.end()) bad(problems, base + "name", "conflict");
    else names.push_back(port.name);
    const bool soft = !mux && is_soft_port(i);   // in the mux profile every port is served by a hardware UART
    const std::vector<int>& speeds = allowed_bauds(soft);
    const int baud = kind_ok ? effective_baud(port) : port.baud;
    if (kind_ok && std::find(speeds.begin(), speeds.end(), baud) == speeds.end()) bad(problems, base + "baud", soft ? "too_fast_for_emulated" : "range");
    if (!mux) {
      if (port.rx < 0) bad(problems, base + "rx", "required"); else claim(port.rx, who, base + "rx");
      if (port.tx >= 0) claim(port.tx, who, base + "tx"); else if (kind_ok && info_of(kind).needs_tx) bad(problems, base + "tx", "required");
      if (port.de >= 0) claim(port.de, who, base + "de");
    }
    if (port.poll_s != 0 && (port.poll_s < 2 || port.poll_s > 3600)) bad(problems, base + "poll_s", "range");
  }
  return problems;
}

// ---- writing -------------------------------------------------------------------------------------------------------------------

// `secrets`: true writes the passwords (for flash storage); false replaces them with "password_set" flags (for the panel).
inline std::string to_json(const Settings& s, bool secrets) {
  json::Writer w;
  w.begin_object();
  w.field("v", kVersion);
  w.key("node").begin_object().field("id", s.node_id).field("name", s.node_name).field("hostname", s.hostname).end_object();
  w.field("uplink", to_text(s.uplink));
  w.key("ip").begin_object().field("dhcp", s.ip.dhcp).field("address", s.ip.address).field("netmask", s.ip.netmask).field("gateway", s.ip.gateway)
      .field("dns1", s.ip.dns1).field("dns2", s.ip.dns2).end_object();
  w.key("ap").begin_object().field("enabled", s.ap.enabled).field("ssid", s.ap.ssid).field("security", to_text(s.ap.security));
  if (secrets) w.field("password", s.ap.password); else w.field("password_set", !s.ap.password.empty());
  w.field("channel", s.ap.channel).field("hidden", s.ap.hidden).field("max_clients", s.ap.max_clients).field("tx_power_dbm", s.ap.tx_power_dbm)
      .field("bandwidth_mhz", s.ap.bandwidth_mhz).field("country", s.ap.country).end_object();
  w.key("sta").begin_object().field("enabled", s.sta.enabled).field("ssid", s.sta.ssid);
  if (secrets) w.field("password", s.sta.password); else w.field("password_set", !s.sta.password.empty());
  w.key("backup").begin_array();
  for (const Network& network : s.sta.backup) {
    w.begin_object().field("ssid", network.ssid);
    if (secrets) w.field("password", network.password); else w.field("password_set", !network.password.empty());
    w.end_object();
  }
  w.end_array();
  w.end_object();
  w.key("mqtt").begin_object().field("enabled", s.mqtt.enabled).field("uri", s.mqtt.uri).field("username", s.mqtt.username);
  if (secrets) w.field("password", s.mqtt.password); else w.field("password_set", !s.mqtt.password.empty());
  w.field("heartbeat_s", s.mqtt.heartbeat_s);
  w.key("backup").begin_array();
  for (const Broker& broker : s.mqtt.backup) {
    w.begin_object().field("uri", broker.uri).field("username", broker.username);
    if (secrets) w.field("password", broker.password); else w.field("password_set", !broker.password.empty());
    w.end_object();
  }
  w.end_array();
  w.end_object();
  w.key("ports").begin_array();
  for (const PortConfig& port : s.ports) {
    w.begin_object().field("enabled", port.enabled).field("kind", port.kind).field("name", port.name).field("baud", port.baud).field("rx", port.rx).field("tx", port.tx)
        .field("de", port.de).field("poll_s", port.poll_s).field("modules", port.modules).field("dialect", port.dialect).field("invert", port.invert)
        .field("cells_per_cycle", port.cells_per_cycle).field("pv2", port.pv2).field("parallel", port.parallel).end_object();
  }
  w.end_array();
  w.field("profile", s.profile);
  w.key("mux").begin_array();
  for (const MuxGroup& group : s.mux) w.begin_object().field("tx", group.tx).field("rx", group.rx).field("s0", group.s0).field("s1", group.s1).field("channels", group.channels).end_object();
  w.end_array();
  w.key("leds").begin_object().field("data", s.leds.data).field("clock", s.leds.clock).field("latch", s.leds.latch).end_object();
  w.key("web").begin_object().field("mode", to_text(s.web)).end_object();
  w.key("ble").begin_object().field("mode", to_text(s.ble)).end_object();
  w.key("ui").begin_object().field("language", s.language).end_object();
  w.key("time").begin_object().field("ntp_enabled", s.time.ntp_enabled).field("ntp", s.time.ntp).field("zone", s.time.zone).end_object();
  w.key("system").begin_object().field("auto_restart_hours", s.auto_restart_hours).end_object();
  w.end_object();
  return w.str();
}

// Reads a stored or received document on top of `base` and checks the result. Nothing is applied unless `problems` stays empty.
inline bool load(std::string_view text, const Settings& base, Settings& out, Problems& problems) {
  json::Value document;
  if (!json::parse(text, document)) { problems.push_back({"", "not_json"}); return false; }
  out = base;
  read_settings(document, out, problems);
  if (problems.empty()) problems = validate(out);
  return problems.empty();
}

}  // namespace armor::config

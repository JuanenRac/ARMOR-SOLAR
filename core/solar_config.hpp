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

struct Station {
  bool enabled = false;
  std::string ssid, password;
};

struct Mqtt {
  bool enabled = false;  // a node that was never configured has no broker yet
  std::string uri, username, password;
  int heartbeat_s = 10;  // the keep-alive of the connection is twice this
  std::string ntp = "pool.ntp.org";
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
  std::array<PortConfig, kPortCount> ports;
  WebMode web = WebMode::kBoth;
  BleMode ble = BleMode::kSetup;   // "setup": only while the node has no user; "always"; "off": the Bluetooth stack is not even started
  std::string language = "en";
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
  }
  if (const json::Value* mqtt = document.get("mqtt"); mqtt != nullptr && mqtt->is_object()) {
    read_bool(*mqtt, "enabled", s.mqtt.enabled, "mqtt.enabled", problems);
    read_text(*mqtt, "uri", s.mqtt.uri, 160, "mqtt.uri", problems);
    read_text(*mqtt, "username", s.mqtt.username, 64, "mqtt.username", problems);
    read_secret(*mqtt, "password", s.mqtt.password, "mqtt.password", problems);
    read_int(*mqtt, "heartbeat_s", s.mqtt.heartbeat_s, 2, 300, "mqtt.heartbeat_s", problems);
    read_text(*mqtt, "ntp", s.mqtt.ntp, 64, "mqtt.ntp", problems);
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
    }
  }
  if (const json::Value* web = document.get("web"); web != nullptr && web->is_object()) {
    if (!read_choice<WebMode>(*web, "mode", {{"http", WebMode::kHttp}, {"both", WebMode::kBoth}, {"https", WebMode::kHttps}}, s.web)) bad(problems, "web.mode", "invalid");
  }
  if (const json::Value* ble = document.get("ble"); ble != nullptr && ble->is_object()) {
    if (!read_choice<BleMode>(*ble, "mode", {{"off", BleMode::kOff}, {"setup", BleMode::kSetup}, {"always", BleMode::kAlways}}, s.ble)) bad(problems, "ble.mode", "invalid");
  }
  if (const json::Value* ui = document.get("ui"); ui != nullptr && ui->is_object()) read_text(*ui, "language", s.language, 4, "ui.language", problems);
}

// ---- checking ------------------------------------------------------------------------------------------------------------------

inline bool language_is_known(std::string_view code) {
  for (const char* known : {"en", "es", "de", "fr", "it", "ja", "zh"}) if (code == known) return true;
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
  if (!s.hostname.empty() && !net::valid_hostname(s.hostname)) bad(problems, "node.hostname", "invalid");

  // the way in: the Ethernet cable (only the s3-eth board has one), with DHCP or a fixed address, or Wi-Fi
  if (s.uplink == Uplink::kEthernet && !board::kHasEthernet) bad(problems, "uplink", "not_available");
  if (s.uplink == Uplink::kEthernet && !s.ip.dhcp) {
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
  }
  if (s.uplink == Uplink::kWifi && !s.ap.enabled && !s.sta.enabled) bad(problems, "sta.enabled", "required");

  // broker
  if (s.mqtt.enabled) {
    if (s.mqtt.uri.empty()) bad(problems, "mqtt.uri", "required");
    else if (!broker_uri_is_valid(s.mqtt.uri)) bad(problems, "mqtt.uri", "invalid");
    if (!net::valid_host(s.mqtt.ntp)) bad(problems, "mqtt.ntp", "invalid");
  }

  // ports: the pins of the enabled ones may not be reserved nor shared, the names are unique and the speeds are ones the port can keep
  detail::PinClaims claims;
  std::vector<std::string> names;
  const auto claim = [&](int gpio, const std::string& who, const std::string& path) {
    if (!board::assignable(gpio)) { bad(problems, path, "reserved"); return; }
    if (!claims.claim(gpio, who)) bad(problems, path, "conflict");
  };
  for (std::size_t i = 0; i < kPortCount; ++i) {
    const PortConfig& port = s.ports[i];
    if (!port.enabled) continue;
    const std::string base = "ports." + std::to_string(i) + ".", who = "port" + std::to_string(i + 1);
    Kind kind = Kind::kVoltronic;
    const bool kind_ok = kind_from_text(port.kind, kind);
    if (!kind_ok) bad(problems, base + "kind", "invalid");
    if (kind_ok && kind == Kind::kVoltronic && port.dialect != "auto" && port.dialect != "pi30" && port.dialect != "revo" && port.dialect != "pi18") bad(problems, base + "dialect", "invalid");
    if (!valid_device_name(port.name)) bad(problems, base + "name", port.name.empty() ? "required" : "invalid");
    else if (std::find(names.begin(), names.end(), port.name) != names.end()) bad(problems, base + "name", "conflict");
    else names.push_back(port.name);
    const std::vector<int>& speeds = allowed_bauds(is_soft_port(i));
    const int baud = kind_ok ? effective_baud(port) : port.baud;
    if (kind_ok && std::find(speeds.begin(), speeds.end(), baud) == speeds.end()) bad(problems, base + "baud", is_soft_port(i) ? "too_fast_for_emulated" : "range");
    if (port.rx < 0) bad(problems, base + "rx", "required"); else claim(port.rx, who, base + "rx");
    if (port.tx >= 0) claim(port.tx, who, base + "tx"); else if (kind_ok && info_of(kind).needs_tx) bad(problems, base + "tx", "required");
    if (port.de >= 0) claim(port.de, who, base + "de");
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
  w.end_object();
  w.key("mqtt").begin_object().field("enabled", s.mqtt.enabled).field("uri", s.mqtt.uri).field("username", s.mqtt.username);
  if (secrets) w.field("password", s.mqtt.password); else w.field("password_set", !s.mqtt.password.empty());
  w.field("heartbeat_s", s.mqtt.heartbeat_s).field("ntp", s.mqtt.ntp).end_object();
  w.key("ports").begin_array();
  for (const PortConfig& port : s.ports) {
    w.begin_object().field("enabled", port.enabled).field("kind", port.kind).field("name", port.name).field("baud", port.baud).field("rx", port.rx).field("tx", port.tx)
        .field("de", port.de).field("poll_s", port.poll_s).field("modules", port.modules).field("dialect", port.dialect).end_object();
  }
  w.end_array();
  w.key("web").begin_object().field("mode", to_text(s.web)).end_object();
  w.key("ble").begin_object().field("mode", to_text(s.ble)).end_object();
  w.key("ui").begin_object().field("language", s.language).end_object();
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

// ARMOR-SOLAR - the ports at work.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
//
// One task per enabled port: it opens the port, asks its equipment whenever the poller says so, feeds the poller what arrives and hands every whole reading to
// the broker link. A port that cannot be opened says why (pins, speed, driver) and stays out of the way of the others. Nothing here has run on a board.
#include "solar_manager.hpp"

#include <array>
#include <atomic>
#include <memory>
#include <mutex>
extern "C" {
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
}
#include "core/ant_settings.hpp"
#include "core/console_probe.hpp"
#include "core/mux_group.hpp"
#include "core/poller.hpp"
#include "mqtt_link.hpp"
#include "mux_board.hpp"
#include "port_leds_hw.hpp"
#include "uart_ports.hpp"

namespace armor::manager {
namespace {
constexpr char kTag[] = "armor-ports";

struct Slot {
  std::mutex lock;
  PortStatus status;
  std::string raw;
  std::string node_id;
  config::PortConfig config;
  std::size_t index = 0;
  // the ANT-BMS settings operation: asked by the web server, run by the port's task
  std::atomic<bool> settings_requested{false};
  bool ant_new_protocol = false;
  BmsSettingsStatus settings;
  // the console question asked from the panel
  std::atomic<bool> console_requested{false};
  ConsoleStatus console;
};
std::array<Slot, config::kPortCount> g_slots;
Publish g_publish;

std::uint64_t uptime_ms() { return static_cast<std::uint64_t>(esp_timer_get_time()) / 1000ULL; }

void fill(Slot& slot, const solar::Poller& poller, std::uint32_t overruns, std::uint32_t framing_errors, const std::string& payload) {
  const solar::PortStats& stats = poller.stats();
  std::lock_guard<std::mutex> guard(slot.lock);
  PortStatus& s = slot.status;
  s.state = poller.state(uptime_ms());
  s.error = stats.last_error;
  s.detail = poller.detail();
  s.bytes_rx = stats.bytes_rx; s.bytes_tx = stats.bytes_tx; s.replies_ok = stats.replies_ok; s.replies_bad = stats.replies_bad; s.timeouts = stats.timeouts; s.readings = stats.readings;
  s.overruns = overruns;
  s.framing_errors = framing_errors;
  if (!payload.empty()) s.last_payload = payload;
  slot.ant_new_protocol = poller.ant_new_protocol();
}

// Reads the settings of the BMS on this port: one request at a time, nothing written, the poller waits. The result is left in the slot for the web server to give.
template <class Line>   // a Port, or the MuxLine of a group
void run_bms_settings(Slot& slot, Line& port) {
  solar::ant::SettingsReader reader;
  std::uint8_t buffer[128];
  std::size_t reported = static_cast<std::size_t>(-1);
  while (!reader.done()) {
    const std::vector<std::uint8_t> request = reader.next_tx(uptime_ms());
    if (!request.empty() && !port.write(request.data(), request.size())) ESP_LOGW(kTag, "port %u: a settings request could not be sent", static_cast<unsigned>(slot.index + 1));
    const std::size_t n = port.read(buffer, sizeof buffer, 30);
    if (n > 0) reader.on_rx(buffer, n);
    if (reader.step() != reported) {
      reported = reader.step();
      std::lock_guard<std::mutex> guard(slot.lock);
      slot.settings.step = reported;
    }
  }
  std::lock_guard<std::mutex> guard(slot.lock);
  slot.settings.step = solar::ant::SettingsReader::total();
  if (reader.failed()) { slot.settings.state = "error"; slot.settings.error = reader.error(); }
  else { slot.settings.state = "done"; slot.settings.result = reader.result_json(); }
}

// Asks the console the question that was requested and waits for its answer; the port's readings wait meanwhile.
template <class Line>
void run_console(Slot& slot, Line& port) {
  solar::console::Probe probe;
  std::string command, why;
  { std::lock_guard<std::mutex> guard(slot.lock); command = slot.console.command; }
  probe.start(command, uptime_ms(), why);
  std::uint8_t buffer[256];
  while (probe.running()) {
    const std::vector<std::uint8_t> request = probe.next_tx(uptime_ms());
    if (!request.empty() && !port.write(request.data(), request.size())) ESP_LOGW(kTag, "port %u: the console question could not be sent", static_cast<unsigned>(slot.index + 1));
    const std::size_t n = port.read(buffer, sizeof buffer, 40);
    if (n > 0) probe.on_rx(buffer, n, uptime_ms());
    probe.tick(uptime_ms());
  }
  std::lock_guard<std::mutex> guard(slot.lock);
  slot.console.state = probe.state();
  slot.console.text = probe.text();
  slot.console.error = probe.error();
  slot.console.truncated = probe.truncated();
}

void port_task(void* argument) {
  Slot& slot = *static_cast<Slot*>(argument);
  const config::PortConfig& cfg = slot.config;
  config::Kind kind = config::Kind::kVoltronic;
  config::kind_from_text(cfg.kind, kind);
  ports::Options options;
  options.index = slot.index;
  options.baud = config::effective_baud(cfg);
  options.rx = cfg.rx; options.tx = cfg.tx; options.de = cfg.de; options.invert = cfg.invert;
  std::string error;
  std::unique_ptr<ports::Port> port = ports::open(options, error);
  if (!port) {
    ESP_LOGE(kTag, "port %u could not be opened (%s): it stays off", static_cast<unsigned>(slot.index + 1), error.c_str());
    std::lock_guard<std::mutex> guard(slot.lock);
    slot.status.state = "error";
    slot.status.error = error;
    vTaskDelete(nullptr);
    return;
  }
  // on the heap: its raw log and its readings would eat the port task's small stack
  const std::unique_ptr<solar::Poller> poller_owner = std::make_unique<solar::Poller>(kind, slot.node_id, cfg.name, config::effective_poll_s(cfg), cfg.modules, cfg.dialect, cfg.cells_per_cycle);
  solar::Poller& poller = *poller_owner;
  poller.set_optional(cfg.pv2, cfg.parallel);
  std::uint8_t buffer[256];
  std::uint64_t last_fill_ms = 0, last_raw_total = 0;
  for (;;) {
    if (slot.settings_requested.exchange(false)) run_bms_settings(slot, *port);
    if (slot.console_requested.exchange(false)) run_console(slot, *port);
    const std::uint64_t now = uptime_ms();
    const std::vector<std::uint8_t> request = poller.next_tx(now);
    if (!request.empty() && !port->write(request.data(), request.size())) ESP_LOGW(kTag, "port %u: the request could not be sent", static_cast<unsigned>(slot.index + 1));
    const std::size_t n = port->read(buffer, sizeof buffer, 40);
    if (n > 0) poller.on_rx(buffer, n, uptime_ms());
    std::string topic, payload;
    // The message carries the node's wall clock; while the clock is not set it is only for the panel (the broker link drops it).
    const bool ready = poller.take_message(mqtt_link::clock_is_set() ? mqtt_link::wall_clock_ms() : uptime_ms(), topic, payload);
    if (ready && g_publish) g_publish(topic, payload);
    const std::uint64_t after = uptime_ms();
    if (ready || after - last_fill_ms >= 1000) {
      last_fill_ms = after;
      fill(slot, poller, port->overruns(), port->framing_errors(), ready ? payload : std::string());
      if (poller.raw().total() != last_raw_total) {
        last_raw_total = poller.raw().total();
        const std::string dump = poller.raw().dump();
        std::lock_guard<std::mutex> guard(slot.lock);
        slot.raw = dump;
      }
    }
  }
}
// ---- the mux profile: one task per group of ports that share a UART ----------------------------------------------------------------------------------

struct GroupJob {
  std::size_t group = 0;
  config::MuxGroup pins;
  std::vector<std::size_t> ports;   // the enabled ports of the group, by index
};
std::array<GroupJob, config::kMuxGroups> g_groups;

void group_task(void* argument) {
  GroupJob& job = *static_cast<GroupJob*>(argument);
  const config::MuxGroup& pins = job.pins;
  int first_baud = 9600;
  if (!job.ports.empty()) first_baud = config::effective_baud(g_slots[job.ports.front()].config);
  std::string error;
  std::unique_ptr<solar::MuxLine> line = muxboard::open_group(job.group, pins, first_baud, error);
  if (!line) {
    ESP_LOGE(kTag, "group %c could not be opened (%s): its ports stay off", static_cast<char>('A' + job.group), error.c_str());
    for (std::size_t index : job.ports) { std::lock_guard<std::mutex> guard(g_slots[index].lock); g_slots[index].status.state = "error"; g_slots[index].status.error = error; }
    vTaskDelete(nullptr);
    return;
  }
  // on the heap: every port's raw log and readings would eat the task's small stack
  std::vector<solar::MuxMember> members;
  for (std::size_t index : job.ports) {
    Slot& slot = g_slots[index];
    const config::PortConfig& cfg = slot.config;
    config::Kind kind = config::Kind::kVoltronic;
    config::kind_from_text(cfg.kind, kind);
    solar::MuxMember member;
    member.port = index;
    member.channel = static_cast<std::size_t>(slot.status.channel);
    member.baud = config::effective_baud(cfg);
    member.invert = cfg.invert;
    member.poller = std::make_unique<solar::Poller>(kind, slot.node_id, cfg.name, config::effective_poll_s(cfg), cfg.modules, cfg.dialect, cfg.cells_per_cycle);
    member.poller->set_optional(cfg.pv2, cfg.parallel);
    members.push_back(std::move(member));
  }
  const std::unique_ptr<solar::MuxGroup> group = std::make_unique<solar::MuxGroup>(std::move(members));
  solar::MuxClock clock;
  clock.uptime_ms = uptime_ms;
  clock.wall_clock_is_set = [] { return mqtt_link::clock_is_set(); };
  clock.wall_clock_ms = [] { return mqtt_link::wall_clock_ms(); };
  std::vector<std::uint64_t> last_fill(job.ports.size(), 0), last_raw_total(job.ports.size(), 0);
  std::uint32_t reported_failures = 0;
  for (;;) {
    // the reading of an ANT-BMS's settings asked in the panel is done between two turns, on that port's channel
    if (!group->has_line()) {
      for (std::size_t k = 0; k < job.ports.size(); ++k) {
        Slot& slot = g_slots[job.ports[k]];
        if (!slot.settings_requested.exchange(false)) continue;
        const solar::MuxMember& m = group->members()[k];
        line->select(m.channel);
        line->configure(m.baud, m.invert);
        line->discard();
        run_bms_settings(slot, *line);
      }
      for (std::size_t k = 0; k < job.ports.size(); ++k) {
        Slot& slot = g_slots[job.ports[k]];
        if (!slot.console_requested.exchange(false)) continue;
        const solar::MuxMember& m = group->members()[k];
        line->select(m.channel);
        line->configure(m.baud, m.invert);
        line->discard();
        run_console(slot, *line);
      }
    }
    const solar::MuxTurn turn = group->step(*line, clock, 20);
    if (group->write_failures() != reported_failures) { reported_failures = group->write_failures(); ESP_LOGW(kTag, "group %c: a request could not be sent", static_cast<char>('A' + job.group)); }
    if (turn.reading && g_publish) g_publish(turn.topic, turn.payload);
    if (turn.reading) leds::good(turn.port);
    if (turn.ended && turn.failed) leds::bad(turn.port);
    if (!turn.active) continue;
    Slot& slot = g_slots[turn.port];
    const solar::Poller& poller = *group->members()[turn.member].poller;
    const std::uint64_t after = uptime_ms();
    if (turn.reading || turn.ended || after - last_fill[turn.member] >= 1000) {
      last_fill[turn.member] = after;
      fill(slot, poller, 0, 0, turn.reading ? turn.payload : std::string());
      if (poller.raw().total() != last_raw_total[turn.member]) {
        last_raw_total[turn.member] = poller.raw().total();
        const std::string dump = poller.raw().dump();
        std::lock_guard<std::mutex> guard(slot.lock);
        slot.raw = dump;
      }
    }
  }
}
}  // namespace

// The mux profile: the eight ports of the base board, their groups and the LEDs.
static void start_mux(const config::Settings& settings) {
  for (std::size_t g = 0; g < config::kMuxGroups; ++g) { g_groups[g].group = g; g_groups[g].pins = settings.mux[g]; g_groups[g].ports.clear(); }
  for (std::size_t i = 0; i < config::kPortCount; ++i) {
    Slot& slot = g_slots[i];
    const config::PortConfig& cfg = settings.ports[i];
    slot.index = i;
    slot.node_id = settings.node_id;
    slot.config = cfg;
    PortStatus& s = slot.status;
    s.number = static_cast<int>(i + 1);
    config::MuxSlot where;
    const bool in_profile = config::mux_slot_of(settings.mux, i, where);
    s.enabled = cfg.enabled && in_profile;
    s.soft = false;
    s.kind = cfg.kind; s.name = cfg.name;
    s.baud = config::effective_baud(cfg);
    s.invert = cfg.invert;
    s.poll_s = config::effective_poll_s(cfg);
    if (in_profile) { s.group = static_cast<int>(where.group); s.channel = static_cast<int>(where.channel); s.rx = settings.mux[where.group].rx; s.tx = settings.mux[where.group].tx; }
    s.state = s.enabled ? "starting" : "disabled";
    if (s.enabled) g_groups[where.group].ports.push_back(i);
  }
  leds::start(settings.leds, settings);
  for (std::size_t g = 0; g < config::kMuxGroups; ++g) {
    if (g_groups[g].ports.empty()) continue;
    xTaskCreate(group_task, (std::string("mux-") + static_cast<char>('A' + g)).c_str(), 8192, &g_groups[g], 5, nullptr);
  }
}

void start(const config::Settings& settings, Publish publish) {
  g_publish = std::move(publish);
  if (config::profile_is_mux(settings.profile)) { start_mux(settings); return; }
  for (std::size_t i = 0; i < config::kPortCount; ++i) {
    Slot& slot = g_slots[i];
    const config::PortConfig& cfg = settings.ports[i];
    slot.index = i;
    slot.node_id = settings.node_id;
    slot.config = cfg;
    PortStatus& s = slot.status;
    s.number = static_cast<int>(i + 1);
    s.enabled = cfg.enabled;
    s.soft = config::is_soft_port(i);
    s.kind = cfg.kind; s.name = cfg.name;
    s.baud = config::effective_baud(cfg);
    s.rx = cfg.rx; s.tx = cfg.tx; s.de = cfg.de; s.poll_s = config::effective_poll_s(cfg);
    s.state = cfg.enabled ? "starting" : "disabled";
    if (!cfg.enabled) continue;
    xTaskCreate(port_task, ("port" + std::to_string(i + 1)).c_str(), 6144, &slot, 5, nullptr);
  }
}

std::string start_bms_settings(std::size_t index) {
  if (index >= config::kPortCount) return "no_port";
  Slot& slot = g_slots[index];
  std::lock_guard<std::mutex> guard(slot.lock);
  if (!slot.status.enabled || slot.status.kind != "ant") return "not_ant";
  if (slot.settings.state == "running") return "busy";
  if (!slot.ant_new_protocol) return slot.status.detail.empty() ? "protocol_unknown" : "old_protocol";
  slot.settings = BmsSettingsStatus();
  slot.settings.state = "running";
  slot.settings.total = solar::ant::SettingsReader::total();
  slot.settings_requested = true;
  return "";
}

std::string start_console(std::size_t index, const std::string& command) {
  if (index >= config::kPortCount) return "no_port";
  Slot& slot = g_slots[index];
  std::lock_guard<std::mutex> guard(slot.lock);
  if (!slot.status.enabled || slot.status.kind != "pylontech") return "not_pylontech";
  std::string canonical;
  if (!solar::console::allowed(command, canonical)) return "not_allowed";
  if (slot.console.state == "running") return "busy";
  slot.console = ConsoleStatus();
  slot.console.state = "running";
  slot.console.command = canonical;
  slot.console_requested = true;
  return "";
}

ConsoleStatus console_status(std::size_t index) {
  if (index >= config::kPortCount) return {};
  Slot& slot = g_slots[index];
  std::lock_guard<std::mutex> guard(slot.lock);
  return slot.console;
}

BmsSettingsStatus bms_settings(std::size_t index) {
  if (index >= config::kPortCount) return {};
  Slot& slot = g_slots[index];
  std::lock_guard<std::mutex> guard(slot.lock);
  return slot.settings;
}

PortStatus status(std::size_t index) {
  if (index >= config::kPortCount) return {};
  Slot& slot = g_slots[index];
  std::lock_guard<std::mutex> guard(slot.lock);
  return slot.status;
}

std::string raw_dump(std::size_t index) {
  if (index >= config::kPortCount) return "";
  Slot& slot = g_slots[index];
  std::lock_guard<std::mutex> guard(slot.lock);
  return slot.raw;
}

std::string readings_json() {
  std::string out = "[";
  bool first = true;
  for (Slot& slot : g_slots) {
    std::lock_guard<std::mutex> guard(slot.lock);
    if (slot.status.last_payload.empty()) continue;
    if (!first) out += ',';
    first = false;
    out += slot.status.last_payload;
  }
  return out + "]";
}

}  // namespace armor::manager

// ARMOR-SOLAR - what a serial port does with the equipment on it: when to ask, what to ask, how to read the answers and when a reading is whole.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
//
// One Poller per port. It touches no hardware: the firmware feeds it the bytes that arrive and the time, and sends the bytes it returns, so the whole
// exchange (the order of the requests, the timeouts, a reply that is refused or garbled, a battery with several modules) is tested on a computer with a
// stand-in for the equipment.
//
//   Voltronic / MPP Solar inverter:  QPIGS (the readings), QMOD (the mode), QPIWS (the warnings), each answered before the next is asked. A reading is
//                                    published only when all three came back whole: a missing warning list must not look like "no warnings".
//                                    Three dialects: "pi30" (that one), "revo" (QPIGS arranged differently, no warnings asked, replies that may end with a
//                                    one-byte checksum) and "pi18" (^P005GS, ^P006MOD, ^P005FWS). "auto" (the default) asks in pi30, then in pi18, and
//                                    keeps to the one that answered; a reply that ends with the checksum instead of the CRC makes it revo.
//   Pylontech battery (console):     `pwr` (the table of the modules), then for every module that is there `bat <n>` (its cells and remaining charge), and every half hour
//                                    `info <n>` (model, rated capacity) and `stat <n>` (cycles), whose answers are kept in between. The extras are optional: if they do not
//                                    answer, the reading goes out without them.
//   ANT-BMS battery (serial):        one request, one whole frame back (core/ant_bms.hpp). Its firmware speaks one of two protocols: the port asks in the old one
//                                    and, when nothing answers, in the new one, and keeps to the one that answered (it looks again after three misses).
//   Raw:                             nothing is asked; what arrives is kept so it can be looked at (a protocol that is not decoded yet).
//
// Nothing here has run against an inverter or a battery.
#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "ant_bms.hpp"
#include "pylontech.hpp"
#include "solar_config.hpp"
#include "solar_json.hpp"
#include "voltronic.hpp"
#include "voltronic_pi18.hpp"

namespace armor::solar {

// The last bytes a port received, to look at in the panel: kept as they came, shown as hexadecimal with the printable characters beside them.
class RawLog {
 public:
  static constexpr std::size_t kCapacity = 2048;
  void add(const std::uint8_t* data, std::size_t length) {
    for (std::size_t i = 0; i < length; ++i) {
      bytes_[head_] = data[i];
      head_ = (head_ + 1) % kCapacity;
      if (size_ < kCapacity) ++size_;
    }
    total_ += length;
  }
  std::size_t total() const { return total_; }
  // The kept bytes, oldest first, sixteen to a line: "48 65 6C 6C 6F 0D  |Hello.|".
  std::string dump(std::size_t max_lines = 128) const {
    std::string out;
    const std::size_t start = (head_ + kCapacity - size_) % kCapacity;
    std::size_t first = 0;
    const std::size_t lines = (size_ + 15) / 16;
    if (lines > max_lines) first = (lines - max_lines) * 16;
    for (std::size_t line = first; line < size_; line += 16) {
      std::string hex, text;
      for (std::size_t i = line; i < line + 16 && i < size_; ++i) {
        const std::uint8_t b = bytes_[(start + i) % kCapacity];
        static const char kDigits[] = "0123456789ABCDEF";
        hex += kDigits[b >> 4];
        hex += kDigits[b & 15];
        hex += ' ';
        text += (b >= 0x20 && b < 0x7F) ? static_cast<char>(b) : '.';
      }
      while (hex.size() < 48) hex += ' ';
      out += hex + " |" + text + "|\n";
    }
    return out;
  }
 private:
  std::array<std::uint8_t, kCapacity> bytes_{};
  std::size_t head_ = 0, size_ = 0, total_ = 0;
};

struct PortStats {
  std::uint32_t bytes_rx = 0, bytes_tx = 0;
  std::uint32_t replies_ok = 0;       // whole, checked answers
  std::uint32_t replies_bad = 0;      // bytes that arrived but were not a whole, checked answer (wrong CRC, "NAK", not a table)
  std::uint32_t timeouts = 0;         // a request nobody answered in time
  std::uint32_t readings = 0;         // whole readings made (each one becomes a message)
  std::string last_error;             // "", "timeout", "crc", "nak", "format"
};

class Poller {
 public:
  static constexpr std::uint64_t kVoltronicTimeoutMs = 2500;
  static constexpr std::uint64_t kConsoleTableTimeoutMs = 5000;
  static constexpr std::uint64_t kConsoleExtraTimeoutMs = 3000;
  static constexpr std::uint64_t kAntTimeoutMs = 2000;
  static constexpr std::uint64_t kExtrasRefreshMs = 30ULL * 60ULL * 1000ULL;   // how long the identity and the history of a module are kept

  // `cells_per_cycle`: a Pylontech stack of several modules takes long to read cell by cell on a line that other ports share; with a number here (1 to 8) each cycle reads the
  // cells of that many modules, in rotation, and the others keep the cells of their last turn. 0 (the default): every module every cycle.
  Poller(config::Kind kind, std::string node_id, std::string device, int poll_s, int max_modules, const std::string& dialect = "auto", int cells_per_cycle = 0)
      : kind_(kind), node_id_(std::move(node_id)), device_(std::move(device)), poll_ms_(static_cast<std::uint64_t>(poll_s) * 1000ULL), max_modules_(max_modules > 0 ? max_modules : 8),
        cells_per_cycle_(cells_per_cycle > 0 ? cells_per_cycle : 0) {
    if (dialect == "pi18") { dialect_ = Dialect::kPi18; auto_ = false; }
    else if (dialect == "revo") { dialect_ = Dialect::kRevo; auto_ = false; }
    else if (dialect == "pi30") auto_ = false;
    known_ = !auto_;
  }

  // The bytes to send now, or none. Call it often (every few tens of milliseconds).
  std::vector<std::uint8_t> next_tx(std::uint64_t now_ms) {
    if (kind_ == config::Kind::kRaw) return {};
    if (step_ != Step::kIdle && now_ms >= deadline_ms_) on_timeout(now_ms);
    if (!pending_.empty()) {
      std::vector<std::uint8_t> out;
      out.swap(pending_);
      stats_.bytes_tx += static_cast<std::uint32_t>(out.size());
      return out;
    }
    // a reading that has not been taken yet is not overwritten by the next cycle
    if (step_ == Step::kIdle && !ready_ && now_ms >= next_cycle_ms_) begin_cycle(now_ms);
    if (pending_.empty()) return {};
    std::vector<std::uint8_t> out;
    out.swap(pending_);
    stats_.bytes_tx += static_cast<std::uint32_t>(out.size());
    return out;
  }

  // Optional readings of an inverter in the standard dialect, asked after the three that make the message and never at the cost of them: the second PV input (QPIGS2; an
  // inverter that has only one answers NAK, and after two misses it is not asked again) and the units of a parallel system (QPGS0 to QPGS<n-1>; a unit that is not there is skipped).
  void set_optional(bool pv2, int parallel_units) { pv2_wanted_ = pv2; parallel_ = parallel_units < 0 ? 0 : parallel_units > 10 ? 10 : parallel_units; }
  // Whether the second PV input has answered (in the last cycle), and what it said.
  bool has_pv2() const { return has_pv2_; }

  // Whether an exchange is under way (a request waits for its answer, or one is about to go out): a line shared with other ports keeps its channel on this one until it is over.
  bool busy() const { return step_ != Step::kIdle || !pending_.empty(); }
  // Whether the next cycle is due and nothing stands in its way (a reading not taken yet is not overwritten). A raw port never asks, so it is never due.
  bool due(std::uint64_t now_ms) const { return kind_ != config::Kind::kRaw && step_ == Step::kIdle && !ready_ && pending_.empty() && now_ms >= next_cycle_ms_; }
  // Whether a whole reading waits to be taken.
  bool ready() const { return ready_; }

  // Bytes that arrived.
  void on_rx(const std::uint8_t* data, std::size_t length, std::uint64_t now_ms) {
    if (length == 0) return;
    stats_.bytes_rx += static_cast<std::uint32_t>(length);
    last_rx_ms_ = now_ms;
    raw_.add(data, length);
    if (kind_ == config::Kind::kVoltronic) rx_voltronic(data, length, now_ms);
    else if (kind_ == config::Kind::kPylontech) rx_pylontech(data, length, now_ms);
    else if (kind_ == config::Kind::kAnt) rx_ant(data, length, now_ms);
  }

  // A whole reading, if one is ready: the topic and the JSON payload (timestamped with the node's clock, `wall_ms`). False when none is.
  bool take_message(std::uint64_t wall_ms, std::string& topic, std::string& payload) {
    if (!ready_) return false;
    ready_ = false;
    topic = solar::topic(node_id_, device_);
    if (topic.empty()) return false;
    if (kind_ == config::Kind::kVoltronic) { InverterExtras extras; extras.has_pv2 = has_pv2_; extras.pv2 = pv2_; extras.units = units_; payload = inverter_json(node_id_, device_, wall_ms, mode_, status_, warnings_, &extras); }
    else payload = battery_json(node_id_, device_, wall_ms, modules_);   // a Pylontech stack or an ANT-BMS (one module)
    last_payload_ = payload;
    ++stats_.readings;
    last_reading_ms_ = last_rx_ms_;
    return true;
  }

  // "waiting" (no cycle has finished yet), "reporting", "silent" (nothing is coming), "garbled" (bytes come, none is an answer), "listening" (a raw port that hears bytes).
  std::string state(std::uint64_t now_ms) const {
    if (kind_ == config::Kind::kRaw) return stats_.bytes_rx == 0 ? "silent" : (now_ms - last_rx_ms_ < 10000 ? "listening" : "silent");
    const std::uint64_t window = poll_ms_ * 3 + 8000;
    if (stats_.readings > 0 && now_ms - last_reading_ms_ <= window) return "reporting";
    if (stats_.bytes_rx == 0) return now_ms < poll_ms_ + 6000 && stats_.readings == 0 ? "waiting" : "silent";
    if (stats_.replies_ok == 0) return "garbled";
    return stats_.readings > 0 ? "silent" : "garbled";
  }

  const PortStats& stats() const { return stats_; }
  const RawLog& raw() const { return raw_; }
  const std::string& last_payload() const { return last_payload_; }
  config::Kind kind() const { return kind_; }
  // What the last ANT-BMS frame said besides the message (empty for any other kind, and until a frame was read): "old", "new" and the MOSFET and balancer codes.
  // For an inverter port: the dialect it speaks ("pi30", "revo", "pi18"), once it is known.
  std::string detail() const {
    if (kind_ == config::Kind::kVoltronic) return known_ ? dialect_name() : "";
    if (kind_ != config::Kind::kAnt || !ant_seen_) return "";
    return std::string(ant_reading_.protocol == ant::Protocol::kNew ? "new" : "old") + " charge=" + std::to_string(ant_reading_.charge_mos) + " discharge=" +
           std::to_string(ant_reading_.discharge_mos) + " balancer=" + std::to_string(ant_reading_.balancer) + " cells=" + std::to_string(ant_reading_.cells) +
           (ant_reading_.protecting ? " protecting" : "");
  }
  const std::string& device() const { return device_; }
  // Whether this is an ANT-BMS port that has found its BMS speaking the newer protocol (the only one whose settings can be read).
  bool ant_new_protocol() const { return kind_ == config::Kind::kAnt && ant_known_ && ant_protocol_ == ant::Protocol::kNew; }
  // The moment the node last heard something on this port, on the clock that was given (0: never).
  std::uint64_t last_rx_ms() const { return last_rx_ms_; }

 private:
  enum class Step { kIdle, kQpigs, kQmod, kQpiws, kQpigs2, kQpgs, kGs, kMod, kFws, kPwr, kBat, kInfo, kStat, kAnt };
  enum class Dialect { kPi30, kRevo, kPi18 };

  void send(std::string_view text, std::uint64_t now_ms, std::uint64_t timeout_ms) {
    pending_.assign(text.begin(), text.end());
    deadline_ms_ = now_ms + timeout_ms;
  }
  void send_query(const char* text, std::uint64_t now_ms) {
    if (dialect_ == Dialect::kPi18) voltronic::pi18::build_command(text, pending_);
    else voltronic::build_command(text, pending_);
    deadline_ms_ = now_ms + kVoltronicTimeoutMs;
  }
  const char* dialect_name() const { return dialect_ == Dialect::kPi18 ? "pi18" : dialect_ == Dialect::kRevo ? "revo" : "pi30"; }

  void begin_cycle(std::uint64_t now_ms) {
    cycle_started_ms_ = now_ms;
    buffer_.clear();
    framer_ = voltronic::ReplyFramer();
    framer18_ = voltronic::pi18::ReplyFramer();
    if (kind_ == config::Kind::kAnt) {
      step_ = Step::kAnt;
      ant_framer_.clear();
      pending_ = ant::request(ant_protocol_);
      deadline_ms_ = now_ms + kAntTimeoutMs;
    } else if (kind_ == config::Kind::kVoltronic) {
      mode_ = 0;
      warnings_.clear();
      has_pv2_ = false;
      units_.clear();
      if (dialect_ == Dialect::kPi18) { step_ = Step::kGs; send_query("GS", now_ms); }
      else { step_ = Step::kQpigs; send_query("QPIGS", now_ms); }
    } else {
      step_ = Step::kPwr;
      modules_.clear();
      module_at_ = 0;
      // A console that has been quiet for a while may swallow its first command: at the start and after a silence a bare Enter goes first, to wake it up.
      send(wake_ ? "\rpwr\r" : "pwr\r", now_ms, kConsoleTableTimeoutMs);
    }
  }

  void abandon(std::uint64_t now_ms) {
    step_ = Step::kIdle;
    pending_.clear();
    buffer_.clear();
    next_cycle_ms_ = now_ms + poll_ms_;
  }

  void finish(std::uint64_t now_ms) {
    step_ = Step::kIdle;
    ready_ = true;
    next_cycle_ms_ = cycle_started_ms_ + poll_ms_;
    if (next_cycle_ms_ < now_ms) next_cycle_ms_ = now_ms;
    buffer_.clear();
  }

  void fail(const char* error, std::uint64_t now_ms) {
    ++stats_.replies_bad;
    stats_.last_error = error;
    abandon(now_ms);
  }

  // ---- Voltronic ----
  // Both dialects' framers listen: while the dialect is not known, a whole reply of the other one is what tells it.
  void rx_voltronic(const std::uint8_t* data, std::size_t length, std::uint64_t now_ms) {
    for (std::size_t i = 0; i < length && step_ != Step::kIdle; ++i) {
      std::vector<std::uint8_t> frame;
      if (framer_.feed(data[i], frame)) on_pi30_frame(frame, now_ms);
      else if (framer18_.feed(data[i], frame)) on_pi18_frame(frame, now_ms);
    }
  }

  // The dialect answered: it is the one from now on (an inverter that only knows it can be met again after three misses).
  void learn(Dialect dialect) {
    if (!auto_) return;
    dialect_ = dialect;
    known_ = true;
    misses_ = 0;
  }
  // A whole reply of the other dialect arrived while looking for it: start over in that one, soon.
  void switch_to(Dialect dialect, std::uint64_t now_ms) {
    learn(dialect);
    abandon(now_ms);
    next_cycle_ms_ = now_ms + 100;
  }

  void on_pi30_frame(const std::vector<std::uint8_t>& frame, std::uint64_t now_ms) {
    std::string text;
    const bool looking = auto_ && !known_;
    const bool crc_ok = voltronic::parse_reply(frame.data(), frame.size(), text);
    if (dialect_ == Dialect::kPi18) {
      if (looking && crc_ok) switch_to(Dialect::kPi30, now_ms);
      return;
    }
    if (step_ == Step::kQpigs2 || step_ == Step::kQpgs) { on_optional(crc_ok, text, now_ms); return; }
    if (!crc_ok) {
      // only the REVO dialect ends a reply with the checksum: a reply that fits it (and no CRC) makes the port REVO
      if ((dialect_ == Dialect::kRevo || looking) && voltronic::parse_reply_revo(frame.data(), frame.size(), text)) { learn(Dialect::kRevo); }
      else { fail("crc", now_ms); return; }
    }
    if (text == "NAK") { learn(dialect_); fail("nak", now_ms); return; }
    const bool revo = dialect_ == Dialect::kRevo;
    if (step_ == Step::kQpigs) {
      if (!(revo ? voltronic::parse_qpigs_revo(text, status_) : voltronic::parse_qpigs(text, status_))) { fail("format", now_ms); return; }
      learn(dialect_);
      ++stats_.replies_ok;
      step_ = Step::kQmod;
      send_query("QMOD", now_ms);
    } else if (step_ == Step::kQmod) {
      if (!voltronic::parse_qmod(text, mode_)) { fail("format", now_ms); return; }
      ++stats_.replies_ok;
      if (revo) { stats_.last_error.clear(); finish(now_ms); return; }   // the REVO dialect has no warning list to ask for
      step_ = Step::kQpiws;
      send_query("QPIWS", now_ms);
    } else if (step_ == Step::kQpiws) {
      if (!voltronic::parse_qpiws(text, warnings_)) { fail("format", now_ms); return; }
      ++stats_.replies_ok;
      stats_.last_error.clear();
      after_main_readings(now_ms);
    }
  }

  // ---- the optional readings (standard dialect only) ----
  bool want_pv2() const { return pv2_wanted_ && !pv2_absent_ && dialect_ == Dialect::kPi30 && known_; }
  // The three readings that make the message are in: the second PV input and the parallel units are asked if they are wanted, and the reading goes out either way.
  void after_main_readings(std::uint64_t now_ms) {
    if (want_pv2()) { step_ = Step::kQpigs2; send_query("QPIGS2", now_ms); return; }
    begin_units(now_ms);
  }
  void begin_units(std::uint64_t now_ms) {
    unit_at_ = 0;
    if (parallel_ > 0 && dialect_ == Dialect::kPi30 && known_) { ask_unit(now_ms); return; }
    finish(now_ms);
  }
  void ask_unit(std::uint64_t now_ms) {
    if (unit_at_ >= parallel_) { finish(now_ms); return; }
    step_ = Step::kQpgs;
    send_query(("QPGS" + std::to_string(unit_at_)).c_str(), now_ms);
  }
  void miss_pv2(std::uint64_t now_ms) {
    has_pv2_ = false;
    if (++pv2_misses_ >= 2) pv2_absent_ = true;
    begin_units(now_ms);
  }
  // An answer to QPIGS2 or QPGS<n>: anything but a good one skips that step, and never spoils the reading.
  void on_optional(bool crc_ok, const std::string& text, std::uint64_t now_ms) {
    if (step_ == Step::kQpigs2) {
      voltronic::Pv2 v;
      if (crc_ok && text != "NAK" && voltronic::parse_qpigs2(text, v)) { pv2_ = v; has_pv2_ = true; pv2_misses_ = 0; ++stats_.replies_ok; begin_units(now_ms); }
      else miss_pv2(now_ms);
      return;
    }
    voltronic::ParallelUnit u;
    if (crc_ok && text != "NAK" && voltronic::parse_qpgs(text, u)) { u.number = unit_at_; units_.push_back(u); ++stats_.replies_ok; }
    ++unit_at_;
    ask_unit(now_ms);
  }

  void on_pi18_frame(const std::vector<std::uint8_t>& frame, std::uint64_t now_ms) {
    voltronic::pi18::Kind kind = voltronic::pi18::Kind::kData;
    std::string payload;
    const bool ok = voltronic::pi18::parse_reply(frame.data(), frame.size(), kind, payload);
    if (dialect_ != Dialect::kPi18) {
      if (auto_ && !known_ && ok) switch_to(Dialect::kPi18, now_ms);
      return;
    }
    if (!ok) { fail("crc", now_ms); return; }
    if (kind == voltronic::pi18::Kind::kNak) { learn(Dialect::kPi18); fail("nak", now_ms); return; }
    if (kind != voltronic::pi18::Kind::kData) { fail("format", now_ms); return; }
    if (step_ == Step::kGs) {
      if (!voltronic::pi18::parse_gs(payload, status_)) { fail("format", now_ms); return; }
      learn(Dialect::kPi18);
      ++stats_.replies_ok;
      step_ = Step::kMod;
      send_query("MOD", now_ms);
    } else if (step_ == Step::kMod) {
      if (!voltronic::pi18::parse_mod(payload, mode_)) { fail("format", now_ms); return; }
      ++stats_.replies_ok;
      step_ = Step::kFws;
      send_query("FWS", now_ms);
    } else if (step_ == Step::kFws) {
      if (!voltronic::pi18::parse_fws(payload, warnings_)) { fail("format", now_ms); return; }
      ++stats_.replies_ok;
      stats_.last_error.clear();
      finish(now_ms);
    }
  }

  // ---- Pylontech console ----
  // The console ends every answer, a good one or a refusal ("Invalid command or fail to excute."), with a line of `$$` and its prompt (`pylon>` or `pylon_debug>`):
  // the answer is whole when the text ends in that prompt. Waiting for it also leaves nothing of this answer to be taken for the next one, and a command the battery does
  // not know is over at once instead of after the timeout.
  static bool console_done(const std::string& text) { return pylontech::console_done(text); }

  void ask_next_module(std::uint64_t now_ms) {
    // the next module that is there, in order, up to the limit
    while (module_at_ < modules_.size()) {
      const pylontech::Module& m = modules_[module_at_];
      if (m.present && m.number >= 1 && m.number <= max_modules_) break;
      ++module_at_;
    }
    if (module_at_ >= modules_.size()) { finish(now_ms); return; }
    const int number = modules_[module_at_].number;
    if (!(cells_wanted_ & (1u << number))) {
      // not this module's turn: it keeps the cells of its last one
      const CellsCache& cache = cells_cache_[static_cast<std::size_t>(number)];
      pylontech::attach_cells(modules_, number, cache.cells);
      if (cache.remaining_ah >= 0) modules_[module_at_].capacity_ah = cache.remaining_ah;
      modules_[module_at_].balancing = cache.balancing;
      after_bat(now_ms);
      return;
    }
    step_ = Step::kBat;
    buffer_.clear();
    send("bat " + std::to_string(number) + "\r", now_ms, kConsoleExtraTimeoutMs);
  }

  // Which modules' cells are asked in this cycle: all of them, or the next `cells_per_cycle_` present ones in rotation (and any that has no cells yet).
  void choose_cells_to_read() {
    std::vector<int> numbers;
    for (const pylontech::Module& m : modules_) if (m.present && m.number >= 1 && m.number <= max_modules_ && m.number <= 16) numbers.push_back(m.number);
    std::sort(numbers.begin(), numbers.end());
    cells_wanted_ = 0;
    if (numbers.empty()) return;
    if (cells_per_cycle_ <= 0 || static_cast<std::size_t>(cells_per_cycle_) >= numbers.size()) {
      for (int n : numbers) cells_wanted_ |= 1u << n;
      return;
    }
    std::size_t start = 0;
    while (start < numbers.size() && numbers[start] < bat_cursor_) ++start;
    if (start >= numbers.size()) start = 0;
    int last = numbers[start];
    for (int k = 0; k < cells_per_cycle_; ++k) {
      last = numbers[(start + static_cast<std::size_t>(k)) % numbers.size()];
      cells_wanted_ |= 1u << last;
    }
    bat_cursor_ = last + 1;
    for (int n : numbers) if (cells_cache_[static_cast<std::size_t>(n)].cells.empty()) cells_wanted_ |= 1u << n;
  }

  void rx_pylontech(const std::uint8_t* data, std::size_t length, std::uint64_t now_ms) {
    if (step_ == Step::kIdle) return;
    for (std::size_t i = 0; i < length; ++i) if (buffer_.size() < 8192) buffer_ += static_cast<char>(data[i]);
    if (!console_done(buffer_)) return;
    if (step_ == Step::kPwr) {
      wake_ = false;
      std::vector<pylontech::Module> found;
      pylontech::parse_pwr(buffer_, found);
      if (found.empty()) { fail("format", now_ms); return; }
      ++stats_.replies_ok;
      stats_.last_error.clear();
      modules_ = found;
      module_at_ = 0;
      choose_cells_to_read();
      ask_next_module(now_ms);
    } else if (step_ == Step::kBat) {
      std::vector<double> cells;
      double remaining_ah = -1;
      int balancing = 0;
      if (pylontech::parse_bat(buffer_, cells, &remaining_ah, &balancing) > 0) {
        pylontech::attach_cells(modules_, modules_[module_at_].number, cells);
        if (remaining_ah >= 0) modules_[module_at_].capacity_ah = remaining_ah;
        modules_[module_at_].balancing = balancing;
        CellsCache& cache = cells_cache_[static_cast<std::size_t>(modules_[module_at_].number)];
        cache.cells = cells;
        cache.remaining_ah = remaining_ah;
        cache.balancing = balancing;
        ++stats_.replies_ok;
      }
      after_bat(now_ms);
    } else if (step_ == Step::kInfo) {
      if (pylontech::parse_info(buffer_, modules_[module_at_]) > 0) ++stats_.replies_ok;
      ask_stat(now_ms);
    } else if (step_ == Step::kStat) {
      if (pylontech::parse_info(buffer_, modules_[module_at_]) > 0) ++stats_.replies_ok;
      end_extras(now_ms);
    }
  }

  // The cells are in. The identity (`info`) and the history (`stat`) of a module hardly change: they are asked when they are not known or half an hour old, and kept.
  void after_bat(std::uint64_t now_ms) {
    pylontech::Module& m = modules_[module_at_];
    const Extras& e = extras_[static_cast<std::size_t>(m.number)];
    if (e.valid && now_ms - e.at_ms < kExtrasRefreshMs) {
      if (!e.model.empty()) m.model = e.model;
      if (e.full_capacity_ah >= 0) m.full_capacity_ah = e.full_capacity_ah;
      if (e.cycles >= 0) m.cycles = e.cycles;
      if (e.health_percent >= 0) m.health_percent = e.health_percent;
      ++module_at_;
      ask_next_module(now_ms);
      return;
    }
    had_capacity_ = m.capacity_ah >= 0;
    step_ = Step::kInfo;
    buffer_.clear();
    send("info " + std::to_string(m.number) + "\r", now_ms, kConsoleExtraTimeoutMs);
  }
  void ask_stat(std::uint64_t now_ms) {
    step_ = Step::kStat;
    buffer_.clear();
    send("stat " + std::to_string(modules_[module_at_].number) + "\r", now_ms, kConsoleExtraTimeoutMs);
  }
  // Both extras have been asked (answered or not): keep what came, unless the module also reports its remaining charge there (that one has to be fresh every time).
  void end_extras(std::uint64_t now_ms) {
    const pylontech::Module& m = modules_[module_at_];
    Extras& e = extras_[static_cast<std::size_t>(m.number)];
    e.model = m.model; e.full_capacity_ah = m.full_capacity_ah; e.cycles = m.cycles; e.health_percent = m.health_percent; e.at_ms = now_ms;
    e.valid = had_capacity_ || m.capacity_ah < 0;
    ++module_at_;
    ask_next_module(now_ms);
  }

  // ---- ANT-BMS ----
  void rx_ant(const std::uint8_t* data, std::size_t length, std::uint64_t now_ms) {
    for (std::size_t i = 0; i < length && step_ == Step::kAnt; ++i) {
      std::vector<std::uint8_t> frame;
      ant::Protocol protocol = ant::Protocol::kOld;
      if (!ant_framer_.feed(data[i], frame, protocol)) continue;
      ant::Reading reading;
      const bool ok = protocol == ant::Protocol::kOld ? ant::decode_old(frame.data(), frame.size(), reading) : ant::decode_new(frame.data(), frame.size(), reading);
      if (!ok) { ant_misses_hint(now_ms); fail("crc", now_ms); if (!ant_known_) next_cycle_ms_ = now_ms + 300; return; }
      ++stats_.replies_ok;
      stats_.last_error.clear();
      ant_protocol_ = protocol;
      ant_known_ = true;
      ant_misses_ = 0;
      ant_seen_ = true;
      ant_reading_ = reading;
      modules_.assign(1, reading.module);
      finish(now_ms);
    }
  }
  // A frame that is not whole or does not check, or silence: while the protocol is not known, the other one is tried next, soon.
  void ant_misses_hint(std::uint64_t) {
    if (!ant_known_) ant_protocol_ = ant_protocol_ == ant::Protocol::kOld ? ant::Protocol::kNew : ant::Protocol::kOld;
  }

  // A request nobody answered in time. The extras of a battery (`bat`, `info`) are not worth losing the reading for: it goes on without them. Anything
  // else gives the cycle up, and the next one starts at the next poll.
  void on_timeout(std::uint64_t now_ms) {
    ++stats_.timeouts;
    if (step_ == Step::kBat) {
      // a module that did not answer for its cells keeps the ones of its last turn
      const int number = modules_[module_at_].number;
      const CellsCache& cache = cells_cache_[static_cast<std::size_t>(number)];
      if (!cache.cells.empty()) pylontech::attach_cells(modules_, number, cache.cells);
      if (cache.remaining_ah >= 0) modules_[module_at_].capacity_ah = cache.remaining_ah;
      modules_[module_at_].balancing = cache.balancing;
      after_bat(now_ms);
      return;
    }
    if (step_ == Step::kQpigs2) { miss_pv2(now_ms); return; }
    if (step_ == Step::kQpgs) { ++unit_at_; ask_unit(now_ms); return; }
    if (step_ == Step::kInfo) { ask_stat(now_ms); return; }
    if (step_ == Step::kStat) { end_extras(now_ms); return; }
    stats_.last_error = "timeout";
    if (kind_ == config::Kind::kPylontech) wake_ = true;   // the console did not answer: the next try starts with a bare Enter
    if (kind_ == config::Kind::kVoltronic && auto_) {
      if (known_ && ++misses_ >= 3) { known_ = false; misses_ = 0; }
      if (!known_) dialect_ = dialect_ == Dialect::kPi18 ? Dialect::kPi30 : Dialect::kPi18;   // look for the other one at the next try
      abandon(now_ms);
      if (!known_) next_cycle_ms_ = now_ms + 300;
      return;
    }
    if (kind_ == config::Kind::kAnt) {
      if (ant_known_ && ++ant_misses_ >= 3) { ant_known_ = false; ant_misses_ = 0; }
      ant_misses_hint(now_ms);
      abandon(now_ms);
      if (!ant_known_) next_cycle_ms_ = now_ms + 300;   // look for the protocol without waiting a whole poll
      return;
    }
    abandon(now_ms);
  }

  config::Kind kind_;
  std::string node_id_, device_;
  std::uint64_t poll_ms_;
  int max_modules_;
  int cells_per_cycle_ = 0;
  int bat_cursor_ = 1;                // the module whose cells come next in the rotation
  std::uint32_t cells_wanted_ = 0;    // bit n: this cycle asks the cells of module n
  struct CellsCache { std::vector<double> cells; double remaining_ah = -1; int balancing = -1; };
  std::array<CellsCache, 17> cells_cache_;   // by module number: the cells and remaining charge of the module's last turn
  Step step_ = Step::kIdle;
  std::uint64_t deadline_ms_ = 0, next_cycle_ms_ = 0, cycle_started_ms_ = 0, last_rx_ms_ = 0, last_reading_ms_ = 0;
  std::vector<std::uint8_t> pending_;
  std::string buffer_;
  voltronic::ReplyFramer framer_;
  voltronic::pi18::ReplyFramer framer18_;
  Dialect dialect_ = Dialect::kPi30;
  bool auto_ = true, known_ = false;   // "auto": the dialect is looked for; known: it answered (or it was chosen)
  int misses_ = 0;
  voltronic::Status status_;
  bool pv2_wanted_ = false, pv2_absent_ = false, has_pv2_ = false;
  int pv2_misses_ = 0;
  voltronic::Pv2 pv2_;
  int parallel_ = 0, unit_at_ = 0;
  std::vector<voltronic::ParallelUnit> units_;
  char mode_ = 0;
  std::vector<std::string> warnings_;
  std::vector<pylontech::Module> modules_;
  struct Extras { std::string model; double full_capacity_ah = -1; int cycles = -1, health_percent = -1; std::uint64_t at_ms = 0; bool valid = false; };
  std::array<Extras, 17> extras_;   // by module number (1 to 16): what `info` and `stat` said, kept between cycles
  bool had_capacity_ = false;
  std::size_t module_at_ = 0;
  ant::Framer ant_framer_;
  ant::Protocol ant_protocol_ = ant::Protocol::kOld;
  ant::Reading ant_reading_;
  bool ant_known_ = false, ant_seen_ = false;
  int ant_misses_ = 0;
  bool ready_ = false;
  bool wake_ = true;                  // the next `pwr` is preceded by a bare Enter (at the start and after a silence)
  std::string last_payload_;
  PortStats stats_;
  RawLog raw_;
};

}  // namespace armor::solar

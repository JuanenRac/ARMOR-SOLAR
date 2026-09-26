// ARMOR-SOLAR - a group of ports that share one hardware UART through a multiplexer (the "mux" profile of the base board): who has the line, and for how long.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
//
// One UART, one 74HC4052 in front of it, up to four ports behind it, each with its own Poller. Only one of them can talk at a time: the group gives the line to a port whose
// cycle is due (or already under way), points the multiplexer at it, sets the speed and the polarity that port needs, drops whatever the UART still held from the port before
// and lets its Poller run until its cycle is over (a whole reading, or a failure), and then goes on to the next one in order. A port that is not due costs no time, a silent one
// costs its own timeouts and no one else's, and a "raw" port only listens for a short while, now and then.
//
// It touches no hardware: the firmware hands it a `MuxLine` (the UART and the select pins) and the clock, so the whole thing is tested on a computer with a stand-in that plays
// the equipment on every channel. Nothing here has run on a board.
#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "poller.hpp"

namespace armor::solar {

// What a group needs from the hardware.
class MuxLine {
 public:
  virtual ~MuxLine() = default;
  virtual void select(std::size_t channel) = 0;            // point the multiplexer at this channel (its select pins)
  virtual bool configure(int baud, bool invert) = 0;       // the speed and the polarity of the UART
  virtual void discard() = 0;                              // drop what the UART holds
  virtual std::size_t read(std::uint8_t* out, std::size_t capacity, unsigned wait_ms) = 0;   // waits up to `wait_ms` for bytes; 0 when none came
  virtual bool write(const std::uint8_t* data, std::size_t length) = 0;                       // false when they could not be sent
};

struct MuxClock {
  std::function<std::uint64_t()> uptime_ms;
  std::function<bool()> wall_clock_is_set;
  std::function<std::uint64_t()> wall_clock_ms;
};

// One port of the group.
struct MuxMember {
  std::size_t port = 0;       // the port's number less one (0 to 7)
  std::size_t channel = 0;    // its channel on the multiplexer
  int baud = 2400;
  bool invert = false;
  std::unique_ptr<Poller> poller;
};

// What happened in one step of the group.
struct MuxTurn {
  bool active = false;        // a port had the line in this step
  std::size_t member = 0;     // which one (its place in the group's list)
  std::size_t port = 0;       // and its number less one
  bool started = false;       // the turn began in this step
  bool ended = false;         // the turn ended in this step
  bool reading = false;       // a whole reading came out: `topic` and `payload`
  bool failed = false;        // the turn ended without a reading
  std::string topic, payload;
};

class MuxGroup {
 public:
  static constexpr std::uint64_t kMaxTurnMs = 90'000;      // no port keeps the line longer than this (a stack of eight modules with every answer late is under it)
  static constexpr std::uint64_t kListenMs = 1'500;        // how long a raw port listens in its turn
  static constexpr std::uint64_t kListenEveryMs = 6'000;   // and how often it gets one
  static constexpr std::size_t kNone = static_cast<std::size_t>(-1);

  explicit MuxGroup(std::vector<MuxMember> members) : members_(std::move(members)), next_listen_(members_.size(), 0) {}

  // One step: gives the line to a port if none has it, lets the port with the line run one round of its exchange, and says what came of it. `wait_ms` is how long to wait for bytes.
  MuxTurn step(MuxLine& line, const MuxClock& clock, unsigned wait_ms = 20) {
    MuxTurn out;
    const std::uint64_t now = clock.uptime_ms();
    if (current_ == kNone) {
      const std::size_t pick = choose(now);
      if (pick == kNone) {
        // nobody is due: whatever arrives now is nobody's, and the wait is what keeps the loop from spinning
        line.read(buffer_, sizeof buffer_, wait_ms);
        return out;
      }
      begin(pick, line, now);
      out.started = true;
    }
    MuxMember& member = members_[current_];
    out.active = true;
    out.member = current_;
    out.port = member.port;
    Poller& poller = *member.poller;
    if (poller.kind() == config::Kind::kRaw) {
      const std::size_t n = line.read(buffer_, sizeof buffer_, wait_ms);
      if (n > 0) poller.on_rx(buffer_, n, clock.uptime_ms());
      if (clock.uptime_ms() - turn_started_ms_ >= kListenMs) { next_listen_[current_] = clock.uptime_ms() + kListenEveryMs; finish(out, false); }
      return out;
    }
    const std::vector<std::uint8_t> request = poller.next_tx(now);
    if (!request.empty() && !line.write(request.data(), request.size())) ++write_failures_;
    const std::size_t n = line.read(buffer_, sizeof buffer_, wait_ms);
    if (n > 0) poller.on_rx(buffer_, n, clock.uptime_ms());
    const std::uint64_t after = clock.uptime_ms();
    if (poller.ready()) {
      std::string topic, payload;
      if (poller.take_message(clock.wall_clock_is_set() ? clock.wall_clock_ms() : after, topic, payload)) {
        out.reading = true;
        out.topic = std::move(topic);
        out.payload = std::move(payload);
        got_reading_ = true;
      }
    }
    if (!poller.busy() || after - turn_started_ms_ > kMaxTurnMs) finish(out, !got_reading_);
    return out;
  }

  const std::vector<MuxMember>& members() const { return members_; }
  bool has_line() const { return current_ != kNone; }
  std::uint32_t write_failures() const { return write_failures_; }
  std::size_t turns() const { return turns_; }

 private:
  // The next port, after the one served last, that is under way or due.
  std::size_t choose(std::uint64_t now) const {
    const std::size_t count = members_.size();
    for (std::size_t k = 1; k <= count; ++k) {
      const std::size_t i = (last_ + k) % count;
      const Poller& poller = *members_[i].poller;
      if (poller.kind() == config::Kind::kRaw) { if (now >= next_listen_[i]) return i; continue; }
      if (poller.busy() || poller.due(now)) return i;
    }
    return kNone;
  }
  void begin(std::size_t i, MuxLine& line, std::uint64_t now) {
    const MuxMember& member = members_[i];
    line.select(member.channel);
    if (!configured_ || configured_baud_ != member.baud || configured_invert_ != member.invert) {
      line.configure(member.baud, member.invert);
      configured_ = true;
      configured_baud_ = member.baud;
      configured_invert_ = member.invert;
    }
    line.discard();
    current_ = i;
    turn_started_ms_ = now;
    got_reading_ = false;
    ++turns_;
  }
  void finish(MuxTurn& out, bool failed) {
    out.ended = true;
    out.failed = failed;
    last_ = current_;
    current_ = kNone;
  }

  std::vector<MuxMember> members_;
  std::vector<std::uint64_t> next_listen_;
  std::size_t current_ = kNone, last_ = 0;
  std::uint64_t turn_started_ms_ = 0;
  bool got_reading_ = false;
  bool configured_ = false, configured_invert_ = false;
  int configured_baud_ = 0;
  std::uint32_t write_failures_ = 0;
  std::size_t turns_ = 0;
  std::uint8_t buffer_[256] = {};
};

}  // namespace armor::solar

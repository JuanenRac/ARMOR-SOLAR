// ARMOR-SOLAR - the arithmetic of an emulated (software) UART: turning the times of the edges of a line into bytes, and a byte into the bits to send.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
//
// The chip has three hardware UARTs; the node gets more serial ports by timing the line in software. Receiving does not need a fast loop: an interrupt
// on every edge of the pin notes the time and the new level, and the bytes are worked out afterwards from those times, so a busy processor (Wi-Fi) delays
// the decoding, never the timing. Sending needs a timer that changes the pin at every bit; this file only says which bits, in which order.
// 8 data bits, no parity, one stop bit (8N1), the framing of every equipment this node reads. Emulated ports are for slow lines (up to 19200 baud): the
// firmware refuses more, because the timing of an interrupt is only good to a few microseconds.
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <vector>

namespace armor::softuart {

constexpr unsigned kMaxBaud = 19200;
constexpr std::size_t kFrameBits = 10;   // start, eight data bits, stop

// The bits of one byte as they go on the line: false is a low level. The start bit first, then the data least significant bit first, then the stop bit.
constexpr std::array<bool, kFrameBits> frame_bits(std::uint8_t byte) {
  std::array<bool, kFrameBits> bits{};
  bits[0] = false;
  for (std::size_t i = 0; i < 8; ++i) bits[1 + i] = ((byte >> i) & 1U) != 0;
  bits[9] = true;
  return bits;
}

// The bits of a message, byte after byte.
inline std::vector<bool> message_bits(const std::uint8_t* data, std::size_t length) {
  std::vector<bool> bits;
  bits.reserve(length * kFrameBits);
  for (std::size_t i = 0; i < length; ++i) for (const bool bit : frame_bits(data[i])) bits.push_back(bit);
  return bits;
}

// The receiving side. Feed it the edges (time in microseconds, the level after the edge) and ask for the bytes.
class Receiver {
 public:
  explicit Receiver(unsigned baud) : bit_us_(1000000.0 / static_cast<double>(baud)) {}

  void edge(std::uint64_t time_us, bool level) { edges_.push_back({time_us, level}); }

  // Works out every byte that is complete at `now_us` and appends it to `out`. A frame whose stop bit is low counts as a framing error and is dropped.
  void poll(std::uint64_t now_us, std::vector<std::uint8_t>& out) {
    for (;;) {
      if (!in_frame_) {
        // the next falling edge after the end of the last frame is a start bit
        while (!edges_.empty() && (edges_.front().level || static_cast<double>(edges_.front().time) <= last_end_)) edges_.pop_front();
        if (edges_.empty()) return;
        start_ = edges_.front().time;
        in_frame_ = true;
      }
      const double stop_at = static_cast<double>(start_) + 9.5 * bit_us_;
      if (static_cast<double>(now_us) < stop_at + 0.1 * bit_us_) return;   // the stop bit has not been on the line long enough to be sure of
      std::uint8_t value = 0;
      for (std::size_t bit = 0; bit < 8; ++bit) if (level_at(static_cast<double>(start_) + (1.5 + static_cast<double>(bit)) * bit_us_)) value = static_cast<std::uint8_t>(value | (1U << bit));
      if (level_at(stop_at)) out.push_back(value);
      else ++framing_errors_;
      last_end_ = stop_at;
      in_frame_ = false;
      // the edges up to the end of this frame are of no use to the next one
      while (!edges_.empty() && static_cast<double>(edges_.front().time) <= stop_at) edges_.pop_front();
    }
  }

  std::uint32_t framing_errors() const { return framing_errors_; }

 private:
  struct Edge { std::uint64_t time; bool level; };

  // The level of the line at a moment: that of the last edge before it (the line rests high before the start bit).
  bool level_at(double moment) const {
    bool level = true;
    for (const Edge& e : edges_) {
      if (static_cast<double>(e.time) > moment) break;
      level = e.level;
    }
    return level;
  }

  double bit_us_;
  std::deque<Edge> edges_;
  bool in_frame_ = false;
  std::uint64_t start_ = 0;
  double last_end_ = -1.0;
  std::uint32_t framing_errors_ = 0;
};

}  // namespace armor::softuart

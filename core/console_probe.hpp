// ARMOR-SOLAR - asking a Pylontech battery's console one question from the panel, and only a question that reads.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
//
// The console of a Pylontech battery has commands that read (the table of the modules, the cells, the identity, the history) and commands that change things. The panel may
// send only the first kind, from a short list written here, and each with its argument checked: the text that goes on the wire is rebuilt from the list, never copied from the
// person's input. What the battery answers is shown as it came, so that an answer nobody has described yet (the `pwrsys` command's, for one) can be looked at, kept and turned into
// a reading later. It touches no hardware: the firmware sends what `next_tx` returns and gives `on_rx` what arrives.
//
// Nothing here has run against a real battery.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "pylontech.hpp"

namespace armor::solar::console {

// The commands that only read, without an argument and with a module number (1 to 16).
inline bool allowed(std::string_view text, std::string& canonical) {
  canonical.clear();
  std::string words;
  for (char c : text) {
    if (static_cast<unsigned char>(c) < 0x20 || static_cast<unsigned char>(c) > 0x7E) return false;   // no control characters, no bytes past ASCII
    words += (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
  }
  if (words.size() > 24) return false;
  // one or two words, single spaces
  std::size_t begin = 0;
  while (begin < words.size() && words[begin] == ' ') ++begin;
  std::size_t end = words.size();
  while (end > begin && words[end - 1] == ' ') --end;
  words = words.substr(begin, end - begin);
  const std::size_t gap = words.find(' ');
  const std::string name = words.substr(0, gap);
  std::string argument = gap == std::string::npos ? "" : words.substr(gap + 1);
  argument.erase(0, argument.find_first_not_of(' ') == std::string::npos ? argument.size() : argument.find_first_not_of(' '));   // more than one space between the words is one
  if (name.empty()) return false;
  if (gap == std::string::npos) {
    if (name == "help" || name == "pwr" || name == "pwrsys") { canonical = name; return true; }
    return false;
  }
  if (name != "bat" && name != "info" && name != "stat" && name != "soh" && name != "data") return false;
  if (argument.empty() || argument.size() > 2 || argument.find(' ') != std::string::npos) return false;
  int module = 0;
  for (char c : argument) { if (c < '0' || c > '9') return false; module = module * 10 + (c - '0'); }
  if (argument.size() == 2 && argument[0] == '0') return false;
  if (module < 1 || module > 16) return false;
  canonical = name + " " + std::to_string(module);
  return true;
}

// One question and its answer.
class Probe {
 public:
  static constexpr std::uint64_t kTimeoutMs = 8'000;
  static constexpr std::size_t kMaxAnswer = 12'288;

  bool running() const { return state_ == State::kRunning; }
  const char* state() const { return state_ == State::kIdle ? "idle" : state_ == State::kRunning ? "running" : state_ == State::kDone ? "done" : "error"; }
  const std::string& command() const { return command_; }
  const std::string& text() const { return text_; }
  const std::string& error() const { return error_; }

  // Starts a question. False (and the reason in `why`) when the text is not one that reads, or one is already under way.
  bool start(std::string_view text, std::uint64_t now_ms, std::string& why) {
    if (running()) { why = "busy"; return false; }
    std::string canonical;
    if (!allowed(text, canonical)) { why = "not_allowed"; return false; }
    command_ = canonical;
    text_.clear();
    error_.clear();
    truncated_ = false;
    pending_ = true;
    started_ms_ = now_ms;
    state_ = State::kRunning;
    return true;
  }

  // The bytes to send now, once.
  std::vector<std::uint8_t> next_tx(std::uint64_t now_ms) {
    if (state_ != State::kRunning) return {};
    if (now_ms - started_ms_ > kTimeoutMs) { fail("timeout"); return {}; }
    if (!pending_) return {};
    pending_ = false;
    std::vector<std::uint8_t> out(command_.begin(), command_.end());
    out.push_back('\r');
    return out;
  }

  void on_rx(const std::uint8_t* data, std::size_t length, std::uint64_t now_ms) {
    if (state_ != State::kRunning || pending_) return;   // nothing was asked yet: what arrives is not the answer
    for (std::size_t i = 0; i < length; ++i) {
      if (text_.size() < kMaxAnswer) text_ += static_cast<char>(data[i]);
      else truncated_ = true;
    }
    if (pylontech::console_done(text_)) { state_ = State::kDone; return; }
    if (now_ms - started_ms_ > kTimeoutMs) fail("timeout");
  }

  // Called now and then while waiting, so that a silent battery ends the question.
  void tick(std::uint64_t now_ms) { if (state_ == State::kRunning && !pending_ && now_ms - started_ms_ > kTimeoutMs) fail("timeout"); }
  bool truncated() const { return truncated_; }

 private:
  enum class State { kIdle, kRunning, kDone, kError };
  void fail(const char* why) { state_ = State::kError; error_ = why; }

  State state_ = State::kIdle;
  std::string command_, text_, error_;
  bool pending_ = false, truncated_ = false;
  std::uint64_t started_ms_ = 0;
};

}  // namespace armor::solar::console

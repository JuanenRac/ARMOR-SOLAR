// ARMOR-SOLAR - host tests of asking a Pylontech battery's console one question: which questions may be asked, and how an answer (or its absence) is taken.
// The console is a stand-in written from the public descriptions of its text; no real battery has been asked.
#include <cstdio>
#include <string>
#include <vector>

#include "../core/console_probe.hpp"

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

using namespace armor::solar;
using namespace armor::solar::console;

static std::string canon(const std::string& text) {
  std::string out;
  return allowed(text, out) ? out : "REFUSED";
}
static std::vector<std::uint8_t> bytes(const std::string& text) { return std::vector<std::uint8_t>(text.begin(), text.end()); }

static void test_what_may_be_asked() {
  for (const char* c : {"help", "pwr", "pwrsys"}) CHECK(canon(c) == c);
  for (const char* c : {"bat", "info", "stat", "soh", "data"}) {
    CHECK(canon(std::string(c) + " 1") == std::string(c) + " 1");
    CHECK(canon(std::string(c) + " 16") == std::string(c) + " 16");
    CHECK(canon(std::string(c) + " 0") == "REFUSED" && canon(std::string(c) + " 17") == "REFUSED" && canon(std::string(c) + " 01") == "REFUSED" && canon(std::string(c) + " 99") == "REFUSED");
    CHECK(canon(c) == "REFUSED");                                        // they need a module
  }
  // tidy input is accepted and rebuilt; nothing else gets through
  CHECK(canon("  PWR  ") == "pwr" && canon("Bat   3") == "bat 3" && canon("INFO 12") == "info 12" && canon("\tpwr") == "REFUSED");
  for (const char* c : {"", " ", "ctrl", "unlock", "reboot", "reset", "bat set 1", "bat 1 2", "bat 1;ctrl", "pwr\rctrl", "pwr\nunlock", "pwr;pwr", "help me", "pwrsys 1", "bat -1", "bat 1.5", "bat a",
                        "bat 1\r", "pwr ctrl", "soh 1 2", "data x", "dat 1", "pw", "pwrs", "config", "sn", "log", "time", "ntp", "date", "shutdown", "bms", "bat  1 2"}) {
    CHECK(canon(c) == "REFUSED");
  }
  CHECK(canon(std::string("pwr") + '\0') == "REFUSED" && canon("pwr\x7F") == "REFUSED" && canon("pwr\xC3\xA9") == "REFUSED");
  CHECK(canon(std::string(30, 'a')) == "REFUSED" && canon("bat " + std::string(30, ' ') + "1") == "REFUSED");
  // what goes on the wire is rebuilt from the list: the input's own letters and spaces never do
  Probe p;
  std::string why;
  CHECK(p.start("  BAT   2 ", 0, why));
  const std::vector<std::uint8_t> out = p.next_tx(1);
  CHECK(std::string(out.begin(), out.end()) == "bat 2\r");
}

static void test_a_question_and_its_answer() {
  Probe p;
  std::string why;
  CHECK(std::string(p.state()) == "idle" && !p.running() && p.next_tx(0).empty());
  CHECK(!p.start("ctrl", 0, why) && why == "not_allowed" && std::string(p.state()) == "idle");
  CHECK(p.start("pwrsys", 1000, why) && p.running() && std::string(p.state()) == "running" && p.command() == "pwrsys");
  const std::vector<std::uint8_t> asked = p.next_tx(1000);
  CHECK(std::string(asked.begin(), asked.end()) == "pwrsys\r" && p.next_tx(1010).empty());        // it is asked once
  const std::string reply = "@\r\nSystem Volt   : 51234 mV\r\nSystem Curr   : -3000 mA\r\nCommand completed successfully\r\n$$\r\npylon>";
  // in pieces
  const std::vector<std::uint8_t> all = bytes(reply);
  for (std::size_t i = 0; i < all.size(); i += 7) {
    CHECK(p.running());
    p.on_rx(all.data() + i, std::min<std::size_t>(7, all.size() - i), 1100 + i);
  }
  CHECK(std::string(p.state()) == "done" && p.text() == reply && p.error().empty() && !p.truncated());
  // a second question can be asked once the first is over, and forgets the first
  CHECK(p.start("bat 1", 5000, why) && p.text().empty() && p.running());
  // a question while another runs is refused
  CHECK(!p.start("pwr", 5001, why) && why == "busy");
}

static void test_a_refusal_is_an_answer_too() {
  Probe p;
  std::string why;
  CHECK(p.start("data 3", 0, why));
  p.next_tx(0);
  const std::vector<std::uint8_t> refusal = bytes("@\r\nInvalid command or fail to excute.\r\n$$\r\npylon>");
  p.on_rx(refusal.data(), refusal.size(), 100);
  CHECK(std::string(p.state()) == "done" && p.text().find("Invalid command") != std::string::npos);
}

static void test_silence_ends_the_question() {
  Probe p;
  std::string why;
  CHECK(p.start("pwrsys", 0, why));
  p.next_tx(0);
  p.tick(7000);
  CHECK(p.running());
  p.tick(8001);
  CHECK(std::string(p.state()) == "error" && p.error() == "timeout" && p.text().empty());
  // half an answer does not finish it either
  Probe q;
  CHECK(q.start("help", 0, why));
  q.next_tx(0);
  const std::vector<std::uint8_t> half = bytes("@\r\nhelp     Commands\r\n");
  q.on_rx(half.data(), half.size(), 100);
  CHECK(q.running());
  q.tick(9000);
  CHECK(std::string(q.state()) == "error" && q.error() == "timeout" && q.text() == "@\r\nhelp     Commands\r\n");   // what came is kept, to be looked at
  // bytes that arrive before anything was asked are not an answer
  Probe r;
  const std::vector<std::uint8_t> noise = bytes("pylon>");
  r.on_rx(noise.data(), noise.size(), 0);
  CHECK(std::string(r.state()) == "idle" && r.text().empty());
  CHECK(r.start("pwr", 0, why));
  r.on_rx(noise.data(), noise.size(), 0);                    // asked in the API, not yet on the wire
  CHECK(r.running() && r.text().empty());
}

static void test_a_long_answer_is_cut() {
  Probe p;
  std::string why;
  CHECK(p.start("help", 0, why));
  p.next_tx(0);
  const std::string chunk(1000, 'x');
  for (int i = 0; i < 20; ++i) p.on_rx(reinterpret_cast<const std::uint8_t*>(chunk.data()), chunk.size(), 100 + i);
  CHECK(p.running() && p.text().size() == Probe::kMaxAnswer && p.truncated());
  const std::vector<std::uint8_t> end = bytes("\r\n$$\r\npylon>");
  p.on_rx(end.data(), end.size(), 200);
  p.tick(9000);
  CHECK(std::string(p.state()) == "error");    // the prompt came after the cut: the answer cannot be known whole, and the text is what was kept
}

int main() {
  test_what_may_be_asked();
  test_a_question_and_its_answer();
  test_a_refusal_is_an_answer_too();
  test_silence_ends_the_question();
  test_a_long_answer_is_cut();
  std::printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}

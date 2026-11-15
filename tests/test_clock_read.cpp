// The 64-bit clock's read rule (Clock::Combine, ReadClock in src/clock_read.hpp) against a model of
// the 32-bit timer and its wrap interrupt, over every interleaving of the five reads with the
// counter's ticks and the interrupt, near the wrap; and the sequence that made the earlier rule
// step forward by 2^32 us and back. Test code. SPDX-License-Identifier: MIT
// Copyright (c) 2026 The port-stm32 authors
#include <array>
#include <cstdint>

#include "clock_read.hpp"
#include "support.hpp"

namespace {
constexpr uint64_t kWrap = uint64_t{1U} << 32U;

// The timer: the true count, the update flag (UIF) and the interrupt's wrap count. The interrupt
// is atomic (it runs with interrupts masked), so it is one step of the model.
struct Timer {
  uint64_t now{0U};
  bool pending{false};
  uint32_t wraps{0U};
  void Tick() {
    ++now;
    if ((now % kWrap) == 0U) {
      pending = true; // the update event sets UIF with the counter's reload
    }
  }
  void Interrupt() {
    if (pending) {
      pending = false;
      ++wraps;
    }
  }
};

// What happens before one read: some ticks and, before or after them, the interrupt (or not).
struct Step {
  uint8_t ticks{0U};
  uint8_t interrupt{0U}; // 0 none, 1 before the ticks, 2 after them
};

// A sample source that applies one Step before each of its reads (five per sample).
class ScriptedSource {
public:
  ScriptedSource(Timer& timer, const Step* steps, std::size_t count) noexcept
      : timer_(timer), steps_(steps), count_(count) {}
  stm::ClockSample Sample() {
    stm::ClockSample s{};
    advance();
    s.high_before = timer_.wraps;
    advance();
    s.counter_before = static_cast<uint32_t>(timer_.now);
    lo_ = (lo_ == 0U) ? timer_.now : lo_;
    advance();
    s.wrap_pending = timer_.pending;
    advance();
    s.counter_after = static_cast<uint32_t>(timer_.now);
    hi_ = timer_.now;
    advance();
    s.high_after = timer_.wraps;
    return s;
  }
  stm::ClockSample SampleMasked() {
    masked_ = true; // the interrupt cannot run inside it
    return Sample();
  }
  [[nodiscard]] uint64_t Low() const {
    return lo_;
  }
  [[nodiscard]] uint64_t High() const {
    return hi_;
  }
  [[nodiscard]] bool Masked() const {
    return masked_;
  }

private:
  void advance() {
    const Step step = (next_ < count_) ? steps_[next_] : Step{};
    ++next_;
    if ((step.interrupt == 1U) && !masked_) {
      timer_.Interrupt();
    }
    for (uint8_t i = 0U; i < step.ticks; ++i) {
      timer_.Tick();
    }
    if ((step.interrupt == 2U) && !masked_) {
      timer_.Interrupt();
    }
  }
  Timer& timer_;
  const Step* steps_;
  std::size_t count_;
  std::size_t next_{0U};
  uint64_t lo_{0U};
  uint64_t hi_{0U};
  bool masked_{false};
};

constexpr std::array<Step, 6> kChoices{Step{0U, 0U}, Step{1U, 0U}, Step{0U, 1U},
                                       Step{1U, 1U}, Step{1U, 2U}, Step{2U, 2U}};
} // namespace

KB_TEST("clock read: Combine takes the first counter unless a wrap is pending, then the second") {
  uint64_t us = 0U;
  KB_CHECK(stm::Clock::Combine(stm::ClockSample{0U, 5U, false, 6U, 0U}, us));
  KB_CHECK(us == 5U);
  KB_CHECK(stm::Clock::Combine(stm::ClockSample{2U, 7U, false, 8U, 2U}, us));
  KB_CHECK(us == ((uint64_t{2U} << 32U) | 7U));
  // Wrapped before the flag read, not yet counted: the second counter is after the wrap.
  KB_CHECK(stm::Clock::Combine(stm::ClockSample{2U, 0xFFFFFFFFU, true, 1U, 2U}, us));
  KB_CHECK(us == ((uint64_t{3U} << 32U) | 1U));
  // The interrupt ran inside the sample: inconsistent, sampled again.
  KB_CHECK(!stm::Clock::Combine(stm::ClockSample{2U, 7U, false, 8U, 3U}, us));
}

KB_TEST(
    "clock read: the sequence that stepped +2^32 and back under the earlier rule is monotonic") {
  // The earlier rule read high, counter, high, flag and took the second high when they differed:
  // (7, 0xFFFFFFF0, 7) gave 0x7FFFFFFF0, (7, 0xFFFFFFF8, 8) gave 0x8FFFFFFF8, (8, 0x10, 8) gave
  // 0x800000010: +4294967304 us, then -4294967272 us.
  Timer timer{};
  timer.now = (uint64_t{7U} << 32U) | 0xFFFFFFF0U;
  timer.wraps = 7U;
  uint64_t last = 0U;
  {
    ScriptedSource first{timer, nullptr, 0U};
    last = stm::ReadClock(first);
    KB_CHECK(last == ((uint64_t{7U} << 32U) | 0xFFFFFFF0U));
  }
  {
    // The counter is read at 0xFFFFFFF8, then it wraps and the interrupt runs before the second
    // wrap-count read: exactly the interleaving of the review's probe.
    timer.now = (uint64_t{7U} << 32U) | 0xFFFFFFF7U;
    std::array<Step, 5> steps{Step{1U, 0U}, Step{0U, 0U}, Step{8U, 2U}, Step{8U, 0U}, Step{0U, 0U}};
    ScriptedSource second{timer, steps.data(), steps.size()};
    const uint64_t now = stm::ReadClock(second);
    KB_CHECK(now >= last);
    KB_CHECK(now < (last + 64U)); // no jump of 2^32
    last = now;
  }
  {
    ScriptedSource third{timer, nullptr, 0U};
    const uint64_t now = stm::ReadClock(third);
    KB_CHECK(now >= last);
    KB_CHECK(now == ((uint64_t{8U} << 32U) | (timer.now & 0xFFFFFFFFU)));
  }
}

KB_TEST("clock read: every interleaving of the reads, the ticks and the interrupt near a wrap") {
  // Starts from 4 before to 3 after a wrap, with the wrap counted or still pending, and every
  // choice of ticks and interrupt before each of the first sample's five reads (6^5 schedules),
  // then a second call: each result lies between the true counts at its first and last counter
  // read, and the second is never below the first.
  uint32_t runs = 0U;
  uint32_t bad = 0U;
  uint32_t retried = 0U;
  for (int offset = -4; offset <= 3; ++offset) {
    for (int pending_start = 0; pending_start <= 1; ++pending_start) {
      const uint64_t start = (uint64_t{5U} * kWrap) + static_cast<uint64_t>(int64_t{offset});
      const bool after_wrap = offset >= 0;
      if ((pending_start == 1) && !after_wrap) {
        continue; // a pending wrap exists only after the wrap
      }
      for (uint32_t code = 0U; code < 7776U; ++code) { // 6^5
        std::array<Step, 10> steps{};
        uint32_t c = code;
        for (std::size_t i = 0U; i < 5U; ++i) {
          steps[i] = kChoices[c % 6U];
          c /= 6U;
        }
        Timer timer{};
        timer.now = start;
        timer.wraps = after_wrap ? ((pending_start == 1) ? 4U : 5U) : 4U;
        timer.pending = (pending_start == 1);
        const uint64_t before = timer.now;
        ScriptedSource source{timer, steps.data(), steps.size()};
        const uint64_t first = stm::ReadClock(source);
        const uint64_t after = timer.now;
        retried += source.Masked() ? 1U : 0U;
        Timer& t = timer;
        ScriptedSource again{t, nullptr, 0U};
        const uint64_t second = stm::ReadClock(again);
        const bool ok = (first >= before) && (first <= after) && (second >= first);
        bad += ok ? 0U : 1U;
        ++runs;
      }
    }
  }
  KB_CHECK_EQ(bad, 0U);
  KB_CHECK(runs > 50000U);
  KB_CHECK_EQ(retried, 0U); // one interrupt per wrap: a second sample is always consistent
}

namespace {
// A source whose unmasked samples are always inconsistent (the interrupt ran inside each).
struct AlwaysInterrupted {
  uint32_t unmasked{0U};
  uint32_t masked{0U};
  stm::ClockSample Sample() {
    ++unmasked;
    return stm::ClockSample{1U, 10U, false, 11U, 2U};
  }
  stm::ClockSample SampleMasked() {
    ++masked;
    return stm::ClockSample{2U, 20U, false, 21U, 2U};
  }
};
} // namespace

KB_TEST("clock read: three inconsistent samples fall back to one taken with interrupts masked") {
  AlwaysInterrupted source{};
  const uint64_t us = stm::ReadClock(source);
  KB_CHECK_EQ(source.unmasked, stm::kClockReadTries);
  KB_CHECK_EQ(source.masked, 1U);
  KB_CHECK(us == ((uint64_t{2U} << 32U) | 20U));
}

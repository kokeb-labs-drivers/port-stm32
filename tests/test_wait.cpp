// Deadline and Completion (completion.hpp): every port wait is bounded on the port's own clock (the
// DWT before a Clock runs), never on HAL_GetTick, and ends even when no clock advances (the poll
// cap). Test code. SPDX-License-Identifier: MIT
// Copyright (c) 2026 The port-stm32 authors
#include "support.hpp"

KB_TEST("wait: a completion done before the wait returns at once with its code") {
  Fresh();
  stm::Completion done{};
  KB_REQUIRE(done.Create() == Status::Ok);
  KB_CHECK_EQ(done.Create(), Status::Ok); // idempotent
  done.Arm();
  done.Complete(0x401U, 7U);
  stm::Deadline deadline{kb::Milliseconds{10U}};
  KB_CHECK(done.Wait(deadline));
  KB_CHECK_EQ(done.Code(), 0x401U);
  KB_CHECK_EQ(done.Value(), 7U);
  done.Arm(); // a new operation: not done, the stale wake-up dropped
  KB_CHECK(!done.Done());
}

KB_TEST("wait: a completion never done ends after its budget, with the HAL tick standing still") {
  Fresh();
  fake::SetTickStep(0U);
  stm::Completion done{};
  KB_REQUIRE(done.Create() == Status::Ok);
  done.Arm();
  stm::Deadline deadline{kb::Milliseconds{3U}};
  KB_CHECK(!done.Wait(deadline));
  KB_CHECK(deadline.Expired());
  KB_CHECK_EQ(deadline.Remaining().Count(), 0U);
  KB_CHECK(!done.Pause(deadline)); // no pause once expired
  KB_CHECK(deadline.ElapsedUs() >= 3000U);
  KB_CHECK_EQ(fake::Ticks(), 0U);
}

KB_TEST("wait: with no clock advancing at all, the poll cap still ends the wait") {
  Fresh();
  fake::SetCycleStep(0U);
  fake::SetCounterStep(0U);
  stm::Completion done{};
  KB_REQUIRE(done.Create() == Status::Ok);
  done.Arm();
  stm::Deadline deadline{kb::Milliseconds{1U}};
  KB_CHECK(!done.Wait(deadline)); // (1 + 1) x kDeadlinePollsPerMs polls at most
  KB_CHECK(deadline.Expired());
}

KB_TEST("wait: a zero budget expires at once; Remaining rounds up") {
  Fresh();
  stm::Deadline none{kb::Milliseconds{0U}};
  KB_CHECK(none.Expired());
  stm::Deadline some{kb::Milliseconds{5U}};
  const uint32_t left = some.Remaining().Count();
  KB_CHECK((left >= 1U) && (left <= 5U));
}

KB_TEST("wait: under a blocking RTOS a stopped clock still ends the wait by the slept time") {
  Fresh();
  fake::SetCycleStep(0U); // the DWT and the port's timer both stand still
  fake::SetCounterStep(0U);
  stm::Completion done{};
  KB_REQUIRE(done.Create() == Status::Ok);
  done.Arm();
  stm::Deadline deadline{kb::Milliseconds{3U}, true}; // sleeps block: each timed-out slice counts
  uint32_t pauses = 0U;
  while (done.Pause(deadline)) {
    ++pauses;
    KB_REQUIRE(pauses < 100U); // the poll cap (60000) would allow far more
  }
  KB_CHECK_EQ(pauses, 5U); // 3 ms slices until 3 + 10 ms have been slept
  KB_CHECK(deadline.Expired());
}

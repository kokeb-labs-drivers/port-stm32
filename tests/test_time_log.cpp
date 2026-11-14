// Clock (the 64-bit extension of a 32-bit timer), BusyWait (DWT) and the log sinks (ITM, Uart).
// Test code. SPDX-License-Identifier: MIT
// Copyright (c) 2026 The port-stm32 authors
#include <bit>
#include <string>
#include <string_view>

#include "support.hpp"

KB_TEST("clock: Combine extends the counter and corrects for a pending wrap") {
  static_assert(stm::Clock::Combine(0U, 5U, 0U, false) == 5U);
  static_assert(stm::Clock::Combine(2U, 7U, 2U, false) == ((uint64_t{2U} << 32U) | 7U));
  static_assert(stm::Clock::Combine(2U, 7U, 2U, true) ==
                ((uint64_t{3U} << 32U) | 7U)); // wrapped, not counted
  static_assert(stm::Clock::Combine(2U, 0xFFFFFFF0U, 2U, true) ==
                ((uint64_t{2U} << 32U) | 0xFFFFFFF0U));
  static_assert(stm::Clock::Combine(2U, 7U, 3U, true) ==
                ((uint64_t{3U} << 32U) | 7U)); // counted meanwhile
}

KB_TEST("clock: Start configures the 32-bit timer at 1 MHz with its wrap interrupt") {
  Fresh();
  const stm::Clock before{};
  KB_CHECK(before.Now().Us() == 0U); // not started yet
  stm::ClockConfig cfg{};
  cfg.timer = 2U;
  cfg.irq_priority = 2U;
  stm::Clock clock{cfg};
  KB_CHECK_EQ(clock.Start(), Status::Ok);
  KB_CHECK_EQ(clock.Start(), Status::Ok);
  KB_CHECK_EQ(fake::Calls("HAL_TIM_Base_Init"), 1U);
  KB_CHECK_EQ(fake::TimInit(2).Prescaler, 159U); // 160 MHz timer clock
  KB_CHECK_EQ(fake::TimInit(2).Period, 0xFFFFFFFFU);
  KB_CHECK(fake::IrqEnabled(TIM2_IRQn));
  KB_CHECK_EQ(fake::IrqPriority(TIM2_IRQn), 2U);
  kb_fake_tim[2].CNT = 1234U;
  KB_CHECK(clock.Now().Us() == 1234U);
  kb_fake_tim[2].SR = TIM_FLAG_UPDATE;
  stm::HandleClockIrq(2U);
  KB_CHECK_EQ(kb_fake_tim[2].SR, 0U);
  KB_CHECK(clock.Now().Us() == ((uint64_t{1U} << 32U) | 1234U));
  kb_fake_tim[2].SR = TIM_FLAG_UPDATE; // wrapped, interrupt not run yet
  kb_fake_tim[2].CNT = 3U;
  KB_CHECK(clock.Now().Us() == ((uint64_t{2U} << 32U) | 3U));
  stm::HandleClockIrq(2U);
  stm::HandleClockIrq(2U); // no flag: nothing counted
  KB_CHECK(clock.Now().Us() == ((uint64_t{2U} << 32U) | 3U));
  stm::HandleClockIrq(30U); // not a timer
  KB_CHECK(clock.LastError().IsOk());
}

KB_TEST("clock: TIM5, a given timer clock, refusals and failures") {
  Fresh();
  stm::ClockConfig cfg{};
  cfg.timer = 5U;
  cfg.timer_clock_hz = 84000000U;
  stm::Clock clock{cfg};
  fake::Fail("HAL_TIM_Base_Init", HAL_ERROR);
  KB_CHECK_EQ(clock.Start(), Status::Fault);
  fake::Fail("HAL_TIM_Base_Start", HAL_BUSY);
  KB_CHECK_EQ(clock.Start(), Status::Busy);
  KB_CHECK_EQ(clock.Start(), Status::Ok);
  KB_CHECK_EQ(fake::TimInit(5).Prescaler, 83U);
  KB_CHECK(fake::IrqEnabled(TIM5_IRQn));
  stm::ClockConfig bad{};
  bad.timer = 3U; // 16-bit
  stm::Clock wrong{bad};
  KB_CHECK_EQ(wrong.Start(), Status::InvalidArgument);
}

KB_TEST("busy wait: cycles at the core clock, rounded up; the DWT is enabled on first use") {
  static_assert(stm::BusyWait::Cycles(1U, 16000000U) == 16U);
  static_assert(stm::BusyWait::Cycles(1U, 1500000U) == 2U); // 1.5 cycles round up
  static_assert(stm::BusyWait::Cycles(999U, 480000000U) == 479520U);
  Fresh();
  stm::BusyWait wait{};
  wait.WaitUs(kb::Microseconds{100U});
  KB_CHECK(fake::CyclesRead() >= 1600U);
  KB_CHECK((kb_fake_dwt.CTRL & DWT_CTRL_CYCCNTENA_Msk) != 0U);
  KB_CHECK((kb_fake_coredebug.DEMCR & CoreDebug_DEMCR_TRCENA_Msk) != 0U);
  fake::SetCycleStep(0x10000000U);
  SystemCoreClock = 480000000U;
  wait.WaitUs(kb::Microseconds{4000000U}); // long waits are split, the counter never wraps a step
  KB_CHECK(fake::CyclesRead() >= (uint64_t{4U} * 480000000U));
}

KB_TEST("log: the line format, cut at the limit") {
  std::array<char, 200> out{};
  std::size_t n = stm::FormatLogLine(kb::iface::LogLevel::Error, "bus down", out);
  KB_CHECK(std::string_view(out.data(), n) == "E bus down\r\n");
  n = stm::FormatLogLine(kb::iface::LogLevel::Warning, "w", out);
  KB_CHECK(std::string_view(out.data(), n) == "W w\r\n");
  n = stm::FormatLogLine(kb::iface::LogLevel::Info, "i", out);
  KB_CHECK(std::string_view(out.data(), n) == "I i\r\n");
  n = stm::FormatLogLine(kb::iface::LogLevel::Debug, "d", out);
  KB_CHECK(std::string_view(out.data(), n) == "D d\r\n");
  const std::string long_line(400U, 'x');
  n = stm::FormatLogLine(kb::iface::LogLevel::Info, long_line, out);
  KB_CHECK_EQ(n, stm::kLogMaxLine + 4U);
  std::array<char, 3> tiny{};
  KB_CHECK_EQ(stm::FormatLogLine(kb::iface::LogLevel::Info, "x", tiny), 0U);
  std::array<char, 6> small{};
  KB_CHECK_EQ(stm::FormatLogLine(kb::iface::LogLevel::Info, "xyzw", small), 6U); // cut to fit
}

KB_TEST("log: ITM drops lines without a probe and waits a bounded time per character") {
  Fresh();
  stm::ItmLogSink itm{};
  itm.Write(kb::iface::LogLevel::Info, "no probe");
  kb_fake_itm.TCR = ITM_TCR_ITMENA_Msk;
  kb_fake_itm.TER = 1U;
  kb_fake_itm.PORT[0].u32 = 1U; // free
  itm.Write(kb::iface::LogLevel::Info, "hi");
  KB_CHECK_EQ(kb_fake_itm.PORT[0].u8, std::bit_cast<uint8_t>('\n')); // the last character written
  kb_fake_itm.PORT[0].u32 = 0U; // busy: dropped after the spins
  itm.Write(kb::iface::LogLevel::Info, "busy");
  KB_CHECK_EQ(kb_fake_itm.PORT[0].u32, 0U);
}

KB_TEST("log: UartLogSink writes the line on the Uart") {
  Fresh();
  stm::UartConfig c{};
  c.tx = PinId{GpioPort::A, 9U, 7U};
  c.rx = PinId{GpioPort::A, 10U, 7U};
  c.instance = 1U;
  stm::Uart uart{c};
  KB_REQUIRE(uart.EnsureReady() == Status::Ok);
  stm::UartLogSink log{uart};
  log.Write(kb::iface::LogLevel::Warning, "low battery");
  const std::string sent(fake::UartSent().begin(), fake::UartSent().end());
  KB_CHECK(sent == "W low battery\r\n");
}

// The log sinks (ITM, Uart) and the line format.
// Test code. SPDX-License-Identifier: MIT
// Copyright (c) 2026 The port-stm32 authors
#include <bit>
#include <string>
#include <string_view>

#include "support.hpp"

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

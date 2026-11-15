// The log sinks (log.hpp): the line format, ItmLogSink on the ITM stimulus port and UartLogSink on
// a port Uart. Platform-free; the ITM goes through sdk.hpp (src/stm32_io_boundary.cpp).
// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The port-stm32 authors
#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
// The LogSink contract takes a std::string_view (kb/interfaces/log.hpp).
// misra-lint: ignore KB-CPP-012
#include <string_view>

#include "kb/interfaces/types.hpp"
#include "kb/port/stm32/log.hpp"
#include "kb/port/stm32/uart.hpp"
#include "sdk.hpp"

namespace kb::port::stm32 {

// --- log --------------------------------------------------------------------------------------

std::size_t FormatLogLine(kb::iface::LogLevel level, std::string_view text,
                          std::span<char> out) noexcept {
  constexpr std::size_t kFrame = 4U; // "<letter> " and "\r\n"
  std::size_t n = 0U;
  if (out.size() >= kFrame) {
    char letter = 'D';
    if (level == kb::iface::LogLevel::Error) {
      letter = 'E';
    } else if (level == kb::iface::LogLevel::Warning) {
      letter = 'W';
    } else if (level == kb::iface::LogLevel::Info) {
      letter = 'I';
    } else {
      letter = 'D';
    }
    out[0] = letter;
    out[1] = ' ';
    const std::size_t body = std::min({text.size(), kLogMaxLine, out.size() - kFrame});
    for (std::size_t i = 0U; i < body; ++i) {
      out[2U + i] = text[i];
    }
    out[2U + body] = '\r';
    out[3U + body] = '\n';
    n = kFrame + body;
  }
  return n;
}

// NOLINTNEXTLINE(readability-convert-member-functions-to-static): the contract calls a member
void ItmLogSink::Write(kb::iface::LogLevel level, std::string_view text) noexcept {
  std::array<char, kLogMaxLine + 4U> line{};
  const std::size_t n = FormatLogLine(level, text, line);
  bool sent = sdk::ItmEnabled();
  for (std::size_t i = 0U; sent && (i < n); ++i) {
    uint32_t spins = 0U;
    while (!sdk::ItmPortFree() && (spins < kItmWaitSpins)) {
      ++spins; // a bounded wait for the probe to take the last character
    }
    sent = sdk::ItmPortFree();
    if (sent) {
      sdk::ItmWrite(line[i]);
    }
  }
}

void UartLogSink::Write(kb::iface::LogLevel level, std::string_view text) noexcept {
  std::array<char, kLogMaxLine + 4U> line{};
  const std::size_t n = FormatLogLine(level, text, line);
  std::array<uint8_t, kLogMaxLine + 4U> bytes{};
  for (std::size_t i = 0U; i < n; ++i) {
    bytes[i] = std::bit_cast<uint8_t>(line[i]); // the same bits, no numeric conversion
  }
  static_cast<void>(uart_.Write(std::span<const uint8_t>{bytes.data(), n}));
}

} // namespace kb::port::stm32

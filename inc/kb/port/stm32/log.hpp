/**
 * @file log.hpp
 * @brief Two `kb::iface::LogSink`s: `ItmLogSink` (lines on the Cortex-M ITM stimulus port 0, read
 * through SWO by a debug probe) and `UartLogSink` (lines on a port Uart).
 *
 * Both write one line per call as `<letter> text\r\n` (`E`, `W`, `I`, `D`), cut at kLogMaxLine
 * characters, and never allocate. ItmLogSink drops the line when the ITM or port 0 is not enabled
 * (no probe attached) and waits at most kItmWaitSpins polls per character for the port to be free.
 * UartLogSink drops the line when the Uart refuses it (its write_timeout bounds the call).
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 The port-stm32 authors
 */
#ifndef KB_PORT_STM32_LOG_HPP
#define KB_PORT_STM32_LOG_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
// The LogSink contract takes a std::string_view (kb/interfaces/log.hpp).
// misra-lint: ignore KB-CPP-012
#include <string_view>

#include "kb/interfaces/log.hpp"
#include "kb/interfaces/types.hpp"
#include "kb/port/stm32/uart.hpp"

namespace kb::port::stm32 {

/** @brief The longest line written; longer text is cut. */
inline constexpr std::size_t kLogMaxLine = 160U;
/** @brief Polls of the ITM port per character before the line is dropped. */
inline constexpr uint32_t kItmWaitSpins = 10000U;

/** @brief One formatted line: `<letter> text\r\n`. Returns the bytes used of `out`. */
[[nodiscard]] std::size_t FormatLogLine(kb::iface::LogLevel level, std::string_view text,
                                        std::span<char> out) noexcept;

/** @brief Log lines on ITM stimulus port 0 (SWO); satisfies kb::iface::LogSink. */
class ItmLogSink {
public:
  /** @brief Writes one line at `level`. */
  void Write(kb::iface::LogLevel level, std::string_view text) noexcept;
};

/** @brief Log lines on a Uart; satisfies kb::iface::LogSink. */
class UartLogSink {
public:
  /** @brief Writes on `uart`, which must outlive the sink and be ready. */
  explicit UartLogSink(Uart& uart) noexcept : uart_(uart) {}
  /** @brief Writes one line at `level`. */
  void Write(kb::iface::LogLevel level, std::string_view text) noexcept;

private:
  Uart& uart_;
};

static_assert(kb::iface::LogSink<ItmLogSink>, "ItmLogSink satisfies kb::iface::LogSink");
static_assert(kb::iface::LogSink<UartLogSink>, "UartLogSink satisfies kb::iface::LogSink");

} // namespace kb::port::stm32

#endif // KB_PORT_STM32_LOG_HPP

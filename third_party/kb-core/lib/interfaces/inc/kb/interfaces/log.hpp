/**
 * @file log.hpp
 * @brief The log contract: `kb::iface::LogSink` (bounded, no allocation) and `LogSinkRef`.
 *
 * `void Write(LogLevel level, std::string_view text) noexcept` writes one line; a sink may cut long
 * text and may drop lines under load, but never blocks without bound and never allocates.
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 The kb-core authors
 */
#ifndef KB_INTERFACES_LOG_HPP
#define KB_INTERFACES_LOG_HPP

#include <concepts>
#include <string_view>

#include "kb/interfaces/types.hpp"

namespace kb::iface {

/** @brief Where log lines go. */
template <typename L>
concept LogSink = requires(L& sink, LogLevel level, std::string_view text) {
  { sink.Write(level, text) } noexcept -> std::same_as<void>;
};

/** @brief The function table behind a LogSinkRef. */
struct LogSinkOps {
  void (*write)(void* self, LogLevel level, std::string_view text) noexcept; ///< Write.
};

namespace detail {
/// The LogSinkOps of an implementation type T.
template <LogSink T>
inline constexpr LogSinkOps kLogSinkOps{
    [](void* self, LogLevel level, std::string_view text) noexcept {
      // codeql::misra_deviation_next_line(DEV-KBC-004)
      static_cast<T*>(self)->Write(level, text);
    },
};
} // namespace detail

/** @brief A non-owning, run-time chosen reference to any LogSink. */
class LogSinkRef {
public:
  /** @brief Refers to `sink`, which must outlive the reference. */
  template <LogSink T>
  constexpr explicit LogSinkRef(T& sink) noexcept : self_(&sink), ops_(&detail::kLogSinkOps<T>) {}
  /** @brief LogSink::Write. */
  void Write(LogLevel level, std::string_view text) noexcept {
    ops_->write(self_, level, text);
  }

private:
  void* self_;
  const LogSinkOps* ops_;
};

static_assert(LogSink<LogSinkRef>, "LogSinkRef satisfies the contract it erases");

} // namespace kb::iface

#endif // KB_INTERFACES_LOG_HPP

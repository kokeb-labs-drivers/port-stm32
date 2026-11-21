// AdcStreamCore: the start checks of an ADC scan stream (the HAL time base runs, the kernel clock
// is not 0, the configuration and the board's stream shape agree), the choice between the DMA
// producer and the one-shot fallback (a stream assigned, the conversion count read back equal to
// the frame's elements), the circular stream armed before the ADC, the interrupt's drain (every DMA
// flag serviced by its own clear bit, the ADC overrun read and cleared, which halves finished,
// whether the DMA lapped), the halt with proof, and the fallback's one-shot scan. Platform-free;
// the ADC registers are behind sdk.hpp (src/boundary/adc_stream.cpp, and src/boundary/adc.cpp for
// the one-shot conversions), the stream in src/dma.cpp. SPDX-License-Identifier: MIT Copyright (c)
// 2026 The port-stm32 authors
#include <cstddef>
#include <cstdint>
#include <span>

#include "kb/dma/assignment.hpp"
#include "kb/port/stm32/adc.hpp"
#include "kb/port/stm32/adc_stream.hpp"
#include "kb/port/stm32/completion.hpp"
#include "kb/port/stm32/dma.hpp"
#include "kb/port/stm32/error.hpp"
#include "kb/utils/status.hpp"
#include "kb/utils/time.hpp"
#include "sdk.hpp"
#include "state.hpp"

namespace kb::port::stm32 {

namespace {
constexpr kb::Milliseconds kNever{0xFFFFFFFFU};
constexpr std::size_t kSampleBytes = 2U;
constexpr std::size_t kFrames = 2U;
} // namespace

AdcStreamCore::~AdcStreamCore() noexcept {
  Close();
}

bool AdcStreamCore::configValid() const noexcept {
  bool channels_ok =
      (config_.channels.size() == ranks_) && (ranks_ > 0U) && (ranks_ <= kAdcMaxRanks);
  for (const uint8_t channel : config_.channels) {
    channels_ok = channels_ok && sdk::AdcExists(config_.instance, channel);
  }
  return channels_ok && (config_.now != nullptr) && (config_.frames_per_pass > 0U) &&
         (config_.shape.frames == kFrames) && (config_.shape.element_bytes == kSampleBytes) &&
         ConfiguredTimeoutValid(config_.conversion_timeout) &&
         RtosIrqPriorityValid(config_.irq_priority);
}

kb::Instant AdcStreamCore::Now() const noexcept {
  return (config_.now != nullptr) ? config_.now() : kb::Instant{0U};
}

uint32_t AdcStreamCore::CyclesSince(uint32_t started) noexcept {
  return sdk::Cycles() - started;
}

bool AdcStreamCore::claim() const noexcept {
  AdcEntry& entry = sdk::State().adcs[config_.instance];
  const IrqGuard guard{};
  const bool free =
      static_cast<AdcPhase>(entry.phase.load(std::memory_order_relaxed)) == AdcPhase::Closed;
  if (free) {
    entry.phase.store(static_cast<uint8_t>(AdcPhase::Streaming), std::memory_order_relaxed);
  }
  return free;
}

void AdcStreamCore::unclaim() const noexcept {
  AdcEntry& entry = sdk::State().adcs[config_.instance];
  const IrqGuard guard{};
  entry.phase.store(static_cast<uint8_t>(AdcPhase::Closed), std::memory_order_relaxed);
}

uint32_t AdcStreamCore::openFallback() noexcept {
  uint32_t code = hal::kOk;
  if (!adc_open_) {
    code = (conversion_.Create() == kb::Status::Ok) ? hal::kOk : port_code::kRtosObject;
    if (code == hal::kOk) {
      code = sdk::AdcOpen(config_.instance, conversion_, config_.irq_priority);
    }
    adc_open_ = code == hal::kOk;
  }
  return code;
}

uint32_t AdcStreamCore::startChecks(bool& claimed) noexcept {
  uint32_t code = hal::kOk;
  const bool valid = configValid();
  claimed = valid && claim();
  if (!valid) {
    code = port_code::kInvalidArgument;
  } else if (!claimed) {
    code = port_code::kOwned; // the instance serves AdcChannel objects (or another stream)
  } else if (!HalTickRuns()) {
    code = port_code::kHalTickStopped; // before the scheduler and its time base: too early
  } else {
    // The kernel clock is read, never selected: its multiplexer is shared board start-up state.
    code = sdk::AdcClockCheck();
  }
  return code;
}

uint32_t AdcStreamCore::chooseProducer() noexcept {
  uint32_t code = hal::kOk;
  conversions_ = 0U;
  if (config_.dma.engine == kEngineNone) {
    reason_ = AdcFallback::NoStream;
  } else if (config_.shape.elements != ranks_) {
    reason_ = AdcFallback::Shape;
  } else {
    code =
        sdk::AdcStreamOpen(config_.instance, config_.channels, config_.sampling, config_.trigger);
    if (code == hal::kOk) {
      // The count the peripheral is configured for decides: a ring whose frame differs from the
      // scan wraps on the wrong boundary.
      conversions_ = sdk::AdcStreamConversions(config_.instance);
      reason_ = (conversions_ == config_.shape.elements) ? AdcFallback::None : AdcFallback::Shape;
      if (reason_ != AdcFallback::None) {
        static_cast<void>(sdk::AdcStreamClose(config_.instance));
      }
    }
  }
  return code;
}

uint32_t AdcStreamCore::openStream() noexcept {
  const DmaPeripheral end{.address = sdk::AdcStreamDataRegister(config_.instance),
                          .width = DmaWidth::HalfWord,
                          .dir = kb::dma::Direction::FromDevice};
  const DmaStreamLimits limits{.max_items = kFrames * ranks_, .fallback_max_bytes = 0U};
  kb::dma::StreamAssignment a = config_.dma;
  a.mode = kb::dma::ChannelMode::Circular;
  a.irq = kb::dma::IrqUse::All;
  uint32_t code =
      (dma_.Bind(a, end, limits) == kb::Status::Ok) ? hal::kOk : port_code::kInvalidArgument;
  if (code == hal::kOk) {
    static_cast<void>(dma_.EnsureReady());
    code = dma_.LastError().Detail();
  }
  if (code != hal::kOk) {
    static_cast<void>(sdk::AdcStreamClose(config_.instance));
  }
  return code;
}

uint32_t AdcStreamCore::Open() noexcept {
  uint32_t code = hal::kOk;
  if (!open_) {
    bool claimed = false;
    code = startChecks(claimed);
    if (code == hal::kOk) {
      code = chooseProducer();
    }
    if (code == hal::kOk) {
      code = (reason_ == AdcFallback::None) ? openStream() : openFallback();
    }
    open_ = code == hal::kOk;
    if (!open_ && claimed) {
      unclaim();
    }
  }
  static_cast<void>(error_.Set(MapHalStatus(code & 0xFFU), code));
  return code;
}

uint32_t AdcStreamCore::Arm(std::span<std::byte> ring, std::size_t bytes) noexcept {
  uint32_t code = open_ ? hal::kOk : port_code::kNotReady;
  if (code == hal::kOk) {
    static_cast<void>(dma_.Begin(ring, bytes, kb::utils::Deadline::After(kb::Instant{0U}, kNever)));
    code = dma_.LastError().Detail();
  }
  if (code == hal::kOk) {
    static_cast<void>(
        sdk::AdcStreamTakeOverrun(config_.instance)); // a stale flag is not this run's
    code = sdk::AdcStreamStart(config_.instance);
    if (code != hal::kOk) {
      static_cast<void>(dma_.Stop());
    }
  }
  return code;
}

kb::dma::StopEvidence AdcStreamCore::Halt(bool& overrun) noexcept {
  kb::dma::StopEvidence evidence = kb::dma::StopEvidence::Confirmed;
  overrun = false;
  if (open_ && (reason_ == AdcFallback::None)) {
    static_cast<void>(sdk::AdcStreamStop(config_.instance));
    evidence = dma_.Stop();
    overrun = sdk::AdcStreamTakeOverrun(config_.instance);
    if (evidence == kb::dma::StopEvidence::Confirmed) {
      // Cancel the circular pass's latch so that the next Arm() can start.
      static_cast<void>(dma_.Cancel());
      static_cast<void>(dma_.TakeEnd());
    }
  }
  return evidence;
}

uint32_t AdcStreamCore::EnterFallback(AdcFallback reason) noexcept {
  uint32_t code = hal::kOk;
  if (open_ && (reason_ == AdcFallback::None)) {
    dma_.SetIrqHook(nullptr);
    static_cast<void>(dma_.Release());
    static_cast<void>(sdk::AdcStreamClose(config_.instance));
    reason_ = reason;
    code = openFallback();
  }
  return code;
}

void AdcStreamCore::Close() noexcept {
  if (open_) {
    if (reason_ == AdcFallback::None) {
      dma_.SetIrqHook(nullptr);
      static_cast<void>(dma_.Release());
      static_cast<void>(sdk::AdcStreamClose(config_.instance));
    }
    if (adc_open_) {
      static_cast<void>(sdk::AdcClose(config_.instance));
      adc_open_ = false;
    }
    open_ = false;
    unclaim();
  }
}

uint32_t AdcStreamCore::OneShotScan(std::span<uint16_t> out) noexcept {
  uint32_t code = adc_open_ ? hal::kOk : port_code::kNotReady;
  for (std::size_t rank = 0U;
       (code == hal::kOk) && (rank < out.size()) && (rank < config_.channels.size()); ++rank) {
    conversion_.Arm();
    code = sdk::AdcStart(config_.instance, config_.channels[rank], config_.sampling);
    if (code == hal::kOk) {
      Deadline deadline{config_.conversion_timeout};
      if (conversion_.Wait(deadline)) {
        code = conversion_.Code();
        out[rank] = static_cast<uint16_t>(conversion_.Value() & 0xFFFFU);
      } else {
        static_cast<void>(sdk::AdcStop(config_.instance));
        code = hal::kTimeout;
      }
    }
  }
  return code;
}

AdcDrain AdcStreamCore::Drain() noexcept {
  AdcDrain d{};
  d.started = sdk::Cycles();
  dma_.OnIrq(); // every flag of the stream, errors first, each by its own clear bit
  const uint32_t flags = dma_.LastServiced();
  d.overrun = sdk::AdcStreamTakeOverrun(config_.instance);
  d.flags = flags | (d.overrun ? kAdcFlagOverrun : 0U);
  d.half0 = (flags & kDmaFlagHalf) != 0U;
  d.half1 = (flags & kDmaFlagComplete) != 0U;
  d.lapped = d.half0 && d.half1;
  return d;
}

bool AdcStreamCore::Inside(std::size_t half) const noexcept {
  const std::size_t items = kFrames * ranks_;
  const auto left = static_cast<std::size_t>(dma_.Remaining());
  const std::size_t pos = (left <= items) ? ((items - left) % items) : 0U;
  const std::size_t start = half * ranks_;
  // The DMA's next write is inside the half (not at its first item): the copy may mix two passes.
  return (pos > start) && (pos < (start + ranks_));
}

} // namespace kb::port::stm32

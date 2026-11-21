// SpiDmaPath: the streams of one SPI bus bound to its data registers, their priorities checked
// against the SPI interrupt's, and one full-duplex DMA frame: the staging buffers lent, the receive
// request, both streams, the transmit request and the peripheral enabled in the family's order, the
// wait for the receive stream's completion on the port's clock, the transmit stream concluded, the
// confirmed stop of both after an error or the deadline, the requests disabled, the leases settled.
// Platform-free; the SPI registers are behind sdk.hpp (src/boundary/spi_dma.cpp), the streams in
// src/dma.cpp.
// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The port-stm32 authors
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>

#include "kb/dma/assignment.hpp"
#include "kb/dma/capabilities.hpp"
#include "kb/port/stm32/completion.hpp"
#include "kb/port/stm32/dma.hpp"
#include "kb/port/stm32/error.hpp"
#include "kb/port/stm32/spi_dma.hpp"
#include "kb/utils/status.hpp"
#include "kb/utils/time.hpp"
#include "sdk.hpp"
#include "state.hpp"

namespace kb::port::stm32 {

namespace {
// The synchronous frame keeps its own deadline on the port's clock; the streams' report deadline
// never ends first.
constexpr kb::Milliseconds kNever{0xFFFFFFFFU};
constexpr uint32_t kIdlePolls = 4096U; // the shift register drains within a few SCK periods

[[nodiscard]] kb::utils::Deadline Never() noexcept {
  return kb::utils::Deadline::After(kb::Instant{0U}, kNever);
}
} // namespace

uint32_t SpiDmaPath::Ready(uint8_t instance, uint8_t irq_priority) noexcept {
  const DmaStreamLimits limits{.max_items = stage_bytes_, .fallback_max_bytes = 0U};
  const DmaPeripheral rx_end{.address = sdk::SpiDmaRxRegister(instance),
                             .width = DmaWidth::Byte,
                             .dir = kb::dma::Direction::FromDevice};
  const DmaPeripheral tx_end{.address = sdk::SpiDmaTxRegister(instance),
                             .width = DmaWidth::Byte,
                             .dir = kb::dma::Direction::ToDevice};
  // One priority for the SPI interrupt and both of its streams, so that an error and a completion
  // of one frame never preempt each other; the receive stream must interrupt at its completion.
  const bool priorities =
      (config_.rx.irq_priority == irq_priority) && (config_.tx.irq_priority == irq_priority);
  const bool shape = (config_.rx.irq == kb::dma::IrqUse::All) &&
                     (config_.rx.mode == kb::dma::ChannelMode::Normal) &&
                     (config_.tx.mode == kb::dma::ChannelMode::Normal) &&
                     (config_.min_bytes > 0U) && (stage_bytes_ > 0U) && (stage_bytes_ <= 65535U);
  uint32_t code = (priorities && shape) ? hooks_.place(*this) : port_code::kInvalidArgument;
  if ((code == hal::kOk) && ((rx_.Bind(config_.rx, rx_end, limits) != kb::Status::Ok) ||
                             (tx_.Bind(config_.tx, tx_end, limits) != kb::Status::Ok))) {
    code = port_code::kInvalidArgument;
  }
  if (code == hal::kOk) {
    static_cast<void>(rx_.EnsureReady());
    code = rx_.LastError().Detail();
  }
  if (code == hal::kOk) {
    static_cast<void>(tx_.EnsureReady());
    code = tx_.LastError().Detail();
  }
  if (code != hal::kOk) {
    Close();
  }
  return code;
}

void SpiDmaPath::Close() noexcept {
  rx_.SetNotify(nullptr, false);
  tx_.SetNotify(nullptr, false);
  static_cast<void>(rx_.Release());
  static_cast<void>(tx_.Release());
}

void SpiDmaPath::count(uint32_t SpiDmaStats::* field) noexcept {
  const IrqGuard guard{};
  ++(stats_.*field);
}

uint32_t SpiDmaPath::Frame(uint8_t instance, std::span<const uint8_t> tx, std::span<uint8_t> rx,
                           Deadline& deadline, Completion& done) noexcept {
  const std::size_t bytes = std::max(tx.size(), rx.size());
  uint32_t code = hal::kOk;
  if (Faulted()) {
    code = port_code::kQuarantined;
  } else if (bytes > stage_bytes_) {
    code = port_code::kTooLarge;
    count(&SpiDmaStats::too_large);
  } else {
    std::span<std::byte> tx_memory{};
    std::span<std::byte> rx_memory{};
    code = hooks_.lend(*this, tx, bytes, tx_memory, rx_memory);
    if (code == hal::kOk) {
      count(&SpiDmaStats::frames);
      code = run(instance, tx_memory, rx_memory, bytes, deadline, done);
    }
    hooks_.settle(*this, rx, code == hal::kOk);
  }
  return code;
}

uint32_t SpiDmaPath::run(uint8_t instance, std::span<std::byte> tx, std::span<std::byte> rx,
                         std::size_t bytes, Deadline& deadline, Completion& done) noexcept {
  uint32_t code = sdk::SpiDmaPrepare(instance, static_cast<uint32_t>(bytes));
  if (code == hal::kOk) {
    rx_.SetNotify(&done, false);
    tx_.SetNotify(&done, true); // only a transmit error ends the frame early
    done.Arm();
    // Receive first: its request is on before the transmit stream can clock a byte out.
    static_cast<void>(rx_.Begin(rx, bytes, Never()));
    code = rx_.LastError().Detail();
    if (code == hal::kOk) {
      static_cast<void>(tx_.Begin(tx, bytes, Never()));
      code = tx_.LastError().Detail();
      if (code != hal::kOk) {
        static_cast<void>(rx_.Cancel());
      }
    }
    if (code == hal::kOk) {
      sdk::SpiDmaGo(instance);
      code = finishStreams(instance, done.Wait(deadline));
    }
    sdk::SpiDmaFinish(instance);
  }
  return code;
}

uint32_t SpiDmaPath::finishStreams(uint8_t instance, bool woke) noexcept {
  uint32_t code = hal::kOk;
  const DmaPoll r = rx_.Poll(kb::Instant{0U});
  if (woke && (r.state == DmaPollState::Done) && (r.status == kb::Status::Ok)) {
    code = finishDone(instance);
  } else if (r.state == DmaPollState::Done) {
    // A receive stream error ended the frame (its stream is stopped): stop the transmit stream.
    static_cast<void>(tx_.Cancel());
    count(&SpiDmaStats::errors);
    code = rx_.Faulted() ? port_code::kQuarantined : r.detail;
  } else {
    code = finishStopped(instance);
  }
  return code;
}

uint32_t SpiDmaPath::finishDone(uint8_t instance) noexcept {
  // The last byte was received, so the last byte was sent: the transmit stream is done too.
  const kb::Status t = tx_.Conclude();
  bool idle = sdk::SpiDmaIdle(instance);
  for (uint32_t i = 0U; (i < kIdlePolls) && !idle; ++i) {
    idle = sdk::SpiDmaIdle(instance);
  }
  const uint32_t errors = sdk::SpiDmaErrors(instance);
  uint32_t code = hal::kOk;
  if (t == kb::Status::Fault) {
    code = tx_.Faulted() ? port_code::kQuarantined : VendorCode(port_code::kDma, 0U);
  } else if ((t != kb::Status::Ok) || (errors != 0U)) {
    code = VendorCode(hal::kError, errors);
    count(&SpiDmaStats::bus_errors);
  } else {
    code = hal::kOk;
  }
  return code;
}

uint32_t SpiDmaPath::finishStopped(uint8_t instance) noexcept {
  // The deadline, or a transmit error: an SPI overrun shows why the receive did not finish.
  const uint32_t errors = sdk::SpiDmaErrors(instance);
  const DmaPoll t = tx_.Poll(kb::Instant{0U}); // a transmit error's report, before the stops
  const kb::Status rs = rx_.Cancel();
  const kb::Status ts = tx_.Cancel();
  uint32_t code = hal::kOk;
  if ((rs != kb::Status::Ok) || (ts != kb::Status::Ok) || Faulted()) {
    code = port_code::kQuarantined;
  } else if ((t.state == DmaPollState::Done) && (t.status == kb::Status::Fault)) {
    code = t.detail;
    count(&SpiDmaStats::errors);
  } else if (errors != 0U) {
    code = VendorCode(hal::kError, errors);
    count(&SpiDmaStats::bus_errors);
  } else {
    code = VendorCode(hal::kTimeout, 0U);
    count(&SpiDmaStats::timeouts);
  }
  return code;
}

kb::Status SpiDmaPath::Recover() noexcept {
  kb::Status status = rx_.Recover();
  if (status == kb::Status::Ok) {
    status = tx_.Recover();
  }
  if (status == kb::Status::Ok) {
    status = hooks_.recover(*this);
  }
  if (status == kb::Status::Ok) {
    count(&SpiDmaStats::recoveries);
  }
  return status;
}

kb::dma::DmaHealth SpiDmaPath::Health() const noexcept {
  const kb::dma::DmaHealth r = rx_.Health();
  const kb::dma::DmaHealth t = tx_.Health();
  kb::dma::DmaHealth h{};
  h.mode = kb::dma::TransferMode::Dma;
  h.faulted = r.faulted || t.faulted;
  h.quarantined = r.quarantined + t.quarantined;
  const SpiDmaStats s = Stats();
  h.errors = r.errors + t.errors + s.bus_errors;
  return h;
}

SpiDmaStats SpiDmaPath::Stats() const noexcept {
  const IrqGuard guard{};
  return stats_;
}

} // namespace kb::port::stm32

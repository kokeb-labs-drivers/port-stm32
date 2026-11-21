// SpiBus and SpiDevice: argument checks, the prescaler, the bus lock, per-device mode and clock,
// chip select around every frame, interrupt-driven chunks of 65535 bytes waited for on the port's
// clock, and abort and re-initialisation after a timeout; the choice of the DMA path for frames of
// its minimum size (src/spi_dma.cpp), its capabilities and its recovery.
// Platform-free; every HAL call goes through sdk.hpp (src/boundary/spi.cpp).
// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The port-stm32 authors
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>

#include "kb/dma/assignment.hpp"
#include "kb/dma/capabilities.hpp"
#include "kb/interfaces/spi.hpp"
#include "kb/port/stm32/completion.hpp"
#include "kb/port/stm32/error.hpp"
#include "kb/port/stm32/spi.hpp"
#include "kb/port/stm32/spi_dma.hpp"
#include "kb/utils/status.hpp"
#include "sdk.hpp"
#include "state.hpp"

namespace kb::port::stm32 {

namespace {
constexpr uint8_t kMaxMode = 3U;
constexpr uint8_t kMaxExponent = 7U;   // divide by 256
constexpr std::size_t kChunk = 65535U; // HAL_SPI_* take a uint16_t size

[[nodiscard]] bool PinOk(PinId pin) noexcept {
  return pin.Used() && pin.Valid() && sdk::PortExists(pin.port);
}

[[nodiscard]] bool SpiBusConfigValid(const SpiBusConfig& c) noexcept {
  return sdk::SpiExists(c.instance) && PinOk(c.sck) && (PinOk(c.mosi) || PinOk(c.miso)) &&
         (!c.mosi.Used() || PinOk(c.mosi)) && (!c.miso.Used() || PinOk(c.miso)) &&
         RtosIrqPriorityValid(c.irq_priority);
}

[[nodiscard]] bool Created(const kb::Result<void>& r) noexcept {
  return r.IsOk() || (r.Error() == kb::Status::AlreadyStarted);
}
} // namespace

uint8_t SpiPrescalerExponent(uint32_t kernel_hz, uint32_t hz, bool& ok) noexcept {
  uint8_t exponent = 0U;
  while ((exponent < kMaxExponent) && ((kernel_hz >> (exponent + 1U)) > hz)) {
    ++exponent;
  }
  ok = (hz > 0U) && ((kernel_hz >> (exponent + 1U)) <= hz) && (kernel_hz > 0U);
  return exponent;
}

// --- SpiBus --------------------------------------------------------------------------------------

SpiBus::~SpiBus() noexcept {
  static_cast<void>(Release());
}

kb::Status SpiBus::EnsureReady() noexcept {
  kb::Status status = kb::Status::Ok;
  if (!ready_) {
    const bool valid = SpiBusConfigValid(config_);
    kernel_hz_ = config_.kernel_clock_hz;
    if (valid && (kernel_hz_ == 0U)) {
      kernel_hz_ = sdk::SpiKernelClockHz(config_.instance);
    }
    uint32_t code = hal::kOk;
    if (!valid) {
      code = port_code::kInvalidArgument;
    } else if (kernel_hz_ == 0U) {
      code = port_code::kNoClock;
    } else if (!Created(mutex_.Create()) || (done_.Create() != kb::Status::Ok)) {
      code = port_code::kRtosObject;
    } else {
      code = sdk::SpiOpen(config_, done_);
      if ((code == hal::kOk) && (dma_ != nullptr)) {
        // A declared DMA path that cannot start (placement, MPU, priorities) fails the start
        // closed.
        code = dma_->Ready(config_.instance, config_.irq_priority);
        if (code != hal::kOk) {
          static_cast<void>(sdk::SpiClose(config_.instance));
        }
      }
    }
    status = error_.Set(MapHalStatus(code & 0xFFU), code);
    ready_ = status == kb::Status::Ok;
    ceiling_hz_ = SpiInterruptCeilingHz(sdk::CoreClockHz(), sdk::SpiHasFifo());
    mode_ = 0xFFU;
    prescaler_ = 0xFFU;
  }
  return status;
}

kb::Status SpiBus::Release() noexcept {
  kb::Status status = kb::Status::Ok;
  if (ready_) {
    if (dma_ != nullptr) {
      dma_->Close();
    }
    const uint32_t code = sdk::SpiClose(config_.instance);
    status = error_.Set(MapHalStatus(code & 0xFFU), code);
    ready_ = false;
  }
  return status;
}

uint32_t SpiBus::SelectLocked(uint8_t mode, uint8_t prescaler) noexcept {
  uint32_t code = hal::kOk;
  if ((mode != mode_) || (prescaler != prescaler_)) {
    code = sdk::SpiConfigure(config_.instance, mode, prescaler);
    mode_ = (code == hal::kOk) ? mode : 0xFFU;
    prescaler_ = (code == hal::kOk) ? prescaler : 0xFFU;
  }
  return code;
}

uint32_t SpiBus::TransferLocked(std::span<const uint8_t> tx, std::span<uint8_t> rx,
                                Deadline& deadline) noexcept {
  done_.Arm();
  uint32_t code = sdk::SpiStart(config_.instance, tx, rx);
  if (code == hal::kOk) {
    if (done_.Wait(deadline)) {
      code = done_.Code();
    } else {
      // Mid-frame: abort (the device initialises the peripheral again before its next frame).
      const uint32_t abort = sdk::SpiAbort(config_.instance);
      code = VendorCode(hal::kTimeout, abort >> 8U);
    }
  }
  return code;
}

kb::Status SpiBus::AttachDma(SpiDmaPath& path) noexcept {
  kb::Status status = kb::Status::InvalidArgument;
  if (!ready_ && (dma_ == nullptr)) {
    dma_ = &path;
    status = kb::Status::Ok;
  }
  return status;
}

bool SpiBus::DmaFrame(std::size_t bytes) const noexcept {
  return (dma_ != nullptr) && (bytes >= dma_->MinBytes());
}

uint32_t SpiBus::DmaTransferLocked(std::span<const uint8_t> tx, std::span<uint8_t> rx,
                                   Deadline& deadline) noexcept {
  return (dma_ != nullptr) ? dma_->Frame(config_.instance, tx, rx, deadline, done_)
                           : port_code::kUnsupported;
}

kb::dma::Capabilities SpiBus::Capabilities() const noexcept {
  kb::dma::Capabilities c{};
  if (dma_ != nullptr) {
    c.mode = kb::dma::TransferMode::Dma;
    c.confirmed_abort = true;
    c.max_frame_bytes = dma_->MaxBytes();
    c.staging_bytes = dma_->MaxBytes();
    c.align_bytes = dma_->AlignBytes();
  } else {
    c.mode = kb::dma::TransferMode::Interrupt;
    c.confirmed_abort = false;
    c.max_frame_bytes = kSpiNoDmaMaxFrame;
    c.staging_bytes = 0U;
    c.align_bytes = 1U;
  }
  c.zero_copy = false;
  return c;
}

kb::dma::DmaHealth SpiBus::Health() const noexcept {
  kb::dma::DmaHealth h{};
  if (dma_ != nullptr) {
    h = dma_->Health();
  } else {
    h.mode = kb::dma::TransferMode::Interrupt;
  }
  return h;
}

kb::dma::BusLimits SpiBus::Limits() const noexcept {
  kb::dma::BusLimits b{};
  b.dma = dma_ != nullptr;
  b.max_frame_dma = (dma_ != nullptr) ? dma_->MaxBytes() : 0U;
  b.max_frame_no_dma = kSpiNoDmaMaxFrame;
  b.rx_length_multiple = 1U;
  b.align_bytes = (dma_ != nullptr) ? dma_->AlignBytes() : 1U;
  return b;
}

kb::Status SpiBus::Recover() noexcept {
  kb::Status status = kb::Status::Ok;
  if (dma_ != nullptr) {
    status = dma_->Recover();
  }
  mode_ = 0xFFU; // the next frame initialises the peripheral again
  prescaler_ = 0xFFU;
  return error_.Set(status, (status == kb::Status::Ok) ? hal::kOk : port_code::kQuarantined);
}

// --- SpiDevice -----------------------------------------------------------------------------------

SpiDevice::~SpiDevice() noexcept {
  static_cast<void>(Release());
}

bool SpiDevice::configValid() const noexcept {
  return PinOk(config_.cs) && (config_.mode <= kMaxMode) && (config_.clock_hz > 0U) &&
         ConfiguredTimeoutValid(config_.timeout);
}

uint32_t SpiDevice::limited(uint32_t hz) const noexcept {
  const uint32_t ceiling = bus_.InterruptCeilingHz();
  return ((ceiling != 0U) && (hz > ceiling)) ? ceiling : hz;
}

void SpiDevice::noteClock(uint32_t asked) noexcept {
  const bool lowered = limited(asked) != asked;
  const uint32_t actual = ActualClockHz();
  const IrqGuard guard{};
  stats_.actual_clock_hz = actual;
  stats_.clamps += lowered ? 1U : 0U;
}

uint32_t SpiDevice::ActualClockHz() const noexcept {
  return bus_.KernelClockHz() >> (prescaler_ + 1U);
}

uint32_t SpiDevice::ActualDmaClockHz() const noexcept {
  return bus_.KernelClockHz() >> (dma_prescaler_ + 1U);
}

uint32_t SpiDevice::claimCs() const noexcept {
  const uint32_t claim = ClaimPin(config_.cs);
  uint32_t code = claim;
  if (claim == hal::kOk) {
    code = sdk::PinOutput(config_.cs, false, Pull::None, Speed::High, true); // released: high
    if (code != hal::kOk) {
      UnclaimPin(config_.cs);
    }
  }
  return code;
}

kb::Status SpiDevice::EnsureReady() noexcept {
  kb::Status status = kb::Status::Ok;
  if (!ready_) {
    bool ok = false;
    if (!configValid()) {
      status = error_.Set(kb::Status::InvalidArgument, port_code::kInvalidArgument);
    } else if (bus_.EnsureReady() != kb::Status::Ok) {
      status = error_.Set(kb::Status::NotReady, port_code::kNotReady);
    } else {
      prescaler_ = SpiPrescalerExponent(bus_.KernelClockHz(), limited(config_.clock_hz), ok);
      bool dma_ok = true;
      dma_prescaler_ = bus_.HasDma()
                           ? SpiPrescalerExponent(bus_.KernelClockHz(), config_.clock_hz, dma_ok)
                           : prescaler_;
      const uint32_t code = (ok && dma_ok) ? claimCs() : port_code::kInvalidArgument;
      status = error_.Set(MapHalStatus(code & 0xFFU), code);
      ready_ = status == kb::Status::Ok;
      if (ready_) {
        noteClock(config_.clock_hz);
      }
    }
  }
  return status;
}

kb::Status SpiDevice::Release() noexcept {
  if (ready_) {
    sdk::PinReset(config_.cs);
    UnclaimPin(config_.cs);
    ready_ = false;
  }
  return error_.Clear();
}

uint32_t SpiDevice::frame(std::span<const uint8_t> tx, std::span<uint8_t> rx) noexcept {
  const std::size_t total = std::max(tx.size(), rx.size());
  const bool dma = bus_.DmaFrame(total);
  uint32_t code = bus_.SelectLocked(config_.mode, dma ? dma_prescaler_ : prescaler_);
  std::size_t done = 0U;
  if ((code == hal::kOk) && dma) {
    // A frame above the staging buffers is refused by the path before anything moves: no chip
    // select.
    const bool fits = total <= bus_.Limits().max_frame_dma;
    Deadline deadline{config_.timeout};
    if (fits) {
      count(&SpiStats::dma_frames);
      sdk::PinWrite(config_.cs, false);
    }
    code = bus_.DmaTransferLocked(tx, rx, deadline);
    if (fits) {
      sdk::PinWrite(config_.cs, true);
    }
  } else if (code == hal::kOk) {
    sdk::PinWrite(config_.cs, false);
    while ((code == hal::kOk) && (done < total)) {
      const std::size_t n = std::min(kChunk, total - done);
      const std::span<const uint8_t> t = tx.empty() ? tx : tx.subspan(done, n);
      const std::span<uint8_t> r = rx.empty() ? rx : rx.subspan(done, n);
      Deadline deadline{config_.timeout};
      code = bus_.TransferLocked(t, r, deadline);
      done += n;
    }
    sdk::PinWrite(config_.cs, true);
  } else {
    // The peripheral could not be set to the device's mode and clock: nothing was clocked.
  }
  return code;
}

kb::Status SpiDevice::finish(uint32_t code) noexcept {
  const uint32_t hal_status = code & 0xFFU;
  kb::Status status = MapHalStatus(hal_status);
  if (hal_status == hal::kError) {
    status = kb::Status::BusError; // mode fault, overrun, CRC or frame error (ErrorCode)
  }
  if (status != kb::Status::Ok) {
    count(&SpiStats::failures);
  }
  if ((status == kb::Status::Timeout) || (hal_status == port_code::kDma) ||
      (hal_status == port_code::kQuarantined)) {
    bus_.InvalidateLocked(); // aborted: every device's next frame initialises the peripheral again
    count(&SpiStats::rebuilds);
  }
  return error_.Set(status, code);
}

kb::Status SpiDevice::Transfer(std::span<const uint8_t> tx, std::span<uint8_t> rx) noexcept {
  kb::Status status = kb::Status::InvalidArgument;
  if (!kb::iface::SpiSpansValid(tx, rx) || (tx.empty() && rx.empty())) {
    static_cast<void>(error_.Set(status, port_code::kInvalidArgument));
  } else if (!ready_) {
    status = error_.Set(kb::Status::NotReady, port_code::kNotReady);
  } else if (!bus_.TryLockFor(config_.timeout)) {
    count(&SpiStats::lock_timeouts);
    status = error_.Set(kb::Status::Busy, port_code::kLockTimeout);
  } else {
    count(&SpiStats::transfers);
    std::fill(rx.begin(), rx.end(), uint8_t{0U}); // a read-only frame clocks these zeros out
    status = finish(frame(tx, rx));
    bus_.Unlock();
  }
  return status;
}

SpiStats SpiDevice::Stats() const noexcept {
  const IrqGuard guard{};
  return stats_;
}

void SpiDevice::count(uint32_t SpiStats::* field) noexcept {
  const IrqGuard guard{};
  ++(stats_.*field);
}

kb::Status SpiDevice::SetClockHz(uint32_t hz) noexcept {
  kb::Status status = kb::Status::InvalidArgument;
  bool ok = (hz > 0U);
  uint8_t exponent = prescaler_;
  if (ready_ && ok) {
    exponent = SpiPrescalerExponent(bus_.KernelClockHz(), limited(hz), ok);
  }
  if (!ok) {
    static_cast<void>(error_.Set(status, port_code::kInvalidArgument));
  } else {
    config_.clock_hz = hz; // the next frame selects the new prescaler
    prescaler_ = exponent;
    if (ready_ && bus_.HasDma()) {
      bool dma_ok = true;
      const uint8_t dma_exponent = SpiPrescalerExponent(bus_.KernelClockHz(), hz, dma_ok);
      dma_prescaler_ = dma_ok ? dma_exponent : exponent;
    } else {
      dma_prescaler_ = exponent;
    }
    if (ready_) {
      noteClock(hz);
    }
    status = error_.Clear();
  }
  return status;
}

} // namespace kb::port::stm32

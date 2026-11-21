// DmaStream: the assignment checks, the NVIC priority written and read back, the start, the
// interrupt that services every flag of its stream one clear bit at a time, the exactly-once report
// through the CompletionLatch, the stop bounded in core cycles that proves EN reads 0, and the
// recovery. Also the dispatch of the DMA vectors (HandleDmaIrq), the port's cache hooks and the
// start-time region check. Platform-free; every register access goes through sdk.hpp
// (src/boundary/dma.cpp).
// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The port-stm32 authors
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "kb/dma/assignment.hpp"
#include "kb/dma/buffer.hpp"
#include "kb/dma/capabilities.hpp"
#include "kb/dma/completion.hpp"
#include "kb/port/stm32/dma.hpp"
#include "kb/port/stm32/error.hpp"
#include "kb/port/stm32/irq.hpp"
#include "kb/utils/status.hpp"
#include "sdk.hpp"
#include "state.hpp"

namespace kb::port::stm32 {

namespace {
// Errors first: a transfer error ends the transfer before its completion flag is looked at.
constexpr std::array<uint32_t, 5> kServiceOrder{kDmaFlagTransferError, kDmaFlagDirectError,
                                                kDmaFlagFifoError, kDmaFlagHalf, kDmaFlagComplete};
constexpr uint32_t kFatal = kDmaFlagTransferError | kDmaFlagDirectError;
constexpr std::size_t kMaxItems = 65535U; // NDTR / CNDTR are 16 bits

[[nodiscard]] std::size_t Index(uint8_t engine, uint8_t stream) noexcept {
  return static_cast<std::size_t>(sdk::DmaIndex(engine, stream));
}
} // namespace

// --- cache hooks and the region check
// --------------------------------------------------------------

void PortCacheOps::Clean(std::span<const std::byte> range) noexcept {
  if (!range.empty()) {
    sdk::CacheClean(kb::dma::AddressOf(range.data()), range.size());
  }
}

void PortCacheOps::Invalidate(std::span<std::byte> range) noexcept {
  if (!range.empty()) {
    sdk::CacheInvalidate(kb::dma::AddressOf(range.data()), range.size());
  }
}

void PortCacheOps::Barrier() noexcept {
  sdk::DataBarrier();
}

uint32_t DmaRegionCheck(kb::dma::CachePolicy policy, std::uintptr_t base,
                        std::size_t size) noexcept {
  uint32_t code = hal::kOk;
  // A NonCacheable region on a core whose D-cache is on needs the MPU to make it uncached; with the
  // cache off (or no cache) every region is coherent. A Maintained region works either way.
  if ((policy == kb::dma::CachePolicy::NonCacheable) && sdk::DataCacheOn() &&
      !sdk::MpuUncached(base, size)) {
    code = port_code::kPlacement;
  }
  return code;
}

// --- the dispatch of the vectors
// -------------------------------------------------------------------

void HandleDmaIrq(uint8_t engine, uint8_t stream) noexcept {
  const std::size_t index = Index(engine, stream);
  if (index < kDmaStreams) {
    DmaStream* const owner = sdk::State().dma[index].load(std::memory_order_acquire);
    if (owner == nullptr) {
      sdk::DmaIrqEnable(engine, stream, false); // not the port's: no interrupt storm
    } else {
      owner->Dispatch();
    }
  }
}

// --- DmaStream
// -------------------------------------------------------------------------------------

DmaStream::~DmaStream() noexcept {
  static_cast<void>(Release());
}

kb::Status DmaStream::Bind(const kb::dma::StreamAssignment& assignment,
                           const DmaPeripheral& peripheral,
                           const DmaStreamLimits& limits) noexcept {
  kb::Status status = kb::Status::InvalidArgument;
  if (!ready_) {
    assignment_ = assignment;
    peripheral_ = peripheral;
    limits_ = limits;
    status = kb::Status::Ok;
  }
  return status;
}

void DmaStream::SetNotify(Completion* done, bool errors_only) noexcept {
  const IrqGuard guard{};
  notify_ = done;
  notify_errors_only_ = errors_only;
}

uint32_t DmaStream::checkAssignment() const noexcept {
  const kb::dma::StreamAssignment& a = assignment_;
  const bool engine_ok = (a.engine == kEngineDma1) || (a.engine == kEngineDma2);
  const bool tier_ok =
      (a.irq_priority <= kLowestIrqPriority) &&
      ((a.notify == kb::dma::Notify::None) || RtosIrqPriorityValid(a.irq_priority));
  const bool ok = engine_ok && (Index(a.engine, a.stream) < kDmaStreams) &&
                  sdk::DmaRequestValid(a.request) && tier_ok && (peripheral_.address != 0U) &&
                  (limits_.max_items > 0U) && (limits_.max_items <= kMaxItems);
  return ok ? hal::kOk : port_code::kInvalidArgument;
}

kb::Status DmaStream::EnsureReady() noexcept {
  kb::Status status = kb::Status::Ok;
  if (!ready_) {
    uint32_t code = Assigned() ? checkAssignment() : port_code::kUnsupported;
    const uint8_t e = assignment_.engine;
    const uint8_t s = assignment_.stream;
    std::size_t index = kDmaStreams;
    if (code == hal::kOk) {
      index = Index(e, s);
      const DmaStream* const owner = sdk::State().dma[index].load(std::memory_order_acquire);
      // A stream someone else started (EN set) or registered is not taken over.
      code = ((owner != nullptr) || sdk::DmaEnabled(e, s))
                 ? port_code::kOwned
                 : sdk::DmaConfigure({.assignment = assignment_, .peripheral = peripheral_});
    }
    if (code == hal::kOk) {
      clearAll();
      // The port writes the priority from the board's assignment and proves it (a regenerated
      // vendor initialisation once moved stream priorities to the RTOS floor).
      sdk::DmaIrqPriority(e, s, assignment_.irq_priority);
      if (sdk::DmaIrqPriorityRead(e, s) != assignment_.irq_priority) {
        code = port_code::kPriority;
      }
    }
    if (code == hal::kOk) {
      sdk::State().dma[index].store(this, std::memory_order_release);
      sdk::DmaIrqEnable(e, s, true);
    }
    status = error_.Set(MapHalStatus(code & 0xFFU), code);
    ready_ = status == kb::Status::Ok;
  }
  return status;
}

kb::Status DmaStream::Release() noexcept {
  kb::Status status = kb::Status::Ok;
  if (ready_) {
    const uint8_t e = assignment_.engine;
    const uint8_t s = assignment_.stream;
    sdk::DmaIrqEnable(e, s, false);
    const kb::dma::StopEvidence evidence = stop();
    clearAll();
    sdk::State().dma[Index(e, s)].store(nullptr, std::memory_order_release);
    ready_ = false;
    status = (evidence == kb::dma::StopEvidence::Confirmed)
                 ? error_.Clear()
                 : error_.Set(kb::Status::Fault, port_code::kQuarantined);
  }
  return status;
}

std::size_t DmaStream::MaxBytes() const noexcept {
  return Assigned() ? (limits_.max_items * DmaItemBytes(peripheral_.width))
                    : limits_.fallback_max_bytes;
}

uint32_t DmaStream::irqMask() const noexcept {
  uint32_t mask = kDmaFlagTransferError | kDmaFlagDirectError; // errors always interrupt
  if (assignment_.irq == kb::dma::IrqUse::All) {
    mask |= kDmaFlagComplete;
    if (assignment_.mode == kb::dma::ChannelMode::Circular) {
      mask |= kDmaFlagHalf;
    }
  }
  return mask;
}

void DmaStream::clearAll() const noexcept {
  const uint32_t flags = sdk::DmaFlags(assignment_.engine, assignment_.stream);
  for (const uint32_t flag : kServiceOrder) {
    if ((flags & flag) != 0U) {
      sdk::DmaClearFlag(assignment_.engine, assignment_.stream, flag);
    }
  }
}

kb::Status DmaStream::Begin(std::span<std::byte> memory, std::size_t bytes,
                            kb::utils::Deadline deadline) noexcept {
  const std::size_t item = DmaItemBytes(peripheral_.width);
  uint32_t code = hal::kOk;
  if (!Assigned()) {
    code = port_code::kUnsupported;
  } else if (!ready_) {
    code = port_code::kNotReady;
  } else if (faulted_) {
    code = port_code::kQuarantined;
  } else if ((bytes == 0U) || (bytes > memory.size()) || ((bytes % item) != 0U)) {
    code = port_code::kInvalidArgument;
  } else if (bytes > MaxBytes()) {
    code = port_code::kTooLarge;
  } else if (!latch_.Arm()) {
    code = hal::kBusy;
  } else {
    deadline_ = deadline;
    bytes_ = bytes;
    result_ = DmaPoll{};
    end_ = DmaEnd::None;
    pending_end_ = DmaEnd::None;
    clearAll(); // a flag of the last pass must not end this one
    {
      const IrqGuard guard{};
      ++stats_.starts;
    }
    sdk::DmaArm(assignment_.engine, assignment_.stream, kb::dma::AddressOf(memory.data()),
                static_cast<uint32_t>(bytes / item), irqMask());
  }
  return error_.Set(MapHalStatus(code & 0xFFU), code);
}

kb::dma::StopEvidence DmaStream::stop() noexcept {
  const uint8_t e = assignment_.engine;
  const uint8_t s = assignment_.stream;
  sdk::DmaDisable(e, s);
  const uint32_t bound = sdk::DmaStopBoundCycles();
  const uint32_t start = sdk::Cycles();
  uint32_t elapsed = 0U;
  bool enabled = sdk::DmaEnabled(e, s);
  // The stream may finish the data item (or burst) in flight after EN is cleared: wait for EN to
  // read 0, at most `bound` cycles and at most `bound` reads (a stopped cycle counter cannot hang).
  for (uint32_t i = 0U; enabled && (i < bound) && (elapsed < bound); ++i) {
    enabled = sdk::DmaEnabled(e, s);
    elapsed = sdk::Cycles() - start;
  }
  kb::dma::StopEvidence evidence = kb::dma::StopEvidence::NotConfirmed;
  if (!enabled) {
    evidence = kb::dma::StopEvidence::Confirmed;
    const IrqGuard guard{};
    stats_.max_stop_cycles = (elapsed > stats_.max_stop_cycles) ? elapsed : stats_.max_stop_cycles;
  }
  return evidence;
}

kb::dma::StopEvidence DmaStream::Stop() noexcept {
  kb::dma::StopEvidence evidence = kb::dma::StopEvidence::Confirmed;
  if (ready_) {
    evidence = stop();
    clearAll();
    if (evidence != kb::dma::StopEvidence::Confirmed) {
      faulted_ = true;
      const IrqGuard guard{};
      ++stats_.quarantines;
    }
  }
  return evidence;
}

uint32_t DmaStream::Remaining() const noexcept {
  return Assigned() ? sdk::DmaRemaining(assignment_.engine, assignment_.stream) : 0U;
}

void DmaStream::SetIrqHook(DmaIrqHook* hook) noexcept {
  const IrqGuard guard{};
  irq_hook_ = hook;
}

void DmaStream::Dispatch() noexcept {
  DmaIrqHook* const hook = irq_hook_;
  if ((hook != nullptr) && (hook->serve != nullptr)) {
    hook->serve(*hook);
  } else {
    OnIrq();
  }
}

void DmaStream::record(uint32_t flags) noexcept {
  stats_.last_flags = flags;
  stats_.transfer_errors += ((flags & kDmaFlagTransferError) != 0U) ? 1U : 0U;
  stats_.direct_errors += ((flags & kDmaFlagDirectError) != 0U) ? 1U : 0U;
  stats_.fifo_errors += ((flags & kDmaFlagFifoError) != 0U) ? 1U : 0U;
  stats_.halves += ((flags & kDmaFlagHalf) != 0U) ? 1U : 0U;
}

uint32_t DmaStream::serviceFlags() noexcept {
  const uint8_t e = assignment_.engine;
  const uint8_t s = assignment_.stream;
  const uint32_t flags = sdk::DmaFlags(e, s);
  uint32_t serviced = 0U;
  // Every flag of the stream, whether or not its interrupt is enabled and whether or not a transfer
  // is in flight, each cleared by its own bit: nothing is left to re-assert the interrupt.
  for (const uint32_t flag : kServiceOrder) {
    if ((flags & flag) != 0U) {
      sdk::DmaClearFlag(e, s, flag);
      serviced |= flag;
    }
  }
  serviced_ = serviced;
  if (serviced != 0U) {
    record(serviced);
  }
  return serviced;
}

void DmaStream::endTransfer(uint32_t serviced) noexcept {
  const bool fatal = (serviced & kFatal) != 0U;
  // A transfer error disables the stream itself; a direct-mode error does not: stop it either way,
  // and give the memory back only once EN reads 0.
  const kb::dma::StopEvidence evidence = fatal ? stop() : kb::dma::StopEvidence::Confirmed;
  const bool confirmed = evidence == kb::dma::StopEvidence::Confirmed;
  const std::size_t item = DmaItemBytes(peripheral_.width);
  const std::size_t left =
      static_cast<std::size_t>(sdk::DmaRemaining(assignment_.engine, assignment_.stream)) * item;
  result_.state = DmaPollState::Done;
  result_.count = (left < bytes_) ? (bytes_ - left) : 0U;
  result_.status = fatal ? kb::Status::Fault : kb::Status::Ok;
  result_.detail = fatal ? VendorCode(port_code::kDma, serviced) : 0U;
  if (!confirmed) {
    pending_end_ = DmaEnd::Unconfirmed;
  } else {
    pending_end_ = fatal ? DmaEnd::Failed : DmaEnd::Completed;
  }
  if (latch_.TryComplete()) {
    stats_.completions += fatal ? 0U : 1U;
    faulted_ = faulted_ || !confirmed;
    stats_.quarantines += confirmed ? 0U : 1U;
    Completion* const done = notify_;
    if ((done != nullptr) && (fatal || !notify_errors_only_)) {
      done->Complete(fatal ? result_.detail : hal::kOk);
    }
  }
}

void DmaStream::OnIrq() noexcept {
  const uint32_t started = sdk::Cycles();
  const uint32_t serviced = serviceFlags();
  const bool ends = (serviced & (kFatal | kDmaFlagComplete)) != 0U;
  const bool normal = assignment_.mode == kb::dma::ChannelMode::Normal;
  const bool pending = latch_.Pending();
  if (pending && normal && ends) {
    endTransfer(serviced);
  } else if ((serviced != 0U) && !pending) {
    ++stats_.unexpected; // nothing in flight: cleared and counted, the interrupt does not re-enter
  } else {
    // A half of a circular pass, or a flag of a pass in flight that does not end it.
  }
  const uint32_t cost = sdk::Cycles() - started;
  stats_.max_isr_cycles = (cost > stats_.max_isr_cycles) ? cost : stats_.max_isr_cycles;
}

kb::Status DmaStream::stopClaimed(bool timeout) noexcept {
  const kb::dma::StopEvidence evidence = stop();
  clearAll();
  const std::size_t item = DmaItemBytes(peripheral_.width);
  const std::size_t left =
      static_cast<std::size_t>(sdk::DmaRemaining(assignment_.engine, assignment_.stream)) * item;
  result_.count = (left < bytes_) ? (bytes_ - left) : 0U;
  kb::Status status = kb::Status::Ok;
  const IrqGuard guard{};
  if (evidence == kb::dma::StopEvidence::Confirmed) {
    end_ = DmaEnd::Aborted;
  } else {
    end_ = DmaEnd::Unconfirmed;
    faulted_ = true;
    ++stats_.quarantines;
    status = kb::Status::Fault;
  }
  if (timeout) {
    ++stats_.timeouts;
  } else {
    ++stats_.cancels;
  }
  return status;
}

DmaPoll DmaStream::Poll(kb::Instant now) noexcept {
  DmaPoll out{};
  if (latch_.TakeReport()) {
    out = result_;
    end_ = pending_end_;
    if ((out.status == kb::Status::Ok) && deadline_.Expired(now)) {
      out.status = kb::Status::Timeout; // a completion after the deadline is a timeout too
      const IrqGuard guard{};
      ++stats_.timeouts;
    }
  } else if (latch_.Pending()) {
    out.state = DmaPollState::Pending;
    if (deadline_.Expired(now)) {
      const kb::dma::CancelClaim claim = latch_.TryCancel();
      if (claim == kb::dma::CancelClaim::CancelledPending) {
        const kb::Status stopped = stopClaimed(true);
        out.state = DmaPollState::Done;
        out.status = (stopped == kb::Status::Ok) ? kb::Status::Timeout : kb::Status::Fault;
        out.count = result_.count;
        out.detail = (stopped == kb::Status::Ok) ? 0U : port_code::kQuarantined;
        latch_.FinishCancel();
      } else if (claim == kb::dma::CancelClaim::DroppedCompletion) {
        // It completed between the two reads: the memory is settled by the completion.
        end_ = pending_end_;
        out = result_;
        out.status = kb::Status::Timeout;
        latch_.FinishCancel();
        const IrqGuard guard{};
        ++stats_.timeouts;
      } else {
        out.state = DmaPollState::Idle;
      }
    }
  } else {
    out.state = DmaPollState::Idle;
  }
  return out;
}

kb::Status DmaStream::Cancel() noexcept {
  kb::Status status = kb::Status::Ok;
  const kb::dma::CancelClaim claim = latch_.TryCancel();
  if (claim == kb::dma::CancelClaim::CancelledPending) {
    status = stopClaimed(false);
  } else if (claim == kb::dma::CancelClaim::DroppedCompletion) {
    end_ = pending_end_; // the hardware finished: its report is dropped, its memory settled
    status = (pending_end_ == DmaEnd::Unconfirmed) ? kb::Status::Fault : kb::Status::Ok;
    const IrqGuard guard{};
    ++stats_.cancels;
  } else {
    status = kb::Status::Ok; // nothing in flight
  }
  if (claim != kb::dma::CancelClaim::Nothing) {
    latch_.FinishCancel();
  }
  return status;
}

kb::Status DmaStream::Conclude() noexcept {
  kb::Status status = kb::Status::Ok;
  const uint8_t e = assignment_.engine;
  const uint8_t s = assignment_.stream;
  if (latch_.Pending()) {
    const bool done = !sdk::DmaEnabled(e, s) && (sdk::DmaRemaining(e, s) == 0U) &&
                      ((sdk::DmaFlags(e, s) & kFatal) == 0U);
    if (done) {
      result_.state = DmaPollState::Done;
      result_.status = kb::Status::Ok;
      result_.count = bytes_;
      result_.detail = 0U;
      pending_end_ = DmaEnd::Completed;
    }
    if (done && latch_.TryComplete() && latch_.TakeReport()) {
      clearAll();
      end_ = DmaEnd::Completed;
      const IrqGuard guard{};
      ++stats_.completions;
    } else {
      const kb::Status stopped = Cancel();
      status = (stopped == kb::Status::Ok) ? kb::Status::BusError : stopped;
    }
  } else if (latch_.TakeReport()) {
    end_ = pending_end_; // an error interrupt ended it
    status = result_.status;
  } else {
    status = kb::Status::Ok;
  }
  return status;
}

DmaEnd DmaStream::TakeEnd() noexcept {
  const DmaEnd end = end_;
  end_ = DmaEnd::None;
  return end;
}

kb::Status DmaStream::Recover() noexcept {
  uint32_t code = Assigned() ? hal::kOk : port_code::kUnsupported;
  if (code == hal::kOk) {
    const uint8_t e = assignment_.engine;
    const uint8_t s = assignment_.stream;
    // A re-initialisation that is proven: the control register cleared, then EN must read 0.
    sdk::DmaReset(e, s);
    const kb::dma::StopEvidence evidence = stop();
    if (evidence != kb::dma::StopEvidence::Confirmed) {
      code = port_code::kQuarantined;
    } else {
      static_cast<void>(latch_.TryCancel());
      latch_.FinishCancel();
      code = ready_ ? sdk::DmaConfigure({.assignment = assignment_, .peripheral = peripheral_})
                    : hal::kOk;
    }
    if (code == hal::kOk) {
      end_ = DmaEnd::None;
      pending_end_ = DmaEnd::None;
      faulted_ = false;
      const IrqGuard guard{};
      ++stats_.recoveries;
    }
  }
  return error_.Set(MapHalStatus(code & 0xFFU), code);
}

kb::dma::Capabilities DmaStream::Capabilities() const noexcept {
  kb::dma::Capabilities c{};
  c.mode = Assigned() ? kb::dma::TransferMode::Dma : kb::dma::TransferMode::Interrupt;
  c.zero_copy = false;
  c.confirmed_abort = Assigned();
  c.max_frame_bytes = MaxBytes();
  c.staging_bytes = 0U;
  c.align_bytes = 1U;
  return c;
}

kb::dma::DmaHealth DmaStream::Health() const noexcept {
  const DmaStats s = Stats();
  kb::dma::DmaHealth h{};
  h.mode = Capabilities().mode;
  h.faulted = faulted_;
  h.quarantined = s.quarantines;
  h.errors = s.transfer_errors + s.direct_errors;
  return h;
}

DmaStats DmaStream::Stats() const noexcept {
  const IrqGuard guard{};
  return stats_;
}

} // namespace kb::port::stm32

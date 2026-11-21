// DmaStream over the host model of the DMA boundary: the assignment checks, the NVIC priority
// written from the assignment and read back (B32), a flag raised with nothing in flight cleared one
// clear bit at a time, counted, and not re-entered (B31), the start refusals, completion, errors,
// the confirmed stop and the quarantine, the recovery, the dispatch of the vectors, the cache hooks
// and the start-time region check. Test code.
// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The port-stm32 authors
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "dma_model.hpp"
#include "kb/dma/host/engine.hpp"
#include "kb/dma/host/machine.hpp"
#include "port_rig.hpp"
#include "sdk.hpp"
#include "support.hpp"

namespace h = kb::dma::host;
namespace d = kb::dma;
using port_rig::HostAxi;

namespace {

constexpr d::StreamAssignment kRx{1U,
                                  0U,
                                  37U,
                                  d::ChannelPriority::High,
                                  6U,
                                  d::Notify::Task,
                                  d::IrqUse::All,
                                  d::ChannelMode::Normal};
constexpr stm::DmaPeripheral kRxEnd{0x40013030U, stm::DmaWidth::Byte, d::Direction::FromDevice};

struct Bench {
  h::Machine machine{32U, false};
  h::DmaEngine engine{machine};
  alignas(32) std::array<std::byte, 128> mem{};
  Bench() {
    fake::Reset();
    dma_model::Reset();
    dma_model::BindMachine(&machine);
    dma_model::BindEngine(1U, 0U, &engine, 0x40U);
    static_cast<void>(machine.RegisterRaw(mem, false));
    static_cast<void>(machine.OpenWindow(mem.data(), mem.size()));
  }
  Bench(const Bench&) = delete;
  Bench& operator=(const Bench&) = delete;
  ~Bench() {
    dma_model::Reset();
  }
  [[nodiscard]] static kb::utils::Deadline Far() {
    return kb::utils::Deadline::After(kb::Instant{0U}, kb::Milliseconds{100000U});
  }
};

void Ticks(int n) {
  for (int i = 0; i < n; ++i) {
    dma_model::Tick();
  }
}

struct S1 {
  static constexpr d::StreamAssignment kAssignment = kRx;
};

struct Counter : stm::DmaIrqHook {
  uint32_t calls{0U};
};
void CountCall(stm::DmaIrqHook& hook) noexcept {
  ++static_cast<Counter&>(hook).calls;
}

} // namespace

KB_TEST("dma regions and checks hold at compile time") {
  static_assert(stm::PlacedRegion<stm::h7::D2Sram>);
  static_assert(stm::PlacedRegion<stm::h7::AxiSram>);
  static_assert(stm::PlacedRegion<stm::h7::Sram4>);
  static_assert(stm::PlacedRegion<stm::h7::Dtcm>);
  static_assert(stm::PlacedRegion<stm::Sram>);
  static_assert(stm::h7::D2Sram::kPolicy == d::CachePolicy::NonCacheable);
  static_assert(stm::h7::AxiSram::kPolicy == d::CachePolicy::Maintained);
  static_assert(d::kRegionReachableBy<stm::kMasterDma1, stm::h7::D2Sram>);
  static_assert(!d::kRegionReachableBy<stm::kMasterDma1, stm::h7::Dtcm>);
  static_assert(!d::kRegionReachableBy<stm::kMasterBdma, stm::h7::AxiSram>);
  constexpr bool kOk =
      (stm::RequireStream<S1>(), stm::RequireStreamReaches<S1, stm::h7::D2Sram>(), true);
  static_assert(kOk);
  KB_CHECK_EQ(stm::MasterOfEngine(stm::kEngineDma2), stm::kMasterDma2);
  KB_CHECK_EQ(stm::MasterOfEngine(7U), 0U);
  KB_CHECK_EQ(stm::DmaItemBytes(stm::DmaWidth::Word), 4U);
  KB_CHECK_EQ(stm::DmaItemBytes(stm::DmaWidth::HalfWord), 2U);
  KB_CHECK_EQ(stm::DmaItemBytes(stm::DmaWidth::Byte), 1U);
}

KB_TEST("dma: assignments the family does not have are refused") {
  Bench b;
  stm::DmaStream none{};
  KB_CHECK_EQ(none.EnsureReady(), Status::Unsupported);
  KB_CHECK(!none.Assigned());
  KB_CHECK_EQ(none.Capabilities().mode, d::TransferMode::Interrupt);
  KB_CHECK_EQ(none.Recover(), Status::Unsupported);
  KB_CHECK_EQ(none.Remaining(), 0U);
  std::array<std::byte, 4> m{};
  KB_CHECK_EQ(none.Begin(m, 4U, Bench::Far()), Status::Unsupported);
  stm::DmaStream engine3{d::StreamAssignment{3U, 0U, 1U}, kRxEnd};
  KB_CHECK_EQ(engine3.EnsureReady(), Status::InvalidArgument);
  stm::DmaStream stream9{d::StreamAssignment{1U, 9U, 1U}, kRxEnd};
  KB_CHECK_EQ(stream9.EnsureReady(), Status::InvalidArgument);
  stm::DmaStream request{d::StreamAssignment{1U, 0U, 200U}, kRxEnd};
  KB_CHECK_EQ(request.EnsureReady(), Status::InvalidArgument);
  d::StreamAssignment prio = kRx;
  prio.irq_priority = 16U;
  stm::DmaStream priority{prio, kRxEnd};
  KB_CHECK_EQ(priority.EnsureReady(), Status::InvalidArgument);
  stm::DmaStream address{kRx, stm::DmaPeripheral{}};
  KB_CHECK_EQ(address.EnsureReady(), Status::InvalidArgument);
  stm::DmaStream items{kRx, kRxEnd, stm::DmaStreamLimits{70000U, 0U}};
  KB_CHECK_EQ(items.EnsureReady(), Status::InvalidArgument);
}

KB_TEST("dma: the port writes the NVIC priority from the assignment and refuses a read-back that "
        "differs (B32)") {
  Bench b;
  {
    stm::DmaStream s{kRx, kRxEnd};
    KB_REQUIRE(s.EnsureReady() == Status::Ok);
    KB_CHECK_EQ(s.EnsureReady(), Status::Ok); // idempotent
    KB_CHECK_EQ(dma_model::PriorityWritten(1U, 0U), 6U);
    KB_CHECK(dma_model::IrqEnabled(1U, 0U));
    KB_CHECK_EQ(dma_model::Request(1U, 0U), 37U);
    KB_CHECK_EQ(s.Release(), Status::Ok);
    KB_CHECK(!dma_model::IrqEnabled(1U, 0U));
    KB_CHECK_EQ(s.Release(), Status::Ok);
  }
  dma_model::ForcePriorityReadback(5); // a vendor initialisation moved it
  stm::DmaStream s{kRx, kRxEnd};
  KB_CHECK_EQ(s.EnsureReady(), Status::NotReady);
  KB_CHECK_EQ(s.LastError().Detail(), stm::port_code::kPriority);
  KB_CHECK(!s.IsReady());
  KB_CHECK(!dma_model::IrqEnabled(1U, 0U));
}

KB_TEST("dma: a stream someone else enabled or registered is not taken over") {
  Bench b;
  dma_model::SetForeignEnabled(1U, 0U, true);
  stm::DmaStream s{kRx, kRxEnd};
  KB_CHECK_EQ(s.EnsureReady(), Status::Busy);
  dma_model::SetForeignEnabled(1U, 0U, false);
  KB_REQUIRE(s.EnsureReady() == Status::Ok);
  stm::DmaStream twin{kRx, kRxEnd};
  KB_CHECK_EQ(twin.EnsureReady(), Status::Busy);
  KB_CHECK_EQ(twin.Bind(kRx, kRxEnd), Status::Ok);
  KB_CHECK_EQ(s.Bind(kRx, kRxEnd), Status::InvalidArgument); // ready: the binding is fixed
}

KB_TEST("dma: a flag raised with nothing in flight is cleared by its own bit, counted, and the "
        "interrupt does not re-enter (B31)") {
  Bench b;
  stm::DmaStream s{kRx, kRxEnd};
  KB_REQUIRE(s.EnsureReady() == Status::Ok);
  dma_model::RaiseFlags(
      1U, 0U, stm::kDmaFlagTransferError | stm::kDmaFlagFifoError | stm::kDmaFlagDirectError);
  KB_CHECK_EQ(dma_model::Nvic(), 1U);
  KB_CHECK_EQ(dma_model::Nvic(), 0U); // nothing left set: no second entry
  KB_CHECK_EQ(dma_model::Clears(1U, 0U, stm::kDmaFlagTransferError), 1U);
  KB_CHECK_EQ(dma_model::Clears(1U, 0U, stm::kDmaFlagFifoError), 1U);
  KB_CHECK_EQ(dma_model::Clears(1U, 0U, stm::kDmaFlagDirectError), 1U);
  KB_CHECK_EQ(dma_model::Clears(1U, 0U, stm::kDmaFlagComplete), 0U);
  const stm::DmaStats st = s.Stats();
  KB_CHECK_EQ(st.unexpected, 1U);
  KB_CHECK_EQ(st.transfer_errors, 1U);
  KB_CHECK_EQ(st.fifo_errors, 1U);
  KB_CHECK_EQ(st.direct_errors, 1U);
  KB_CHECK(st.max_isr_cycles > 0U);
  KB_CHECK_EQ(s.Poll(kb::Instant{0U}).state, stm::DmaPollState::Idle);
}

KB_TEST("dma: a transfer completes once, its moved count and end are reported") {
  Bench b;
  stm::DmaStream s{kRx, kRxEnd};
  std::array<std::byte, 16> m{};
  KB_CHECK_EQ(s.Begin(m, 8U, Bench::Far()), Status::NotReady);
  KB_REQUIRE(s.EnsureReady() == Status::Ok);
  KB_CHECK_EQ(s.Begin(b.mem, 0U, Bench::Far()), Status::InvalidArgument);
  KB_CHECK_EQ(s.Begin(std::span<std::byte>(b.mem).first(4U), 8U, Bench::Far()),
              Status::InvalidArgument);
  KB_CHECK_EQ(s.Begin(b.mem, 8U, Bench::Far()), Status::Ok);
  KB_CHECK_EQ(s.Begin(b.mem, 8U, Bench::Far()), Status::Busy);
  KB_CHECK_EQ(s.Poll(kb::Instant{0U}).state, stm::DmaPollState::Pending);
  KB_CHECK_EQ(dma_model::ArmedIrqs(1U, 0U),
              stm::kDmaFlagComplete | stm::kDmaFlagTransferError | stm::kDmaFlagDirectError);
  Ticks(20);
  const stm::DmaPoll p = s.Poll(kb::Instant{0U});
  KB_CHECK_EQ(p.state, stm::DmaPollState::Done);
  KB_CHECK_EQ(p.status, Status::Ok);
  KB_CHECK_EQ(p.count, 8U);
  KB_CHECK_EQ(s.Poll(kb::Instant{0U}).state, stm::DmaPollState::Idle); // once
  KB_CHECK_EQ(s.TakeEnd(), stm::DmaEnd::Completed);
  KB_CHECK_EQ(s.TakeEnd(), stm::DmaEnd::None);
  KB_CHECK_EQ(b.mem[3], std::byte{0x43U});
  KB_CHECK_EQ(s.Stats().completions, 1U);
  KB_CHECK_EQ(s.Stats().starts, 1U);
  KB_CHECK_EQ(s.Release(), Status::Ok);
  stm::DmaStream small{kRx, kRxEnd, stm::DmaStreamLimits{16U, 0U}};
  KB_REQUIRE(small.EnsureReady() == Status::Ok);
  KB_CHECK_EQ(small.Begin(b.mem, 32U, Bench::Far()), Status::OutOfRange);
  KB_CHECK_EQ(small.LastError().Detail(), stm::port_code::kTooLarge);
  KB_CHECK_EQ(small.MaxBytes(), 16U);
}

KB_TEST("dma: a transfer error ends the transfer as Fault with the stream flags; a direct-mode "
        "error stops it") {
  Bench b;
  stm::DmaStream s{kRx, kRxEnd};
  KB_REQUIRE(s.EnsureReady() == Status::Ok);
  KB_REQUIRE(s.Begin(b.mem, 32U, Bench::Far()) == Status::Ok);
  b.engine.InjectError(4U, h::kFlagTransferError);
  Ticks(20);
  const stm::DmaPoll p = s.Poll(kb::Instant{0U});
  KB_CHECK_EQ(p.status, Status::Fault);
  KB_CHECK_EQ(p.detail, stm::VendorCode(stm::port_code::kDma, stm::kDmaFlagTransferError));
  KB_CHECK_EQ(p.count, 4U);
  KB_CHECK_EQ(s.TakeEnd(), stm::DmaEnd::Failed);
  KB_CHECK_EQ(s.Health().errors, 1U);
  // A direct-mode error leaves EN set: the interrupt stops the stream before it reports.
  KB_REQUIRE(s.Begin(b.mem, 32U, Bench::Far()) == Status::Ok);
  dma_model::Tick();
  dma_model::RaiseFlags(1U, 0U, stm::kDmaFlagDirectError);
  static_cast<void>(dma_model::Nvic());
  KB_CHECK(!b.engine.Enabled());
  KB_CHECK_EQ(s.Poll(kb::Instant{0U}).status, Status::Fault);
  KB_CHECK_EQ(s.TakeEnd(), stm::DmaEnd::Failed);
}

KB_TEST("dma: an overdue transfer is stopped with proof; a late completion is a timeout") {
  Bench b;
  stm::DmaStream s{kRx, kRxEnd};
  KB_REQUIRE(s.EnsureReady() == Status::Ok);
  const auto soon = kb::utils::Deadline::After(kb::Instant{0U}, kb::Milliseconds{1U});
  KB_REQUIRE(s.Begin(b.mem, 64U, soon) == Status::Ok);
  dma_model::Tick();
  const stm::DmaPoll p = s.Poll(kb::Instant{5000U});
  KB_CHECK_EQ(p.state, stm::DmaPollState::Done);
  KB_CHECK_EQ(p.status, Status::Timeout);
  KB_CHECK(!b.engine.Enabled());
  KB_CHECK_EQ(s.TakeEnd(), stm::DmaEnd::Aborted);
  KB_CHECK_EQ(s.Stats().timeouts, 1U);
  // A completion that arrives after its deadline is reported as a timeout too.
  KB_REQUIRE(s.Begin(b.mem, 4U, soon) == Status::Ok);
  Ticks(10);
  KB_CHECK_EQ(s.Poll(kb::Instant{5000U}).status, Status::Timeout);
  KB_CHECK_EQ(s.TakeEnd(), stm::DmaEnd::Completed);
}

KB_TEST("dma: a stop that never reads EN 0 quarantines; Recover proves the stop") {
  Bench b;
  h::EngineConfig stuck{};
  stuck.stuck = true;
  b.engine.Configure(stuck);
  stm::DmaStream s{kRx, kRxEnd};
  KB_REQUIRE(s.EnsureReady() == Status::Ok);
  KB_REQUIRE(s.Begin(b.mem, 100U, Bench::Far()) == Status::Ok);
  dma_model::Tick();
  KB_CHECK_EQ(s.Cancel(), Status::Fault);
  KB_CHECK(s.Faulted());
  KB_CHECK_EQ(s.TakeEnd(), stm::DmaEnd::Unconfirmed);
  KB_CHECK(s.Health().faulted);
  KB_CHECK_EQ(s.Health().quarantined, 1U);
  KB_CHECK_EQ(s.Begin(b.mem, 8U, Bench::Far()), Status::Fault);
  KB_CHECK_EQ(s.Stop(), d::StopEvidence::NotConfirmed);
  KB_CHECK_EQ(s.Recover(), Status::Ok);
  KB_CHECK(!s.Faulted());
  KB_CHECK_EQ(s.Stats().recoveries, 1U);
  KB_CHECK_EQ(s.Begin(b.mem, 8U, Bench::Far()), Status::Ok);
  KB_CHECK_EQ(s.Cancel(), Status::Ok);
  KB_CHECK_EQ(s.Cancel(), Status::Ok); // nothing in flight
  KB_CHECK_EQ(s.Stats().cancels, 2U);  // the unconfirmed one and the confirmed one
}

KB_TEST("dma: a stream with its completion interrupt off is concluded from the hardware") {
  Bench b;
  d::StreamAssignment quiet = kRx;
  quiet.irq = d::IrqUse::ErrorsOnly;
  stm::DmaStream s{quiet, kRxEnd};
  KB_REQUIRE(s.EnsureReady() == Status::Ok);
  KB_REQUIRE(s.Begin(b.mem, 4U, Bench::Far()) == Status::Ok);
  KB_CHECK_EQ(dma_model::ArmedIrqs(1U, 0U), stm::kDmaFlagTransferError | stm::kDmaFlagDirectError);
  Ticks(10);
  KB_CHECK_EQ(s.Poll(kb::Instant{0U}).state, stm::DmaPollState::Pending); // no interrupt said so
  KB_CHECK_EQ(s.Conclude(), Status::Ok);
  KB_CHECK_EQ(s.TakeEnd(), stm::DmaEnd::Completed);
  KB_CHECK_EQ(s.Conclude(), Status::Ok); // nothing in flight
  // Not finished when its partner did: stopped, BusError.
  KB_REQUIRE(s.Begin(b.mem, 64U, Bench::Far()) == Status::Ok);
  dma_model::Tick();
  KB_CHECK_EQ(s.Conclude(), Status::BusError);
  KB_CHECK_EQ(s.TakeEnd(), stm::DmaEnd::Aborted);
  // An error interrupt ended it first: Conclude takes that report.
  KB_REQUIRE(s.Begin(b.mem, 64U, Bench::Far()) == Status::Ok);
  b.engine.InjectError(2U, h::kFlagTransferError);
  Ticks(5);
  KB_CHECK_EQ(s.Conclude(), Status::Fault);
  KB_CHECK_EQ(s.TakeEnd(), stm::DmaEnd::Failed);
}

KB_TEST("dma: an unowned stream's vector disables its line; a hook takes over the dispatch") {
  Bench b;
  stm::sdk::DmaIrqEnable(2U, 7U, true);
  KB_CHECK(dma_model::IrqEnabled(2U, 7U));
  stm::HandleDmaIrq(2U, 7U);
  KB_CHECK(!dma_model::IrqEnabled(2U, 7U));
  stm::HandleDmaIrq(9U, 0U); // no such stream: nothing
  Counter counter{};
  counter.serve = &CountCall;
  stm::DmaStream s{kRx, kRxEnd};
  KB_REQUIRE(s.EnsureReady() == Status::Ok);
  s.SetIrqHook(&counter);
  stm::HandleDmaIrq(1U, 0U);
  KB_CHECK_EQ(counter.calls, 1U);
  s.SetIrqHook(nullptr);
  stm::HandleDmaIrq(1U, 0U);
  KB_CHECK_EQ(counter.calls, 1U);
}

KB_TEST("dma: a circular stream counts its halves and never completes") {
  Bench b;
  d::StreamAssignment circ = kRx;
  circ.mode = d::ChannelMode::Circular;
  stm::DmaStream s{circ, kRxEnd};
  KB_REQUIRE(s.EnsureReady() == Status::Ok);
  KB_CHECK(dma_model::Circular(1U, 0U));
  KB_REQUIRE(s.Begin(b.mem, 8U, Bench::Far()) == Status::Ok);
  KB_CHECK_EQ(dma_model::ArmedIrqs(1U, 0U), stm::kDmaFlagsAll & ~stm::kDmaFlagFifoError);
  dma_model::RaiseFlags(1U, 0U, stm::kDmaFlagHalf | stm::kDmaFlagComplete);
  s.OnIrq();
  KB_CHECK_EQ(s.LastServiced(), stm::kDmaFlagHalf | stm::kDmaFlagComplete);
  KB_CHECK_EQ(s.Stats().halves, 1U);
  KB_CHECK_EQ(s.Poll(kb::Instant{0U}).state, stm::DmaPollState::Pending);
  KB_CHECK_EQ(s.Stop(), d::StopEvidence::Confirmed);
}

KB_TEST("dma: the region check refuses a NonCacheable region the MPU leaves cached; placement") {
  Bench b;
  KB_CHECK_EQ(stm::DmaRegionCheck(d::CachePolicy::NonCacheable, 0x30000000U, 0x8000U), 0U);
  dma_model::SetCache(true, false);
  KB_CHECK_EQ(stm::DmaRegionCheck(d::CachePolicy::NonCacheable, 0x30000000U, 0x8000U),
              stm::port_code::kPlacement);
  KB_CHECK_EQ(stm::DmaRegionCheck(d::CachePolicy::Maintained, 0x24000000U, 0x8000U), 0U);
  KB_CHECK_EQ(stm::DmaRegionCheck(d::CachePolicy::Coherent, 0x20000000U, 0x8000U), 0U);
  dma_model::SetCache(true, true);
  KB_CHECK_EQ(stm::DmaRegionCheck(d::CachePolicy::NonCacheable, 0x30000000U, 0x8000U), 0U);
  static d::Buffer<stm::h7::D2Sram, 64> outside{}; // a host address, not in the D2 window
  KB_CHECK_EQ(stm::DmaPlacementCheck(outside), stm::port_code::kPlacement);
  static d::Buffer<HostAxi, 64> inside{};
  KB_CHECK_EQ(stm::DmaPlacementCheck(inside), 0U);
}

KB_TEST("dma: the cache hooks maintain the range they are given, nothing for an empty one") {
  Bench b;
  const h::MaintenanceCounts before = b.machine.Maintenance();
  stm::PortCacheOps::Clean(std::span<const std::byte>(b.mem.data(), 32U));
  stm::PortCacheOps::Invalidate(std::span<std::byte>(b.mem.data(), 32U));
  stm::PortCacheOps::Clean({});
  stm::PortCacheOps::Invalidate({});
  stm::PortCacheOps::Barrier();
  const h::MaintenanceCounts after = b.machine.Maintenance();
  KB_CHECK_EQ(after.cleans - before.cleans, 1U);
  KB_CHECK_EQ(after.invalidates - before.invalidates, 1U);
  KB_CHECK_EQ(after.barriers - before.barriers, 1U);
}

KB_TEST("dma: a DmaTransfer refuses a direction its stream does not have and takes back a "
        "refused start") {
  Bench b;
  static d::Buffer<HostAxi, 64> buffer{};
  stm::DmaStream s{kRx, kRxEnd};
  stm::DmaTransfer<HostAxi> t{s};
  KB_CHECK_EQ(t.Start(buffer, d::Direction::ToDevice, 8U, Bench::Far()), Status::InvalidArgument);
  KB_CHECK_EQ(t.Start(buffer, d::Direction::FromDevice, 8U, Bench::Far()), Status::NotReady);
  KB_CHECK_EQ(buffer.Current(), d::BufferState::Cpu); // the lend was taken back
  KB_CHECK(t.Capabilities().zero_copy);
  KB_CHECK_EQ(t.Capabilities().align_bytes, 32U);
  KB_CHECK(&t.Stream() == &s);
  KB_CHECK_EQ(t.Recover(buffer), Status::Ok); // a buffer the CPU owns already stays the CPU's
}

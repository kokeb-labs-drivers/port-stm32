// AdcDmaStream over the host model: the circular stream armed before the ADC, the idempotent start,
// the start refusals (kernel clock 0, HAL time base stopped, a shared instance), the conversion
// count against the frame (B33: one-shot fallback, reported), the overrun counted, the drain as the
// only reader of the flags (an error flag with no frame: B31), the stall supervision with signed
// time and its restart, the fallback after failed restarts, the seqlock publication, the bounded
// pass. Test code. SPDX-License-Identifier: MIT Copyright (c) 2026 The port-stm32 authors
#include <array>
#include <cstddef>
#include <cstdint>

#include "dma_model.hpp"
#include "kb/dma/host/machine.hpp"
#include "kb/dma/host/stream_rig.hpp"
#include "port_rig.hpp"
#include "support.hpp"

namespace h = kb::dma::host;
namespace d = kb::dma;
using port_rig::HostAxi;
using port_rig::HostD2;
using port_rig::PortAdcRig;

namespace {

constexpr d::StreamConfig kSup{.frame_period = kb::Microseconds{1000U},
                               .stall_periods = 10U,
                               .bad_frames_to_restart = 8U,
                               .restart_backoff = kb::Milliseconds{1000U},
                               .restarts_before_fallback = 0U};

stm::AdcStreamConfig Cfg(const d::StreamConfig& sup = kSup) {
  stm::AdcStreamConfig c{};
  c.instance = 1U;
  c.channels = port_rig::kAdcChannels;
  c.trigger = 0x7U;
  c.dma = port_rig::kAdcStream;
  c.shape = d::StreamShape{2U, 2U, 4U};
  c.supervision = sup;
  c.now = &port_rig::AdcNow;
  return c;
}

struct Fallback {
  using Adc = stm::AdcDmaStream<h::Scan, HostD2>;
  Adc::Ring ring{};
  Adc adc;
  explicit Fallback(const stm::AdcStreamConfig& c) : adc(ring, c) {
    fake::Reset();
    dma_model::Reset();
  }
};

} // namespace

KB_TEST("adc stream: the circular stream is armed before the ADC; the start is idempotent") {
  PortAdcRig<HostD2> rig(kSup);
  KB_REQUIRE(rig.Start(kb::Instant{0U}) == Status::Ok);
  KB_CHECK(dma_model::Circular(1U, 4U));
  KB_CHECK_EQ(dma_model::ArmedIrqs(1U, 4U), stm::kDmaFlagComplete | stm::kDmaFlagHalf |
                                                stm::kDmaFlagTransferError |
                                                stm::kDmaFlagDirectError);
  KB_CHECK(dma_model::Find("arm:1:4") < dma_model::Find("adc-start:1"));
  KB_CHECK_EQ(dma_model::PriorityWritten(1U, 4U), 4U); // Notify::None: above the RTOS limit allowed
  KB_CHECK_EQ(rig.Start(kb::Instant{10U}), Status::Ok);
  KB_CHECK_EQ(dma_model::AdcStarts(), 1U); // a second start of a running stream changes nothing
  KB_CHECK_EQ(rig.Adc().Capabilities().mode, d::TransferMode::Dma);
  KB_CHECK_EQ(rig.Adc().Mode(), d::StreamMode::Dma);
  KB_CHECK_EQ(rig.Adc().FallbackReason(), stm::AdcFallback::None);
  KB_CHECK_EQ(rig.Adc().Core().ConversionsReadBack(), 4U);
  KB_CHECK(!rig.Adc().Health().faulted);
  KB_CHECK_EQ(rig.Adc().Stop(), Status::Ok);
  KB_CHECK_EQ(rig.Adc().Stop(), Status::Ok);
  KB_CHECK(dma_model::Find("adc-stop:1") < dma_model::Log().size());
}

KB_TEST("adc stream: frames are published from the interrupt and the latest through the seqlock") {
  PortAdcRig<HostAxi> rig(kSup); // the Maintained ring: the drain invalidates whole lines only
  KB_CHECK_EQ(rig.Adc().Latest().Error(), Status::Empty);
  KB_REQUIRE(rig.Start(kb::Instant{0U}) == Status::Ok);
  rig.RunHalves(2U, kb::Instant{1000U});
  KB_CHECK_EQ(rig.Adc().Stream().Stats().published, 2U);
  KB_CHECK(rig.Adc().Stream().Stats().max_isr_cycles > 0U);
  KB_CHECK_EQ(rig.Adc().Service(), d::SupervisorAction::None);
  const auto latest = rig.Adc().Latest();
  KB_REQUIRE(latest.IsOk());
  KB_CHECK_EQ(latest.Value().seq, 1U);
  KB_CHECK_EQ(latest.Value().scan.v[0], 4U);
  KB_CHECK_EQ(latest.Value().scan.v[3], 7U);
  KB_CHECK_EQ(latest.Value().at_us, 1000U);
  KB_CHECK_EQ(latest.Value().flags, 0U);
  KB_CHECK_EQ(rig.M().Count(h::Violation::UnalignedMaintenance), 0U);
  KB_CHECK_EQ(rig.M().Total(), 0U);
}

KB_TEST("adc stream: an owner pass takes at most frames_per_pass frames") {
  PortAdcRig<HostD2> rig(kSup, 1U);
  KB_REQUIRE(rig.Start(kb::Instant{0U}) == Status::Ok);
  rig.RunHalves(2U, kb::Instant{100U});
  static_cast<void>(rig.Adc().Service());
  KB_CHECK_EQ(rig.Adc().Stream().Stats().delivered, 1U);
  static_cast<void>(rig.Adc().Service());
  KB_CHECK_EQ(rig.Adc().Stream().Stats().delivered, 2U);
}

KB_TEST("adc stream: a conversion count that differs from the frame gives the one-shot fallback, "
        "reported (B33)") {
  Fallback f(Cfg());
  dma_model::ForceConversions(3); // a vendor initialisation left three ranks configured
  fake::AdcRaw(1234U);
  KB_REQUIRE(f.adc.Start() == Status::Ok);
  KB_CHECK_EQ(f.adc.FallbackReason(), stm::AdcFallback::Shape);
  KB_CHECK_EQ(f.adc.Mode(), d::StreamMode::Fallback);
  KB_CHECK_EQ(f.adc.Capabilities().mode, d::TransferMode::Interrupt);
  KB_CHECK_EQ(f.adc.Health().mode, d::TransferMode::Interrupt);
  KB_CHECK_EQ(f.adc.Core().ConversionsReadBack(), 3U);
  KB_CHECK_EQ(dma_model::Find("arm:1:4"), dma_model::Log().size()); // no DMA
  KB_CHECK(dma_model::Find("adc-close:1") < dma_model::Log().size());
  static_cast<void>(f.adc.Service());
  const auto latest = f.adc.Latest();
  KB_REQUIRE(latest.IsOk());
  KB_CHECK_EQ(latest.Value().scan.v[2], 1234U);
  KB_CHECK_EQ(latest.Value().flags & 4U, 4U); // the frame says it came from the fallback
  KB_CHECK_EQ(f.adc.Stream().Stats().fallbacks, 1U);
  KB_CHECK_EQ(f.adc.Restart(), Status::Ok); // nothing to restart in the fallback
  KB_CHECK_EQ(f.adc.Stop(), Status::Ok);
}

KB_TEST("adc stream: a board shape that differs from the frame, or no stream, is the fallback") {
  {
    stm::AdcStreamConfig c = Cfg();
    c.shape.elements = 5U;
    Fallback f(c);
    KB_REQUIRE(f.adc.Start() == Status::Ok);
    KB_CHECK_EQ(f.adc.FallbackReason(), stm::AdcFallback::Shape);
    KB_CHECK_EQ(dma_model::AdcOpens(), 0U);
  }
  {
    stm::AdcStreamConfig c = Cfg();
    c.dma = d::StreamAssignment{};
    Fallback f(c);
    KB_REQUIRE(f.adc.Start() == Status::Ok);
    KB_CHECK_EQ(f.adc.FallbackReason(), stm::AdcFallback::NoStream);
  }
}

KB_TEST("adc stream: the start refuses a stopped kernel clock, a stopped time base, a bad "
        "configuration and a shared instance") {
  {
    Fallback f(Cfg());
    fake::SetHclk(0U);
    KB_CHECK_EQ(f.adc.Start(), Status::NotReady);
    KB_CHECK_EQ(f.adc.Core().LastError().Detail(), stm::port_code::kNoClock);
    KB_CHECK_EQ(dma_model::AdcOpens(), 0U);
    fake::SetHclk(160000000U);
    KB_CHECK_EQ(f.adc.Start(), Status::Ok); // the instance was given back after the refusal
  }
  {
    Fallback f(Cfg());
    fake::SetTickStep(0U);
    KB_CHECK_EQ(f.adc.Start(), Status::NotReady);
    KB_CHECK_EQ(f.adc.Core().LastError().Detail(), stm::port_code::kHalTickStopped);
  }
  {
    stm::AdcStreamConfig c = Cfg();
    c.now = nullptr;
    Fallback f(c);
    KB_CHECK_EQ(f.adc.Start(), Status::InvalidArgument);
  }
  {
    Fallback f(Cfg());
    stm::AdcConfig one{};
    one.instance = 1U;
    one.channel = 1U;
    stm::AdcChannel ch{one};
    KB_REQUIRE(ch.EnsureReady() == Status::Ok);
    KB_CHECK_EQ(f.adc.Start(), Status::Busy); // the channel shares the instance
    static_cast<void>(ch.Release());
    KB_REQUIRE(f.adc.Start() == Status::Ok);
    stm::AdcChannel late{one};
    KB_CHECK_EQ(late.EnsureReady(), Status::Busy); // the stream has it alone
  }
  {
    Fallback f(Cfg());
    dma_model::FailAdcOpen(stm::port_code::kInvalidArgument);
    KB_CHECK_EQ(f.adc.Start(), Status::InvalidArgument);
  }
}

KB_TEST("adc stream: an ADC overrun is counted in the drain and the next frame shows the gap") {
  PortAdcRig<HostD2> rig(kSup);
  KB_REQUIRE(rig.Start(kb::Instant{0U}) == Status::Ok);
  rig.RaiseOverrun();
  rig.RunHalves(1U, kb::Instant{1000U});
  const d::StreamStats s = rig.Adc().Stream().Stats();
  KB_CHECK_EQ(s.hw_overruns, 1U);
  KB_CHECK_EQ(s.published, 1U);
  KB_CHECK((s.last_hw_flags & stm::kAdcFlagOverrun) != 0U);
  static_cast<void>(rig.Adc().Service());
  KB_CHECK_EQ(rig.Adc().Latest().Value().flags & 1U, 1U); // gap
}

KB_TEST("adc stream: an error flag of the ring's stream is serviced and counted, never a frame "
        "(B31)") {
  PortAdcRig<HostD2> rig(kSup);
  KB_REQUIRE(rig.Start(kb::Instant{0U}) == Status::Ok);
  dma_model::RaiseFlags(1U, 4U, stm::kDmaFlagTransferError);
  KB_CHECK_EQ(dma_model::Nvic(), 1U);
  KB_CHECK_EQ(dma_model::Nvic(), 0U);
  KB_CHECK_EQ(rig.Adc().DmaCounters().transfer_errors, 1U);
  KB_CHECK_EQ(rig.Adc().Stream().Stats().published, 0U);
  KB_CHECK_EQ(dma_model::Clears(1U, 4U, stm::kDmaFlagTransferError), 1U);
}

KB_TEST("adc stream: a stopped trigger is a stall (signed time); the restart reads the stop reason "
        "and starts a new epoch") {
  PortAdcRig<HostD2> rig(kSup);
  KB_REQUIRE(rig.Start(kb::Instant{0U}) == Status::Ok);
  rig.RunHalves(1U, kb::Instant{1050U});
  port_rig::g_adc_now = kb::Instant{1000U}; // the owner read its clock before the interrupt's stamp
  KB_CHECK_EQ(rig.Adc().Service(), d::SupervisorAction::None);
  rig.RaiseOverrun(); // the reason the requests stopped
  port_rig::g_adc_now = kb::Instant{1050U + 20000U};
  KB_CHECK_EQ(rig.Adc().Service(), d::SupervisorAction::Restart);
  const d::StreamStats s = rig.Adc().Stream().Stats();
  KB_CHECK_EQ(s.stalls, 1U);
  KB_CHECK_EQ(s.restarts, 1U);
  KB_CHECK_EQ(s.hw_overruns, 1U);
  KB_CHECK_EQ(s.epoch, 1U);
  KB_CHECK_EQ(dma_model::AdcStarts(), 2U);
  // A second stall inside the back-off window waits.
  port_rig::g_adc_now = kb::Instant{1050U + 20000U + 15000U};
  KB_CHECK_EQ(rig.Adc().Service(), d::SupervisorAction::BackOff);
  rig.RunHalves(1U, port_rig::g_adc_now);
  static_cast<void>(rig.Adc().Service());
  KB_CHECK_EQ(rig.Adc().Latest().Value().epoch, 1U);
}

KB_TEST("adc stream: restarts that bring no good frame switch to the fallback") {
  d::StreamConfig sup = kSup;
  sup.restarts_before_fallback = 1U;
  sup.restart_backoff = kb::Milliseconds{0U};
  PortAdcRig<HostD2> rig(sup);
  KB_REQUIRE(rig.Start(kb::Instant{0U}) == Status::Ok);
  port_rig::g_adc_now = kb::Instant{20000U};
  KB_CHECK_EQ(rig.Adc().Service(), d::SupervisorAction::Restart);
  port_rig::g_adc_now = kb::Instant{40000U};
  KB_CHECK_EQ(rig.Adc().Service(), d::SupervisorAction::UseFallback);
  KB_CHECK_EQ(rig.Adc().Mode(), d::StreamMode::Fallback);
  KB_CHECK_EQ(rig.Adc().FallbackReason(), stm::AdcFallback::Restarts);
  KB_CHECK_EQ(rig.Adc().Capabilities().mode, d::TransferMode::Interrupt);
}

KB_TEST("adc stream: a ring the DMA lapped is published torn, the half after it clean") {
  PortAdcRig<HostD2> rig(kSup);
  KB_REQUIRE(rig.Start(kb::Instant{0U}) == Status::Ok);
  rig.Advance((2U * h::HwRing::kSamples) + 2U);
  rig.Isr(kb::Instant{500U});
  KB_CHECK_EQ(rig.Adc().Stream().Stats().torn, 1U);
  rig.Advance(1U); // inside the first half: a copy of it now would mix two passes
  KB_CHECK(rig.Adc().Core().Inside(0U));
  KB_CHECK(!rig.Adc().Core().Inside(1U));
}

KB_TEST("adc stream: a start that fails after the arm gives the ring back") {
  PortAdcRig<HostD2> rig(kSup);
  dma_model::FailAdcStart(stm::port_code::kInvalidArgument);
  KB_CHECK_EQ(rig.Start(kb::Instant{0U}), Status::InvalidArgument);
  KB_CHECK(!rig.Adc().Stream().Stats().running);
  KB_CHECK(!rig.Adc().Health().faulted);
}

// kb-core's DMA conformance suite T1-T18 (kb/dma/host/conformance.hpp) against this port: the
// port's DmaStream, DmaTransfer, SpiBus with an SpiDmaLink and AdcDmaStream, their boundary linked
// against kb-core's host DMA-and-cache model (dma_model/), through the adapter rigs of
// dma_model/port_rig.hpp. Each case runs in the port's Maintained region (H7 AXI SRAM, 32-byte
// lines: every clean and invalidate checked) and in the default NonCacheable region (H7 D2 window).
// Three cases also run with a defect in the boundary to show that they see through the port (clean,
// invalidate, EN wait). docs/dma.md records the result of each case. Test code.
// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The port-stm32 authors
#include <cstdio>

#include "dma_model.hpp"
#include "kb/dma/host/conformance.hpp"
#include "kb/dma/host/rig.hpp"
#include "kb/dma/host/stream_rig.hpp"
#include "kb_test.hpp"
#include "port_rig.hpp"

namespace c = kb::dma::host::conformance;
namespace h = kb::dma::host;
using port_rig::HostAxi;
using port_rig::HostD2;
using port_rig::PortAdcRig;
using port_rig::PortRig;

namespace {

bool Passed(const char* name, const char* region, const char* verdict) {
  std::fprintf(stderr, "    %s %s: %s%s\n", name, region,
               (verdict == nullptr) ? "pass" : "FAIL: ", (verdict == nullptr) ? "" : verdict);
  return verdict == nullptr;
}

h::RigOptions RoundTo(std::size_t n) {
  h::RigOptions o{};
  o.engine.round_rx_to = n;
  return o;
}
h::RigOptions Stuck() {
  h::RigOptions o{};
  o.engine.stuck = true;
  return o;
}
h::RigOptions NoDma() {
  h::RigOptions o{};
  o.port.has_dma = false;
  return o;
}

template <typename Case>
void Both(const char* name, Case fn, const h::RigOptions& opt = h::RigOptions{}) {
  {
    PortRig<HostAxi> rig(h::Faults{}, opt);
    KB_CHECK(Passed(name, "AXI (Maintained)", fn(rig)));
  }
  {
    PortRig<HostD2> rig(h::Faults{}, opt);
    KB_CHECK(Passed(name, "D2 (NonCacheable)", fn(rig)));
  }
}

constexpr kb::dma::StreamConfig kCfg{.frame_period = kb::Microseconds{1000U},
                                     .stall_periods = 10U,
                                     .bad_frames_to_restart = 8U,
                                     .restart_backoff = kb::Milliseconds{1000U},
                                     .restarts_before_fallback = 0U};

template <typename Case>
void BothStream(const char* name, Case fn) {
  {
    PortAdcRig<HostD2> rig(kCfg);
    KB_CHECK(Passed(name, "D2 (NonCacheable)", fn(rig)));
  }
  {
    PortAdcRig<HostAxi> rig(kCfg);
    KB_CHECK(Passed(name, "AXI (Maintained)", fn(rig)));
  }
}

} // namespace

KB_TEST("conformance T1: transmit needs a clean") {
  Both("T1", [](auto& r) { return c::T1TxNeedsClean(r); });
  dma_model::BoundaryFaults defect{};
  defect.clean_noop = true;
  PortRig<HostAxi> broken(h::Faults{}, h::RigOptions{}, defect);
  KB_CHECK(c::T1TxNeedsClean(broken) != nullptr); // a boundary without the clean is caught
}
KB_TEST("conformance T2: receive needs an invalidate before and after") {
  Both("T2", [](auto& r) { return c::T2RxNeedsInvalidate(r); });
  dma_model::BoundaryFaults defect{};
  defect.invalidate_noop = true;
  PortRig<HostAxi> broken(h::Faults{}, h::RigOptions{}, defect);
  KB_CHECK(c::T2RxNeedsInvalidate(broken) != nullptr);
}
KB_TEST("conformance T3: the CPU path does no maintenance") {
  Both("T3", [](auto& r) { return c::T3CpuPathNoMaintenance(r); });
}
KB_TEST("conformance T4: a neighbour of the receive buffer survives") {
  Both("T4", [](auto& r) { return c::T4NeighbourSurvives(r); });
}
KB_TEST("conformance T5: no CPU view during a lease") {
  Both("T5", [](auto& r) { return c::T5NoCpuDuringLease(r); });
}
KB_TEST("conformance T6: no DMA write after a timeout") {
  Both("T6", [](auto& r) { return c::T6NoWriteAfterTimeout(r); });
}
KB_TEST("conformance T7: an abort waits for the enable bit") {
  Both("T7", [](auto& r) { return c::T7AbortWaitsForEnable(r); });
  dma_model::BoundaryFaults defect{};
  defect.en_reads_zero = true;
  PortRig<HostAxi> broken(h::Faults{}, h::RigOptions{}, defect);
  KB_CHECK(c::T7AbortWaitsForEnable(broken) != nullptr);
}
KB_TEST("conformance T8: receive rounding stays inside the buffer") {
  Both("T8", [](auto& r) { return c::T8RoundingStaysInside(r); }, RoundTo(4U));
}
KB_TEST("conformance T9: a held frame is stable and the overrun shows") {
  BothStream("T9", [](auto& r) { return c::T9HeldFrame(r); });
}
KB_TEST("conformance T10: a lapped half is flagged") {
  BothStream("T10", [](auto& r) { return c::T10LappedIsFlagged(r); });
}
KB_TEST("conformance T11: one drainer") {
  BothStream("T11", [](auto& r) { return c::T11SingleDrainer(r); });
}
KB_TEST("conformance T12: the stall check is signed") {
  BothStream("T12", [](auto& r) { return c::T12SignedStall(r); });
}
KB_TEST("conformance T13: an overflow is not data") {
  BothStream("T13", [](auto& r) { return c::T13OverflowIsNotData(r); });
}
KB_TEST("conformance T14: error flags") {
  Both("T14", [](auto& r) { return c::T14ErrorFlags(r); });
}
KB_TEST("conformance T15: a cancel racing a completion") {
  Both("T15", [](auto& r) { return c::T15CancelRacesCompletion(r); });
}
KB_TEST("conformance T16: an unconfirmed stop quarantines") {
  Both("T16", [](auto& r) { return c::T16UnconfirmedQuarantines(r); }, Stuck());
}
KB_TEST("conformance T17: the fallback is reported") {
  const h::RigOptions o = NoDma();
  Both("T17", [&](auto& r) { return c::T17FallbackIsReported(r, o.port.it_ceiling); }, o);
}
KB_TEST("conformance T18: a synchronous transfer goes through staging") {
  Both("T18", [](auto& r) { return c::T18SyncThroughStaging(r); }, RoundTo(4U));
}

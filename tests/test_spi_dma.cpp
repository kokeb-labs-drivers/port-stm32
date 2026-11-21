// SPI by DMA over the host model: the order of the requests and the streams, the data through the
// staging pair, the interrupt path for short frames (no cache maintenance: the CPU path of T3), the
// staging limit, the full SCK, a timeout, an SPI overrun, stream errors, a quarantine and its
// recovery, the start-time checks (priorities, placement, MPU), capabilities and limits. Test code.
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
using port_rig::HostD2;
using port_rig::kSpiRx;
using port_rig::kSpiTx;

namespace {

stm::SpiBusConfig BusCfg() {
  stm::SpiBusConfig c{};
  c.instance = 1U;
  c.sck = PinId{GpioPort::A, 5U, 5U};
  c.miso = PinId{GpioPort::A, 6U, 5U};
  c.mosi = PinId{GpioPort::A, 7U, 5U};
  c.kernel_clock_hz = 16000000U;
  c.irq_priority = 5U;
  return c;
}

stm::SpiDeviceConfig DevCfg(uint32_t hz = 1000000U) {
  stm::SpiDeviceConfig c{};
  c.cs = PinId{GpioPort::B, 6U, 0U};
  c.mode = 0U;
  c.clock_hz = hz;
  return c;
}

template <stm::PlacedRegion R>
struct Bus {
  using Link = stm::SpiDmaLink<R, 64U>;
  h::Machine machine{32U, false};
  h::DmaEngine rx{machine};
  h::DmaEngine tx{machine};
  typename Link::Stage tx_stage{};
  typename Link::Stage rx_stage{};
  Link link;
  stm::SpiBus bus{BusCfg()};
  stm::SpiDevice dev;

  explicit Bus(const stm::SpiDmaConfig& cfg = stm::SpiDmaConfig{kSpiRx, kSpiTx, 16U},
               uint32_t hz = 1000000U)
      : link(tx_stage, rx_stage, cfg), dev(bus, DevCfg(hz)) {
    fake::Reset();
    dma_model::Reset();
    dma_model::BindMachine(&machine);
    dma_model::BindEngine(1U, 2U, &rx, 0x30U);
    dma_model::BindEngine(1U, 3U, &tx);
    const bool cacheable = R::kPolicy == d::CachePolicy::Maintained;
    static_cast<void>(machine.RegisterBuffer(tx_stage.RawMemory(d::PortKey<Bus>{}),
                                             tx_stage.Header(), cacheable));
    static_cast<void>(machine.RegisterBuffer(rx_stage.RawMemory(d::PortKey<Bus>{}),
                                             rx_stage.Header(), cacheable));
  }
  Bus(const Bus&) = delete;
  Bus& operator=(const Bus&) = delete;
  ~Bus() {
    static_cast<void>(dev.Release());
    static_cast<void>(bus.Release());
    dma_model::Reset();
  }
  [[nodiscard]] Status Start() {
    Status s = bus.AttachDma(link);
    if (s == Status::Ok) {
      s = dev.EnsureReady();
    }
    return s;
  }
};

constexpr std::array<uint8_t, 20> kTx{1,  2,  3,  4,  5,  6,  7,  8,  9,  10,
                                      11, 12, 13, 14, 15, 16, 17, 18, 19, 20};

} // namespace

KB_TEST("spi dma: the receive request, both streams, then the transmit request and the start") {
  Bus<HostD2> b;
  KB_REQUIRE(b.Start() == Status::Ok);
  std::array<uint8_t, 20> rx{};
  KB_CHECK_EQ(b.dev.Transfer(kTx, rx), Status::Ok);
  const std::size_t prepare = dma_model::Find("prepare:1:20");
  const std::size_t arm_rx = dma_model::Find("arm:1:2");
  const std::size_t arm_tx = dma_model::Find("arm:1:3");
  const std::size_t go = dma_model::Find("go:1");
  const std::size_t finish = dma_model::Find("finish:1");
  KB_CHECK(prepare < arm_rx);
  KB_CHECK(arm_rx < arm_tx);
  KB_CHECK(arm_tx < go);
  KB_CHECK(go < finish);
  KB_CHECK(finish < dma_model::Log().size());
  // The data: the receive engine's pattern into the caller's buffer, the caller's bytes out.
  KB_CHECK_EQ(rx[0], 0x30U);
  KB_CHECK_EQ(rx[19], 0x43U);
  const auto sent = b.tx.Captured();
  KB_REQUIRE(sent.size() == 20U);
  KB_CHECK_EQ(sent[0], std::byte{1U});
  KB_CHECK_EQ(sent[19], std::byte{20U});
  KB_CHECK_EQ(b.dev.Stats().dma_frames, 1U);
  KB_CHECK_EQ(b.link.Stats().frames, 1U);
  KB_CHECK_EQ(b.tx_stage.Current(), d::BufferState::Cpu);
  KB_CHECK_EQ(b.rx_stage.Current(), d::BufferState::Cpu);
  KB_CHECK_EQ(dma_model::ArmedIrqs(1U, 3U), stm::kDmaFlagTransferError | stm::kDmaFlagDirectError);
  KB_CHECK_EQ(b.machine.Total(), 0U);
}

KB_TEST("spi dma: a read-only frame clocks zeros out; a write-only frame discards what came in") {
  Bus<HostAxi> b;
  KB_REQUIRE(b.Start() == Status::Ok);
  std::array<uint8_t, 24> rx{};
  KB_CHECK_EQ(b.dev.Transfer({}, rx), Status::Ok);
  KB_CHECK_EQ(rx[1], 0x31U);
  for (const std::byte x : b.tx.Captured()) {
    KB_CHECK_EQ(x, std::byte{0U});
  }
  KB_CHECK_EQ(b.dev.Transfer(kTx, {}), Status::Ok);
  KB_CHECK_EQ(b.tx.Captured()[4], std::byte{5U});
  KB_CHECK_EQ(b.machine.Total(), 0U); // every clean and invalidate on whole owned lines
  KB_CHECK(b.machine.Maintenance().cleans > 0U);
}

KB_TEST("spi dma: frames below the minimum go by interrupt with no cache maintenance (the CPU "
        "path)") {
  Bus<HostAxi> b;
  KB_REQUIRE(b.Start() == Status::Ok);
  const h::MaintenanceCounts before = b.machine.Maintenance();
  const uint32_t transfers = fake::SpiTransfers();
  std::array<uint8_t, 4> rx{};
  KB_CHECK_EQ(b.dev.Transfer(std::span<const uint8_t>(kTx).first(4U), rx), Status::Ok);
  KB_CHECK_EQ(fake::SpiTransfers(), transfers + 1U);
  const h::MaintenanceCounts after = b.machine.Maintenance();
  KB_CHECK_EQ(after.cleans, before.cleans);
  KB_CHECK_EQ(after.invalidates, before.invalidates);
  KB_CHECK_EQ(after.barriers, before.barriers);
  KB_CHECK_EQ(b.dev.Stats().dma_frames, 0U);
  KB_CHECK(b.bus.DmaFrame(16U));
  KB_CHECK(!b.bus.DmaFrame(15U));
}

KB_TEST("spi dma: a frame larger than the staging buffers is refused, never cut") {
  Bus<HostD2> b;
  KB_REQUIRE(b.Start() == Status::Ok);
  std::array<uint8_t, 65> big{};
  KB_CHECK_EQ(b.dev.Transfer(big, {}), Status::OutOfRange);
  KB_CHECK_EQ(b.dev.LastError().Detail(), stm::port_code::kTooLarge);
  KB_CHECK_EQ(b.link.Stats().too_large, 1U);
  KB_CHECK_EQ(dma_model::Find("go:1"), dma_model::Log().size()); // nothing clocked
}

KB_TEST("spi dma: DMA frames run at the device's SCK, interrupt frames at the ceiling") {
  Bus<HostD2> b(stm::SpiDmaConfig{kSpiRx, kSpiTx, 16U}, 4000000U);
  KB_REQUIRE(b.Start() == Status::Ok);
  KB_CHECK_EQ(b.dev.ActualDmaClockHz(), 4000000U);
  KB_CHECK(b.dev.ActualClockHz() < b.dev.ActualDmaClockHz()); // 16 MHz / 40 per bit ceiling
  KB_CHECK_EQ(b.dev.SetClockHz(2000000U), Status::Ok);
  KB_CHECK_EQ(b.dev.ActualDmaClockHz(), 2000000U);
  stm::SpiBus plain{BusCfg()};
  stm::SpiDevice dev{plain, DevCfg(1000000U)};
  static_cast<void>(b.dev.Release());
  static_cast<void>(b.bus.Release());
  KB_REQUIRE(dev.EnsureReady() == Status::Ok);
  KB_CHECK_EQ(dev.ActualDmaClockHz(), dev.ActualClockHz());
  KB_CHECK_EQ(dev.SetClockHz(500000U), Status::Ok);
  KB_CHECK_EQ(dev.ActualDmaClockHz(), dev.ActualClockHz());
}

KB_TEST("spi dma: a frame that does not finish in time stops both streams with proof: Timeout") {
  Bus<HostD2> b;
  KB_REQUIRE(b.Start() == Status::Ok);
  dma_model::SetSpiRuns(false);
  std::array<uint8_t, 20> rx{};
  KB_CHECK_EQ(b.dev.Transfer(kTx, rx), Status::Timeout);
  KB_CHECK(!b.rx.Enabled());
  KB_CHECK(!b.tx.Enabled());
  KB_CHECK_EQ(b.link.Stats().timeouts, 1U);
  KB_CHECK_EQ(b.rx_stage.Current(), d::BufferState::Cpu);
  KB_CHECK(dma_model::Find("finish:1") < dma_model::Log().size());
  dma_model::SetSpiRuns(true);
  KB_CHECK_EQ(b.dev.Transfer(kTx, rx), Status::Ok);
}

KB_TEST("spi dma: an SPI overrun that stalled the receive stream is a BusError") {
  Bus<HostD2> b;
  KB_REQUIRE(b.Start() == Status::Ok);
  dma_model::SetSpiRuns(false);
  dma_model::SetSpiErrors(stm::sdk::kSpiDmaOverrun);
  std::array<uint8_t, 20> rx{};
  KB_CHECK_EQ(b.dev.Transfer(kTx, rx), Status::BusError);
  KB_CHECK_EQ(b.dev.LastError().Detail(),
              stm::VendorCode(stm::hal::kError, stm::sdk::kSpiDmaOverrun));
  KB_CHECK_EQ(b.link.Stats().bus_errors, 1U);
  // An error flag left at the end of an otherwise finished frame is a BusError too.
  dma_model::SetSpiRuns(true);
  KB_CHECK_EQ(b.dev.Transfer(kTx, rx), Status::BusError);
  dma_model::SetSpiErrors(0U);
  KB_CHECK_EQ(b.dev.Transfer(kTx, rx), Status::Ok);
}

KB_TEST("spi dma: a stream transfer error is a Fault with the stream's flags") {
  Bus<HostD2> b;
  KB_REQUIRE(b.Start() == Status::Ok);
  std::array<uint8_t, 20> rx{};
  b.rx.InjectError(3U, h::kFlagTransferError);
  KB_CHECK_EQ(b.dev.Transfer(kTx, rx), Status::Fault);
  KB_CHECK_EQ(b.dev.LastError().Detail(),
              stm::VendorCode(stm::port_code::kDma, stm::kDmaFlagTransferError));
  KB_CHECK_EQ(b.link.Stats().errors, 1U);
  KB_CHECK_EQ(b.dev.Transfer(kTx, rx), Status::Ok); // the bus was set up again
  b.tx.InjectError(3U, h::kFlagTransferError);
  KB_CHECK_EQ(b.dev.Transfer(kTx, rx), Status::Fault);
  KB_CHECK(b.bus.Health().errors >= 2U);
}

KB_TEST("spi dma: a stop that cannot be confirmed quarantines the bus until Recover()") {
  Bus<HostAxi> b;
  KB_REQUIRE(b.Start() == Status::Ok);
  h::EngineConfig stuck{};
  stuck.stuck = true;
  b.rx.Configure(stuck);
  dma_model::SetSpiRuns(false);
  std::array<uint8_t, 20> rx{};
  KB_CHECK_EQ(b.dev.Transfer(kTx, rx), Status::Fault);
  KB_CHECK_EQ(b.dev.LastError().Detail(), stm::port_code::kQuarantined);
  KB_CHECK(b.bus.Health().faulted);
  KB_CHECK_EQ(b.rx_stage.Current(), d::BufferState::Quarantined);
  KB_CHECK_EQ(b.dev.Transfer(kTx, rx), Status::Fault); // refused, nothing started
  KB_CHECK_EQ(b.bus.Recover(), Status::Ok);
  KB_CHECK(!b.bus.Health().faulted);
  KB_CHECK_EQ(b.rx_stage.Current(), d::BufferState::Cpu);
  KB_CHECK_EQ(b.link.Stats().recoveries, 1U);
  dma_model::SetSpiRuns(true);
  KB_CHECK_EQ(b.dev.Transfer(kTx, rx), Status::Ok);
}

KB_TEST("spi dma: the start refuses streams at another priority than the SPI interrupt, a receive "
        "stream without its completion, a misplaced buffer, an MPU that leaves the window cached") {
  {
    d::StreamAssignment rx = kSpiRx;
    rx.irq_priority = 6U;
    Bus<HostD2> b(stm::SpiDmaConfig{rx, kSpiTx, 16U});
    KB_CHECK_EQ(b.Start(), Status::NotReady); // the device's bus did not start
    KB_CHECK_EQ(b.bus.LastError().Error(), Status::InvalidArgument);
  }
  {
    d::StreamAssignment rx = kSpiRx;
    rx.irq = d::IrqUse::ErrorsOnly;
    Bus<HostD2> b(stm::SpiDmaConfig{rx, kSpiTx, 16U});
    KB_CHECK_EQ(b.Start(), Status::NotReady);
    KB_CHECK_EQ(b.bus.LastError().Error(), Status::InvalidArgument);
  }
  {
    Bus<stm::h7::D2Sram> b; // the H7 window: the host's buffers are not in it
    KB_CHECK_EQ(b.Start(), Status::NotReady);
    KB_CHECK_EQ(b.bus.LastError().Detail(), stm::port_code::kPlacement);
    KB_CHECK(!b.bus.IsReady());
  }
  {
    Bus<HostD2> b;
    dma_model::SetCache(true, false);
    KB_CHECK_EQ(b.Start(), Status::NotReady);
  }
  {
    Bus<HostD2> b;
    dma_model::ForcePriorityReadback(2);
    KB_CHECK_EQ(b.Start(), Status::NotReady);
    KB_CHECK_EQ(b.bus.LastError().Detail(), stm::port_code::kPriority);
  }
  {
    Bus<HostD2> b;
    KB_REQUIRE(b.Start() == Status::Ok);
    KB_CHECK_EQ(b.bus.AttachDma(b.link), Status::InvalidArgument); // ready: too late
  }
  {
    Bus<HostD2> b;
    dma_model::FailSpiPrepare(stm::port_code::kInvalidArgument);
    KB_REQUIRE(b.Start() == Status::Ok);
    std::array<uint8_t, 20> rx{};
    KB_CHECK_EQ(b.dev.Transfer(kTx, rx), Status::InvalidArgument);
  }
}

KB_TEST("spi dma: capabilities, health and limits say which path serves") {
  Bus<HostAxi> b;
  KB_REQUIRE(b.Start() == Status::Ok);
  const d::Capabilities c = b.bus.Capabilities();
  KB_CHECK_EQ(c.mode, d::TransferMode::Dma);
  KB_CHECK(c.confirmed_abort);
  KB_CHECK_EQ(c.max_frame_bytes, 64U);
  KB_CHECK_EQ(c.staging_bytes, 64U);
  KB_CHECK_EQ(c.align_bytes, 32U);
  KB_CHECK_EQ(b.bus.Health().mode, d::TransferMode::Dma);
  const d::BusLimits l = b.bus.Limits();
  KB_CHECK(l.dma);
  KB_CHECK_EQ(l.MaxFrame(), 64U);
  KB_CHECK_EQ(b.link.MinBytes(), 16U);
  KB_CHECK_EQ(b.link.RxStream().Assignment().stream, 2U);
  KB_CHECK_EQ(b.link.TxStream().Assignment().stream, 3U);
  KB_CHECK_EQ(b.link.Config().min_bytes, 16U);
  stm::SpiBus plain{BusCfg()};
  KB_CHECK_EQ(plain.Capabilities().mode, d::TransferMode::Interrupt);
  KB_CHECK_EQ(plain.Health().mode, d::TransferMode::Interrupt);
  KB_CHECK(!plain.Limits().dma);
  KB_CHECK_EQ(plain.Limits().MaxFrame(), stm::kSpiNoDmaMaxFrame);
  KB_CHECK_EQ(plain.Recover(), Status::Ok);
  stm::Deadline deadline{kb::Milliseconds{1U}};
  KB_CHECK_EQ(plain.DmaTransferLocked({}, {}, deadline), stm::port_code::kUnsupported);
  // Compile time: a driver's largest frame against the bus.
  using Limits = stm::SpiLimitsOf<Bus<HostAxi>::Link>;
  static_assert(Limits::kBus.MaxFrame() == 64U);
  constexpr bool kFits = (d::RequireBusCarries<Limits, 64U>(), true);
  static_assert(kFits);
  static_assert(stm::SpiLimitsNoDma::kBus.MaxFrame() == stm::kSpiNoDmaMaxFrame);
}

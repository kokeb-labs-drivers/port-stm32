// The adapter rigs of kb-core's DMA conformance suite (kb/dma/host/conformance.hpp) for this port:
// the same member functions as kb::dma::host::HostRig and AdcRig, over the port's real classes
// (DmaStream, DmaTransfer, SpiBus with an SpiDmaLink, AdcDmaStream) with their boundary linked
// against the host model (dma_model.cpp). Port() forwards each call to the port's DmaTransfer of
// the last-started direction, translating only the result types into the suite's (PortPoll,
// PortStats). Test code, outside the MISRA claim. SPDX-License-Identifier: MIT Copyright (c) 2026
// The port-stm32 authors
#ifndef KB_PORT_STM32_TESTS_PORT_RIG_HPP
#define KB_PORT_STM32_TESTS_PORT_RIG_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

#include "dma_model.hpp"
#include "fake_stm32_hal.hpp"
#include "kb/dma/buffer.hpp"
#include "kb/dma/host/engine.hpp"
#include "kb/dma/host/machine.hpp"
#include "kb/dma/host/model_port.hpp"
#include "kb/dma/host/rig.hpp"
#include "kb/dma/host/stream_rig.hpp"
#include "kb/port/stm32/adc_stream.hpp"
#include "kb/port/stm32/dma.hpp"
#include "kb/port/stm32/irq.hpp"
#include "kb/port/stm32/spi.hpp"
#include "kb/port/stm32/spi_dma.hpp"

namespace port_rig {

namespace h = kb::dma::host;
namespace stm = kb::port::stm32;

/// The port's H7 regions with their placement window opened to the whole host address space (the
/// host's buffers are not at the H7 addresses); policy, line, alignment and masters are the port's.
struct HostAxi : stm::h7::AxiSram {
  static constexpr std::uintptr_t kBase = 0U;
  static constexpr std::size_t kSize = std::numeric_limits<std::size_t>::max();
};
struct HostD2 : stm::h7::D2Sram {
  static constexpr std::uintptr_t kBase = 0U;
  static constexpr std::size_t kSize = std::numeric_limits<std::size_t>::max();
};

/// The streams of the rig.
inline constexpr kb::dma::StreamAssignment kRxStream{1U,
                                                     0U,
                                                     37U,
                                                     kb::dma::ChannelPriority::High,
                                                     5U,
                                                     kb::dma::Notify::Task,
                                                     kb::dma::IrqUse::All,
                                                     kb::dma::ChannelMode::Normal};
inline constexpr kb::dma::StreamAssignment kTxStream{1U,
                                                     1U,
                                                     38U,
                                                     kb::dma::ChannelPriority::High,
                                                     5U,
                                                     kb::dma::Notify::Task,
                                                     kb::dma::IrqUse::All,
                                                     kb::dma::ChannelMode::Normal};
/// The SPI bus's streams for the synchronous case (T18).
inline constexpr kb::dma::StreamAssignment kSpiRx{1U,
                                                  2U,
                                                  37U,
                                                  kb::dma::ChannelPriority::High,
                                                  5U,
                                                  kb::dma::Notify::Task,
                                                  kb::dma::IrqUse::All,
                                                  kb::dma::ChannelMode::Normal};
inline constexpr kb::dma::StreamAssignment kSpiTx{1U,
                                                  3U,
                                                  38U,
                                                  kb::dma::ChannelPriority::High,
                                                  5U,
                                                  kb::dma::Notify::Task,
                                                  kb::dma::IrqUse::ErrorsOnly,
                                                  kb::dma::ChannelMode::Normal};

/** @brief The buffer-transfer rig (HostRig's members) over the port in region `R`. */
template <stm::PlacedRegion R>
class PortRig {
public:
  using Region = R;
  static constexpr std::size_t kStageBytes = 100U;
  using Stage = kb::dma::Buffer<R, kStageBytes>;
  static constexpr std::size_t kNeighbourBytes = 32U;
  static constexpr std::size_t kCallerOffset = 259U;
  static constexpr uint8_t kRxSeed = 0x60U;
  static constexpr std::size_t kCallerBytes = 5U;
  static constexpr std::size_t kCanaryBytes = 16U;
  static constexpr bool kCacheable = R::kPolicy == kb::dma::CachePolicy::Maintained;

  /** @brief Port() of the rig: the port's DmaTransfer of the active direction. */
  class PortView {
  public:
    explicit PortView(PortRig& rig) noexcept : rig_(rig) {}
    void Tick() noexcept {
      rig_.engine_.Tick();
      static_cast<void>(dma_model::Nvic());
    }
    [[nodiscard]] h::PortPoll Poll(kb::Instant now) noexcept {
      return Translate(rig_.Active().Poll(now));
    }
    [[nodiscard]] kb::Status Cancel() noexcept {
      return rig_.Active().Cancel();
    }
    [[nodiscard]] kb::Status Recover(Stage& buffer) noexcept {
      return ((&buffer == &rig_.tx_) ? rig_.tx_transfer_ : rig_.rx_transfer_).Recover(buffer);
    }
    [[nodiscard]] kb::dma::Capabilities Capabilities() const noexcept {
      return rig_.Active().Capabilities();
    }
    [[nodiscard]] kb::dma::DmaHealth Health() const noexcept {
      return rig_.Active().Health();
    }
    [[nodiscard]] h::PortStats Stats() const noexcept {
      const stm::DmaStats s = rig_.Active().Stats();
      h::PortStats out{};
      out.transfer_errors = s.transfer_errors;
      out.fifo_errors = s.fifo_errors;
      out.completions = s.completions;
      out.timeouts = s.timeouts;
      out.cancels = s.cancels;
      return out;
    }

  private:
    [[nodiscard]] static h::PortPoll Translate(const stm::DmaPoll& p) noexcept {
      h::PortPoll out{};
      out.state = (p.state == stm::DmaPollState::Done)      ? h::PollState::Done
                  : (p.state == stm::DmaPollState::Pending) ? h::PollState::Pending
                                                            : h::PollState::Idle;
      out.status = p.status;
      out.count = p.count;
      const uint32_t flags = p.detail >> 8U;
      out.detail = (((flags & stm::kDmaFlagTransferError) != 0U) ? h::kFlagTransferError : 0U) |
                   (((flags & stm::kDmaFlagFifoError) != 0U) ? h::kFlagFifoError : 0U);
      return out;
    }
    PortRig& rig_;
  };

  explicit PortRig(const h::Faults& faults = h::Faults{},
                   const h::RigOptions& options = h::RigOptions{},
                   const dma_model::BoundaryFaults& boundary = dma_model::BoundaryFaults{})
      : machine_(32U, options.speculation), engine_(machine_, options.engine),
        rx_stream_(
            options.port.has_dma ? StreamOrNone(kRxStream) : kb::dma::StreamAssignment{},
            stm::DmaPeripheral{0x40013030U, stm::DmaWidth::Byte, kb::dma::Direction::FromDevice},
            stm::DmaStreamLimits{options.port.dma_ceiling, options.port.it_ceiling}),
        tx_stream_(
            kTxStream,
            stm::DmaPeripheral{0x40013020U, stm::DmaWidth::Byte, kb::dma::Direction::ToDevice},
            stm::DmaStreamLimits{options.port.dma_ceiling, options.port.it_ceiling}),
        rx_transfer_(rx_stream_), tx_transfer_(tx_stream_), view_(*this) {
    static_cast<void>(faults); // the port has no defect switches: it is the port
    fake::Reset();
    dma_model::Reset();
    dma_model::SetFaults(boundary);
    dma_model::BindMachine(&machine_);
    dma_model::BindEngine(1U, 0U, &engine_);
    dma_model::BindEngine(1U, 1U, &engine_);
    static_cast<void>(machine_.RegisterBuffer(RawOf(tx_), tx_.Header(), kCacheable));
    static_cast<void>(
        machine_.RegisterBuffer(RawOf(pack_.stage), pack_.stage.Header(), kCacheable));
    static_cast<void>(machine_.RegisterRaw(pack_.neighbour, true));
    static_cast<void>(machine_.RegisterRaw(arena_.mem, true));
    if (options.port.has_dma) {
      static_cast<void>(rx_stream_.EnsureReady());
    }
    static_cast<void>(tx_stream_.EnsureReady());
    // T18's SPI bus: its own two streams (one engine each) and staging pair.
    dma_model::BindEngine(1U, 2U, &spi_rx_engine_, kRxSeed);
    dma_model::BindEngine(1U, 3U, &spi_tx_engine_);
    static_cast<void>(machine_.RegisterBuffer(RawOf(spi_tx_), spi_tx_.Header(), kCacheable));
    static_cast<void>(machine_.RegisterBuffer(RawOf(spi_rx_), spi_rx_.Header(), kCacheable));
    spi_rx_engine_.Configure(options.engine);
  }
  PortRig(const PortRig&) = delete;
  PortRig& operator=(const PortRig&) = delete;
  ~PortRig() {
    static_cast<void>(device_.Release());
    static_cast<void>(bus_.Release());
    static_cast<void>(rx_stream_.Release());
    static_cast<void>(tx_stream_.Release());
    dma_model::Reset();
  }

  [[nodiscard]] h::Machine& M() noexcept {
    return machine_;
  }
  [[nodiscard]] h::DmaEngine& Engine() noexcept {
    return engine_;
  }
  [[nodiscard]] PortView& Port() noexcept {
    return view_;
  }
  [[nodiscard]] Stage& TxStage() noexcept {
    return tx_;
  }
  [[nodiscard]] Stage& RxStage() noexcept {
    return pack_.stage;
  }
  [[nodiscard]] std::span<std::byte> TxView() noexcept {
    return tx_.CpuView();
  }
  [[nodiscard]] std::span<std::byte> RxView() noexcept {
    return pack_.stage.CpuView();
  }
  void CpuFill(std::span<std::byte> view, std::size_t n, uint8_t seed) noexcept {
    for (std::size_t i = 0U; (i < n) && (i < view.size()); ++i) {
      const auto b = static_cast<std::byte>(static_cast<uint8_t>(seed + i));
      machine_.CpuStore(&view[i], std::span<const std::byte>(&b, 1U));
    }
  }
  [[nodiscard]] bool CpuReadsBack(std::span<std::byte> view, std::size_t n, uint8_t seed) noexcept {
    bool same = view.size() >= n;
    for (std::size_t i = 0U; same && (i < n); ++i) {
      std::byte b{0U};
      machine_.CpuLoad(&view[i], std::span<std::byte>(&b, 1U));
      same = (b == static_cast<std::byte>(static_cast<uint8_t>(seed + i)));
    }
    return same;
  }
  [[nodiscard]] kb::Status StartTx(std::size_t n) noexcept {
    active_tx_ = true;
    return tx_transfer_.Start(tx_, kb::dma::Direction::ToDevice, n, far());
  }
  [[nodiscard]] kb::Status StartRx(std::size_t n, uint8_t seed,
                                   kb::utils::Deadline deadline = kb::utils::Deadline{}) noexcept {
    active_tx_ = false;
    dma_model::SetSeed(1U, 0U, seed);
    return rx_transfer_.Start(pack_.stage, kb::dma::Direction::FromDevice, n,
                              deadline.Expired(now_) ? far() : deadline);
  }
  [[nodiscard]] kb::Instant Now() const noexcept {
    return now_;
  }
  void AdvanceUs(uint64_t us) noexcept {
    now_ = kb::Instant{now_.Us() + us};
  }
  [[nodiscard]] kb::utils::Deadline DeadlineIn(uint32_t ms) const noexcept {
    return kb::utils::Deadline::After(now_, kb::Milliseconds{ms});
  }
  [[nodiscard]] h::PortPoll RunToDone(std::size_t max_ticks = 4096U) noexcept {
    h::PortPoll poll{};
    poll.state = h::PollState::Pending;
    for (std::size_t i = 0U; (i < max_ticks) && (poll.state == h::PollState::Pending); ++i) {
      view_.Tick();
      poll = view_.Poll(now_);
    }
    return poll;
  }
  void Ticks(std::size_t n) noexcept {
    for (std::size_t i = 0U; i < n; ++i) {
      view_.Tick();
    }
  }
  /// T3: the port's CPU path (an interrupt-driven frame) copies by CPU and calls no cache hook; the
  /// rig's CPU copies the same way (test_spi_dma.cpp proves the port's path makes no maintenance
  /// call).
  void CpuPathCopyIn(std::span<std::byte> view, std::size_t n, uint8_t seed) noexcept {
    CpuFill(view, n, seed);
  }
  [[nodiscard]] std::byte* Neighbour(std::size_t /*n*/) noexcept {
    return pack_.neighbour.data();
  }
  void FillNeighbour(std::size_t n, uint8_t value) noexcept {
    std::array<std::byte, kNeighbourBytes> bytes{};
    bytes.fill(static_cast<std::byte>(value));
    machine_.CpuStore(Neighbour(n), bytes);
  }
  [[nodiscard]] bool NeighbourIntact(std::size_t n, uint8_t value) noexcept {
    std::array<std::byte, kNeighbourBytes> bytes{};
    machine_.CpuLoad(Neighbour(n), bytes);
    bool same = true;
    for (const std::byte b : bytes) {
      same = same && (b == static_cast<std::byte>(value));
    }
    return same;
  }
  [[nodiscard]] bool BeginReceive(std::size_t n, uint8_t seed) noexcept {
    return StartRx(n, seed) == kb::Status::Ok;
  }
  [[nodiscard]] bool FinishReceive() noexcept {
    return RunToDone().status == kb::Status::Ok;
  }
  [[nodiscard]] bool ReceiveIntoTarget(std::size_t n, uint8_t seed) noexcept {
    return BeginReceive(n, seed) && FinishReceive();
  }
  [[nodiscard]] bool TargetReadsBack(std::size_t n, uint8_t seed) noexcept {
    return CpuReadsBack(pack_.stage.CpuView(), n, seed);
  }
  [[nodiscard]] std::span<std::byte> CallerBuffer() noexcept {
    return {arena_.mem.data() + kCallerOffset, kCallerBytes};
  }
  void ArmCallerCanaries() noexcept {
    std::array<std::byte, kCanaryBytes> pattern{};
    pattern.fill(std::byte{0xC3U});
    machine_.CpuStore(arena_.mem.data() + kCallerOffset - kCanaryBytes, pattern);
    machine_.CpuStore(arena_.mem.data() + kCallerOffset + kCallerBytes, pattern);
    machine_.EvictAll();
  }
  [[nodiscard]] bool CallerCanariesIntact() noexcept {
    std::array<std::byte, kCanaryBytes> before{};
    std::array<std::byte, kCanaryBytes> after{};
    machine_.CpuLoad(arena_.mem.data() + kCallerOffset - kCanaryBytes, before);
    machine_.CpuLoad(arena_.mem.data() + kCallerOffset + kCallerBytes, after);
    bool intact = true;
    for (std::size_t i = 0U; i < kCanaryBytes; ++i) {
      intact = intact && (before[i] == std::byte{0xC3U}) && (after[i] == std::byte{0xC3U});
    }
    return intact;
  }
  /// T18: the port's real synchronous SpiDevice::Transfer on a bus with a DMA link: a transmit-only
  /// frame of `tx`, then a receive-only frame into the caller's `rx`, each through the staging
  /// pair.
  [[nodiscard]] kb::Status SyncTransfer(std::span<const std::byte> tx,
                                        std::span<std::byte> rx) noexcept {
    kb::Status status = startBus();
    std::array<uint8_t, kStageBytes> out{};
    for (std::size_t i = 0U; (i < tx.size()) && (i < out.size()); ++i) {
      out[i] = static_cast<uint8_t>(tx[i]);
    }
    if ((status == kb::Status::Ok) && !tx.empty()) {
      status = device_.Transfer(std::span<const uint8_t>(out.data(), tx.size()), {});
    }
    if ((status == kb::Status::Ok) && !rx.empty()) {
      // The caller's bytes, not a staging buffer: the port copies into them after the frame.
      // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): a byte span seen as uint8_t
      const std::span<uint8_t> caller(reinterpret_cast<uint8_t*>(rx.data()), rx.size());
      status = device_.Transfer({}, caller);
    }
    return status;
  }

private:
  struct alignas(128) Arena {
    std::array<std::byte, 512> mem{};
  };
  struct Pack {
    Stage stage;
    alignas(32) std::array<std::byte, kNeighbourBytes> neighbour{};
  };

  [[nodiscard]] static constexpr kb::dma::StreamAssignment StreamOrNone(
      kb::dma::StreamAssignment a) {
    return a;
  }
  template <std::size_t B>
  [[nodiscard]] std::span<std::byte> RawOf(kb::dma::Buffer<R, B>& buffer) noexcept {
    return buffer.RawMemory(kb::dma::PortKey<PortRig>{});
  }
  [[nodiscard]] stm::DmaTransfer<R>& Active() noexcept {
    return active_tx_ ? tx_transfer_ : rx_transfer_;
  }
  [[nodiscard]] const stm::DmaTransfer<R>& Active() const noexcept {
    return active_tx_ ? tx_transfer_ : rx_transfer_;
  }
  [[nodiscard]] kb::utils::Deadline far() const noexcept {
    return kb::utils::Deadline::After(now_, kb::Milliseconds{1000000U});
  }
  [[nodiscard]] kb::Status startBus() noexcept {
    kb::Status status = kb::Status::Ok;
    if (!bus_.IsReady()) {
      status = bus_.AttachDma(link_);
      if (status == kb::Status::Ok) {
        status = device_.EnsureReady();
      }
    }
    return status;
  }

  h::Machine machine_;
  h::DmaEngine engine_;
  h::DmaEngine spi_rx_engine_{machine_};
  h::DmaEngine spi_tx_engine_{machine_};
  stm::DmaStream rx_stream_;
  stm::DmaStream tx_stream_;
  stm::DmaTransfer<R> rx_transfer_;
  stm::DmaTransfer<R> tx_transfer_;
  PortView view_;
  Stage tx_;
  Pack pack_{};
  Arena arena_{};
  Stage spi_tx_;
  Stage spi_rx_;
  stm::SpiDmaLink<R, kStageBytes> link_{spi_tx_, spi_rx_, stm::SpiDmaConfig{kSpiRx, kSpiTx, 1U}};
  stm::SpiBus bus_{stm::SpiBusConfig{1U, stm::PinId{stm::GpioPort::A, 5U, 5U},
                                     stm::PinId{stm::GpioPort::A, 7U, 5U},
                                     stm::PinId{stm::GpioPort::A, 6U, 5U}, 16000000U, 5U}};
  stm::SpiDevice device_{bus_, stm::SpiDeviceConfig{stm::PinId{stm::GpioPort::B, 6U, 0U}, 0U,
                                                    1000000U, kb::Milliseconds{20U}}};
  kb::Instant now_;
  bool active_tx_{false};
};

/// The clock the ADC rig's interrupt stamps frames with.
inline kb::Instant g_adc_now{};
inline kb::Instant AdcNow() noexcept {
  return g_adc_now;
}

/// The ADC rig's stream.
inline constexpr kb::dma::StreamAssignment kAdcStream{1U,
                                                      4U,
                                                      9U,
                                                      kb::dma::ChannelPriority::High,
                                                      4U,
                                                      kb::dma::Notify::None,
                                                      kb::dma::IrqUse::All,
                                                      kb::dma::ChannelMode::Circular};
inline constexpr std::array<uint8_t, 4> kAdcChannels{1U, 2U, 3U, 4U};

/** @brief The stream rig (AdcRig's members) over the port's AdcDmaStream<Scan, R>. */
template <stm::PlacedRegion R>
class PortAdcRig {
public:
  using Port = stm::AdcDmaStream<h::Scan, R>;

  explicit PortAdcRig(const kb::dma::StreamConfig& supervision, uint32_t frames_per_pass = 2U)
      : machine_(R::kLineBytes, false), adc_(ring_, Config(supervision, frames_per_pass)) {
    fake::Reset();
    dma_model::Reset();
    dma_model::BindMachine(&machine_);
    dma_model::BindRing(1U, 4U, &ring_hw_, &machine_);
    static_cast<void>(machine_.RegisterBuffer(ring_.RawMemory(kb::dma::PortKey<PortAdcRig>{}),
                                              ring_.Header(),
                                              R::kPolicy == kb::dma::CachePolicy::Maintained));
  }
  PortAdcRig(const PortAdcRig&) = delete;
  PortAdcRig& operator=(const PortAdcRig&) = delete;
  ~PortAdcRig() {
    static_cast<void>(adc_.Stop());
    dma_model::Reset();
  }

  [[nodiscard]] kb::Status Start(kb::Instant now) noexcept {
    g_adc_now = now;
    return adc_.Start();
  }
  void Advance(std::size_t n) noexcept {
    ring_hw_.Advance(n);
  }
  void RunHalves(std::size_t halves, kb::Instant at) noexcept {
    for (std::size_t i = 0U; i < halves; ++i) {
      ring_hw_.Advance(h::HwRing::kSamples);
      Isr(at);
    }
  }
  /// The stream's vector runs at `at` (through the port's dispatch, HandleDmaIrq).
  void Isr(kb::Instant at) noexcept {
    g_adc_now = at;
    stm::HandleDmaIrq(kAdcStream.engine, kAdcStream.stream);
  }
  /// The port has no task-side drain: the owner never touches the stream's flags.
  void TaskDrainPreempted(kb::Instant /*at*/) noexcept {}
  void RaiseOverrun() noexcept {
    ring_hw_.RaiseOverrun();
  }
  [[nodiscard]] uint32_t HalvesDone() const noexcept {
    return ring_hw_.Halves();
  }
  [[nodiscard]] typename Port::StreamType& StreamRef() noexcept {
    return adc_.Stream();
  }
  [[nodiscard]] Port& Adc() noexcept {
    return adc_;
  }
  [[nodiscard]] h::Machine& M() noexcept {
    return machine_;
  }

private:
  [[nodiscard]] static stm::AdcStreamConfig Config(const kb::dma::StreamConfig& supervision,
                                                   uint32_t frames_per_pass) noexcept {
    stm::AdcStreamConfig c{};
    c.instance = 1U;
    c.channels = kAdcChannels;
    c.trigger = 0x7U;
    c.dma = kAdcStream;
    c.shape = kb::dma::StreamShape{2U, 2U, 4U};
    c.supervision = supervision;
    c.now = &AdcNow;
    c.frames_per_pass = frames_per_pass;
    return c;
  }

  h::Machine machine_;
  h::HwRing ring_hw_{};
  typename Port::Ring ring_{};
  Port adc_;
};

} // namespace port_rig

#endif // KB_PORT_STM32_TESTS_PORT_RIG_HPP

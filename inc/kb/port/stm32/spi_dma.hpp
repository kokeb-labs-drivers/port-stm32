/**
 * @file spi_dma.hpp
 * @brief SPI by DMA (docs/dma.md): `kb::port::stm32::SpiDmaLink<Region, Bytes>`, the receive and
 * transmit streams of one SpiBus with its pair of port-owned staging buffers, attached with
 * `SpiBus::AttachDma()` before the bus starts.
 *
 * **Staging.** A caller's buffers are never DMA targets: `SpiDevice::Transfer` copies `tx` into the
 * transmit staging buffer (zeros for a read-only frame), lends both buffers (`kb::dma::Lease`, with
 * the region's cache maintenance: none for the H7 D2 window, clean and invalidate by line for AXI
 * SRAM), runs both streams full duplex, and copies the received bytes out of the receive staging
 * buffer. The buffers are `kb::dma::Buffer`s the board defines in its DMA region; their placement
 * and the region's MPU attributes are checked at `SpiBus::EnsureReady()`.
 *
 * **Which frames.** Frames of at least `SpiDmaConfig::min_bytes` go by DMA at the device's full SCK
 * (the interrupt ceiling does not apply); shorter frames go by interrupt (setup costs more than it
 * saves). A frame larger than the staging buffers is refused (`OutOfRange`): the board sizes them
 * to the largest frame of the bus's drivers, which `kb::dma::RequireBusCarries<SpiLimitsOf<Link>,
 * N>` checks at compile time.
 *
 * **Order.** H7 (SPI with TSIZE): TSIZE written with SPE clear, RXDMAEN, both streams enabled,
 * TXDMAEN, SPE, CSTART. F4, G4, L4: RXDMAEN, both streams, TXDMAEN, SPE. The frame ends with the
 * receive stream's completion (the transmit stream may run with its error interrupts only); then
 * the requests are disabled and, on H7, SPE is cleared.
 *
 * **Errors.** A transfer or direct-mode error of either stream: `Fault` (Detail: port code 0xFA and
 * the stream flags). An SPI overrun or mode fault: `BusError`. The deadline: both streams stopped
 * (EN must read 0), `Timeout`. A stop that is not confirmed quarantines the staging buffers: the
 * bus answers `Fault` (port code 0xFB) until `SpiBus::Recover()`.
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 The port-stm32 authors
 */
#ifndef KB_PORT_STM32_SPI_DMA_HPP
#define KB_PORT_STM32_SPI_DMA_HPP

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <utility>

#include "kb/dma/assignment.hpp"
#include "kb/dma/buffer.hpp"
#include "kb/dma/capabilities.hpp"
#include "kb/dma/completion.hpp"
#include "kb/port/stm32/completion.hpp"
#include "kb/port/stm32/dma.hpp"
#include "kb/port/stm32/error.hpp"
#include "kb/utils/status.hpp"

namespace kb::port::stm32 {

/** @brief The largest frame of a bus without DMA: no limit (chunks of 65535 under chip select). */
inline constexpr std::size_t kSpiNoDmaMaxFrame = std::numeric_limits<std::size_t>::max();

/** @brief The streams of one SPI bus, as the board assigns them. */
struct SpiDmaConfig {
  kb::dma::StreamAssignment rx{}; ///< The receive stream (its completion ends the frame).
  kb::dma::StreamAssignment tx{}; ///< The transmit stream (`IrqUse::ErrorsOnly` saves an IRQ).
  std::size_t min_bytes{16U};     ///< Frames of at least this size go by DMA, shorter by interrupt.
};

/** @brief Counters of a bus's DMA path. */
struct SpiDmaStats {
  uint32_t frames{0U};     ///< Frames started by DMA.
  uint32_t errors{0U};     ///< Frames that ended in a stream error.
  uint32_t bus_errors{0U}; ///< Frames that ended in an SPI overrun or mode fault.
  uint32_t timeouts{0U};   ///< Frames stopped at their deadline.
  uint32_t too_large{0U};  ///< Frames refused as larger than the staging buffers.
  uint32_t recoveries{0U}; ///< Recover() calls that returned the path to service.
};

/**
 * @brief The platform-free part of a bus's DMA path (src/spi_dma.cpp): the two streams, the frame
 * sequence, the error mapping. `SpiDmaLink` adds the staging buffers and their leases.
 */
class SpiDmaPath {
public:
  SpiDmaPath(const SpiDmaPath&) = delete;
  SpiDmaPath& operator=(const SpiDmaPath&) = delete;
  SpiDmaPath(SpiDmaPath&&) = delete;
  SpiDmaPath& operator=(SpiDmaPath&&) = delete;
  ~SpiDmaPath() = default;

  /**
   * @brief At SpiBus::EnsureReady(): the staging placement and region checks, the streams bound to
   * SPI `instance`'s data registers and started, their priorities equal to the SPI interrupt's
   * `irq_priority`. The vendor code (0: ready).
   */
  [[nodiscard]] uint32_t Ready(uint8_t instance, uint8_t irq_priority) noexcept;
  /** @brief Stops and releases both streams. */
  void Close() noexcept;
  /**
   * @brief One full-duplex frame through the staging buffers, waited for on `deadline`; `done` is
   * the bus's Completion. The vendor code (0: done).
   */
  [[nodiscard]] uint32_t Frame(uint8_t instance, std::span<const uint8_t> tx, std::span<uint8_t> rx,
                               Deadline& deadline, Completion& done) noexcept;
  /** @brief After a quarantine: both streams re-initialised with proof, the buffers released. */
  [[nodiscard]] kb::Status Recover() noexcept;

  /** @brief Frames of at least this size go by DMA. */
  [[nodiscard]] std::size_t MinBytes() const noexcept {
    return config_.min_bytes;
  }
  /** @brief The staging capacity: the largest DMA frame. */
  [[nodiscard]] std::size_t MaxBytes() const noexcept {
    return stage_bytes_;
  }
  /** @brief The alignment unit of the staging buffers. */
  [[nodiscard]] std::size_t AlignBytes() const noexcept {
    return align_bytes_;
  }
  /** @brief True after a stop could not be confirmed, until Recover(). */
  [[nodiscard]] bool Faulted() const noexcept {
    return rx_.Faulted() || tx_.Faulted();
  }
  /** @brief The DMA part of the bus's health record. */
  [[nodiscard]] kb::dma::DmaHealth Health() const noexcept;
  /** @brief The counters of the path. */
  [[nodiscard]] SpiDmaStats Stats() const noexcept;
  /** @brief The receive stream. */
  [[nodiscard]] const DmaStream& RxStream() const noexcept {
    return rx_;
  }
  /** @brief The transmit stream. */
  [[nodiscard]] const DmaStream& TxStream() const noexcept {
    return tx_;
  }
  /** @brief The board's configuration. */
  [[nodiscard]] const SpiDmaConfig& Config() const noexcept {
    return config_;
  }

protected:
  /** @brief What the staging buffers' owner does (SpiDmaLink): placement, lend, settle, recover. */
  struct Hooks {
    uint32_t (*place)(SpiDmaPath& self) noexcept;
    uint32_t (*lend)(SpiDmaPath& self, std::span<const uint8_t> tx, std::size_t bytes,
                     std::span<std::byte>& tx_memory, std::span<std::byte>& rx_memory) noexcept;
    void (*settle)(SpiDmaPath& self, std::span<uint8_t> rx, bool copy_out) noexcept;
    kb::Status (*recover)(SpiDmaPath& self) noexcept;
  };

  /** @brief A path over staging buffers of `stage_bytes` aligned to `align_bytes`. */
  SpiDmaPath(const SpiDmaConfig& config, std::size_t stage_bytes, std::size_t align_bytes,
             const Hooks& hooks) noexcept
      : config_(config), hooks_(hooks), stage_bytes_(stage_bytes), align_bytes_(align_bytes) {}

  /** @brief The receive stream (the owner settles its lease). */
  [[nodiscard]] DmaStream& Rx() noexcept {
    return rx_;
  }
  /** @brief The transmit stream. */
  [[nodiscard]] DmaStream& Tx() noexcept {
    return tx_;
  }

private:
  [[nodiscard]] uint32_t run(uint8_t instance, std::span<std::byte> tx, std::span<std::byte> rx,
                             std::size_t bytes, Deadline& deadline, Completion& done) noexcept;
  [[nodiscard]] uint32_t finishStreams(uint8_t instance, bool woke) noexcept;
  [[nodiscard]] uint32_t finishDone(uint8_t instance) noexcept;
  [[nodiscard]] uint32_t finishStopped(uint8_t instance) noexcept;
  void count(uint32_t SpiDmaStats::* field) noexcept;

  SpiDmaConfig config_;
  Hooks hooks_;
  std::size_t stage_bytes_;
  std::size_t align_bytes_;
  DmaStream rx_;
  DmaStream tx_;
  SpiDmaStats stats_{};
};

/**
 * @brief The DMA path of one SPI bus over two staging buffers of `Bytes` in region `R` (one pair
 * per bus, used only under the bus lock). The board defines the buffers in the region's section.
 */
template <PlacedRegion R, std::size_t Bytes>
class SpiDmaLink : public SpiDmaPath {
public:
  /// A staging buffer.
  using Stage = kb::dma::Buffer<R, Bytes>;
  /// The largest DMA frame.
  static constexpr std::size_t kStageBytes = Bytes;
  /// The alignment unit.
  static constexpr std::size_t kAlign = R::kAlignBytes;

  /** @brief A link over `tx` and `rx` (static storage, the board's region) with `config`. */
  SpiDmaLink(Stage& tx, Stage& rx, const SpiDmaConfig& config) noexcept
      : SpiDmaPath(config, Bytes, R::kAlignBytes, kHooks), tx_stage_(tx), rx_stage_(rx) {}
  SpiDmaLink(const SpiDmaLink&) = delete;
  SpiDmaLink& operator=(const SpiDmaLink&) = delete;
  SpiDmaLink(SpiDmaLink&&) = delete;
  SpiDmaLink& operator=(SpiDmaLink&&) = delete;
  ~SpiDmaLink() = default;

  /** @brief The transmit staging buffer. */
  [[nodiscard]] Stage& TxStage() noexcept {
    return tx_stage_;
  }
  /** @brief The receive staging buffer. */
  [[nodiscard]] Stage& RxStage() noexcept {
    return rx_stage_;
  }

private:
  [[nodiscard]] static SpiDmaLink& self(SpiDmaPath& path) noexcept {
    return static_cast<SpiDmaLink&>(path);
  }
  [[nodiscard]] kb::dma::PortKey<SpiDmaLink> key() const noexcept {
    return kb::dma::PortKey<SpiDmaLink>{};
  }

  static uint32_t place(SpiDmaPath& path) noexcept {
    SpiDmaLink& me = self(path);
    uint32_t code = DmaPlacementCheck(me.tx_stage_);
    if (code == hal::kOk) {
      code = DmaPlacementCheck(me.rx_stage_);
    }
    return code;
  }

  static uint32_t lend(SpiDmaPath& path, std::span<const uint8_t> tx, std::size_t bytes,
                       std::span<std::byte>& tx_memory, std::span<std::byte>& rx_memory) noexcept {
    SpiDmaLink& me = self(path);
    uint32_t code = port_code::kQuarantined;
    const std::span<std::byte> view = me.tx_stage_.CpuView();
    if (view.size() >= bytes) {
      // The caller's bytes (or zeros for a read-only frame) by CPU copy: no cache maintenance here.
      for (std::size_t i = 0U; i < bytes; ++i) {
        view[i] = tx.empty() ? std::byte{0U} : static_cast<std::byte>(tx[i]);
      }
      const kb::Status t =
          me.tx_stage_.Lend(me.key(), kb::dma::Direction::ToDevice, bytes, me.ops_, me.tx_lease_);
      const kb::Status r = (t == kb::Status::Ok)
                               ? me.rx_stage_.Lend(me.key(), kb::dma::Direction::FromDevice, bytes,
                                                   me.ops_, me.rx_lease_)
                               : t;
      if ((t == kb::Status::Ok) && (r != kb::Status::Ok)) {
        static_cast<void>(me.tx_lease_.Abort(me.key(), me.ops_, kb::dma::StopEvidence::Confirmed));
      }
      if (r == kb::Status::Ok) {
        tx_memory = me.tx_lease_.DeviceView(me.key());
        rx_memory = me.rx_lease_.DeviceView(me.key());
        code = hal::kOk;
      }
    }
    return code;
  }

  static void settleOne(SpiDmaLink& me, kb::dma::Lease<R>& lease, DmaEnd end) noexcept {
    if (end == DmaEnd::Completed) {
      static_cast<void>(lease.Complete(me.key(), me.ops_, kb::dma::Outcome::Ok));
    } else if (end == DmaEnd::Failed) {
      static_cast<void>(lease.Complete(me.key(), me.ops_, kb::dma::Outcome::Error));
    } else if (end == DmaEnd::Aborted) {
      static_cast<void>(lease.Abort(me.key(), me.ops_, kb::dma::StopEvidence::Confirmed));
    } else if (end == DmaEnd::Unconfirmed) {
      static_cast<void>(lease.Abort(me.key(), me.ops_, kb::dma::StopEvidence::NotConfirmed));
    } else if (lease.Active()) {
      // Lent but never started (a start refused): the DMA never owned it.
      static_cast<void>(lease.Abort(me.key(), me.ops_, kb::dma::StopEvidence::Confirmed));
    } else {
      // Nothing lent.
    }
  }

  static void settle(SpiDmaPath& path, std::span<uint8_t> rx, bool copy_out) noexcept {
    SpiDmaLink& me = self(path);
    settleOne(me, me.tx_lease_, me.Tx().TakeEnd());
    settleOne(me, me.rx_lease_, me.Rx().TakeEnd());
    const std::span<const std::byte> view = std::as_const(me.rx_stage_).CpuView();
    if (copy_out && (view.size() >= rx.size())) {
      for (std::size_t i = 0U; i < rx.size(); ++i) {
        rx[i] = static_cast<uint8_t>(view[i]);
      }
    }
  }

  static kb::Status recover(SpiDmaPath& path) noexcept {
    SpiDmaLink& me = self(path);
    const kb::Status t = me.tx_stage_.Recover(me.key(), me.ops_, kb::dma::StopEvidence::Confirmed);
    const kb::Status r = me.rx_stage_.Recover(me.key(), me.ops_, kb::dma::StopEvidence::Confirmed);
    // InvalidArgument: that buffer was the CPU's already.
    const bool ok = ((t == kb::Status::Ok) || (t == kb::Status::InvalidArgument)) &&
                    ((r == kb::Status::Ok) || (r == kb::Status::InvalidArgument));
    return ok ? kb::Status::Ok : kb::Status::Fault;
  }

  static constexpr Hooks kHooks{.place = &SpiDmaLink::place,
                                .lend = &SpiDmaLink::lend,
                                .settle = &SpiDmaLink::settle,
                                .recover = &SpiDmaLink::recover};

  Stage& tx_stage_;
  Stage& rx_stage_;
  PortCacheOps ops_{};
  kb::dma::Lease<R> tx_lease_{};
  kb::dma::Lease<R> rx_lease_{};
};

/** @brief The compile-time DMA limits of a bus served by link type `Link` (kb::dma::BusLimits). */
template <typename Link>
struct SpiLimitsOf {
  static constexpr kb::dma::BusLimits kBus{.dma = true,
                                           .max_frame_dma = Link::kStageBytes,
                                           .max_frame_no_dma = kSpiNoDmaMaxFrame,
                                           .rx_length_multiple = 1U,
                                           .align_bytes = Link::kAlign};
};

/** @brief The limits of a bus without DMA. */
struct SpiLimitsNoDma {
  static constexpr kb::dma::BusLimits kBus{.dma = false,
                                           .max_frame_dma = 0U,
                                           .max_frame_no_dma = kSpiNoDmaMaxFrame,
                                           .rx_length_multiple = 1U,
                                           .align_bytes = 1U};
};

} // namespace kb::port::stm32

#endif // KB_PORT_STM32_SPI_DMA_HPP

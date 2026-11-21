/**
 * @file dma.hpp
 * @brief The STM32 DMA layer of the port (docs/dma.md): the masters and memory regions of each
 * family as `kb::dma` region tags, the checks a board's stream assignment must pass, and
 * `kb::port::stm32::DmaStream`, one DMA1/DMA2 stream (F4, H7) or channel (G4, L4) driven by
 * registers with a confirmed stop, plus `DmaTransfer<Region>`, one buffer transfer on a stream with
 * the lease of a `kb::dma::Buffer`.
 *
 * **Assignment.** A stream is never searched for at run time: the board generator resolves
 * `board.yml` into a `kb::dma::StreamAssignment` (engine 1 = DMA1, 2 = DMA2; the stream 0 to 7 on
 * F4 and H7, the channel 1 to 8 on G4 and L4; the request is the DMAMUX request line on G4, H7 and
 * L4+, the channel selection on F4, the `CSELR` value on L4) and the port checks it at compile time
 * (`RequireStream`) and at `EnsureReady()`. `EnsureReady()` writes the stream's NVIC priority from
 * the assignment and reads it back (a vendor code generator that rewrote it is refused).
 *
 * **Interrupt.** Every entry of a stream's interrupt services every flag of the stream, errors
 * first, each cleared by its own clear bit and counted, whether or not a transfer is in flight, so
 * a flag can never re-assert the interrupt for ever. Completion is claimed exactly once through a
 * `kb::dma::CompletionLatch`.
 *
 * **Stop.** A stop clears EN and waits, bounded in core cycles, for EN to read 0 (the stream may
 * finish the data item in flight after EN is cleared). Only then does the memory go back to the
 * CPU; a stop that is not confirmed quarantines the buffer and the stream is `Faulted` until
 * `Recover()`.
 *
 * | Result | Meaning |
 * | --- | --- |
 * | `Ok` | Started, finished, or stopped with proof |
 * | `InvalidArgument` | An assignment the family does not have, a priority outside 0 to 15 or above
 * the RTOS limit for `Notify::Task` | | `NotReady` | Not started; or the NVIC priority does not
 * read back (`port_code::kPriority`) | | `Busy` | The stream is enabled by someone else, or a
 * transfer is in flight | | `OutOfRange` | A transfer larger than the stream's limit | |
 * `Unsupported` | No stream was assigned (the declared fallback serves) | | `Fault` | A transfer
 * error (detail: the stream flags), or a stop that could not be confirmed | | `Timeout` | The
 * deadline passed; the transfer was stopped |
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 The port-stm32 authors
 */
#ifndef KB_PORT_STM32_DMA_HPP
#define KB_PORT_STM32_DMA_HPP

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

#include "kb/dma/assignment.hpp"
#include "kb/dma/buffer.hpp"
#include "kb/dma/cache.hpp"
#include "kb/dma/capabilities.hpp"
#include "kb/dma/completion.hpp"
#include "kb/dma/region.hpp"
#include "kb/port/stm32/completion.hpp"
#include "kb/port/stm32/error.hpp"
#include "kb/utils/result.hpp"
#include "kb/utils/status.hpp"
#include "kb/utils/time.hpp"

namespace kb::port::stm32 {

/** @name Bus masters (kb::dma::MasterSet bits)
 * @{ */
inline constexpr kb::dma::MasterSet kMasterDma1 = kb::dma::MasterBit(0U); ///< DMA1.
inline constexpr kb::dma::MasterSet kMasterDma2 = kb::dma::MasterBit(1U); ///< DMA2.
inline constexpr kb::dma::MasterSet kMasterBdma = kb::dma::MasterBit(2U); ///< H7 BDMA (D3).
inline constexpr kb::dma::MasterSet kMasterMdma = kb::dma::MasterBit(3U); ///< H7 MDMA (D1).
/** @} */

/** @name Engine numbers of a kb::dma::StreamAssignment
 * @{ */
inline constexpr uint8_t kEngineNone = 0U; ///< No stream: the declared fallback serves.
inline constexpr uint8_t kEngineDma1 = 1U; ///< DMA1.
inline constexpr uint8_t kEngineDma2 = 2U; ///< DMA2.
/** @} */

/** @brief The master of engine `engine` (0 for an engine the port does not drive). */
[[nodiscard]] constexpr kb::dma::MasterSet MasterOfEngine(uint8_t engine) noexcept {
  kb::dma::MasterSet master = 0U;
  if (engine == kEngineDma1) {
    master = kMasterDma1;
  } else if (engine == kEngineDma2) {
    master = kMasterDma2;
  } else {
    master = 0U;
  }
  return master;
}

/**
 * @brief A region tag that also says where the region is (`kBase`, `kSize`), so `EnsureReady()` can
 * check that a buffer was placed inside it (a buffer placed by the default linker rules is
 * refused).
 */
template <typename R>
concept PlacedRegion = kb::dma::Region<R> && requires {
  { R::kBase } -> std::convertible_to<std::uintptr_t>;
  { R::kSize } -> std::convertible_to<std::size_t>;
};

/**
 * @name Region tags
 * The memories of each family as the port knows them. A board narrows a region to the window its
 * linker script and MPU set up by defining its own tag with the same members (the board generator
 * writes them from `board.yml`).
 * @{ */
namespace h7 {
/** @brief D2 SRAM1 to SRAM3 behind an MPU region marked non-cacheable: the default DMA region. */
struct D2Sram {
  static constexpr kb::dma::CachePolicy kPolicy = kb::dma::CachePolicy::NonCacheable;
  static constexpr std::size_t kLineBytes = 1U;
  static constexpr std::size_t kAlignBytes = 32U;
  static constexpr kb::dma::MasterSet kMasters = kMasterDma1 | kMasterDma2 | kMasterMdma;
  static constexpr std::uintptr_t kBase = 0x30000000U;
  static constexpr std::size_t kSize = 0x48000U; ///< SRAM1 + SRAM2 + SRAM3 of the H743.
};
/** @brief AXI SRAM (D1), cacheable write-back: the port cleans and invalidates by 32-byte line. */
struct AxiSram {
  static constexpr kb::dma::CachePolicy kPolicy = kb::dma::CachePolicy::Maintained;
  static constexpr std::size_t kLineBytes = 32U;
  static constexpr std::size_t kAlignBytes = 32U;
  static constexpr kb::dma::MasterSet kMasters = kMasterDma1 | kMasterDma2 | kMasterMdma;
  static constexpr std::uintptr_t kBase = 0x24000000U;
  static constexpr std::size_t kSize = 0x80000U;
};
/** @brief D3 SRAM4 behind a non-cacheable MPU region: the only memory the BDMA reaches. */
struct Sram4 {
  static constexpr kb::dma::CachePolicy kPolicy = kb::dma::CachePolicy::NonCacheable;
  static constexpr std::size_t kLineBytes = 1U;
  static constexpr std::size_t kAlignBytes = 32U;
  static constexpr kb::dma::MasterSet kMasters =
      kMasterBdma | kMasterDma1 | kMasterDma2 | kMasterMdma;
  static constexpr std::uintptr_t kBase = 0x38000000U;
  static constexpr std::size_t kSize = 0x10000U;
};
/** @brief DTCM: CPU only (and MDMA); declared so that a DMA1/DMA2 buffer in it fails to build. */
struct Dtcm {
  static constexpr kb::dma::CachePolicy kPolicy = kb::dma::CachePolicy::Coherent;
  static constexpr std::size_t kLineBytes = 1U;
  static constexpr std::size_t kAlignBytes = 4U;
  static constexpr kb::dma::MasterSet kMasters = kMasterMdma;
  static constexpr std::uintptr_t kBase = 0x20000000U;
  static constexpr std::size_t kSize = 0x20000U;
};
} // namespace h7

/** @brief The main SRAM of the cacheless families (F4, G4, L4); the board narrows the size. */
struct Sram {
  static constexpr kb::dma::CachePolicy kPolicy = kb::dma::CachePolicy::Coherent;
  static constexpr std::size_t kLineBytes = 1U;
  static constexpr std::size_t kAlignBytes = 4U;
  static constexpr kb::dma::MasterSet kMasters = kMasterDma1 | kMasterDma2;
  static constexpr std::uintptr_t kBase = 0x20000000U;
  static constexpr std::size_t kSize = 0x80000U;
};
/** @} */

/** @brief The interrupt limits of the build's RTOS (`kMinRtosIrqPriority`, 4 priority bits). */
struct PortInterruptLimits {
  static constexpr kb::dma::InterruptLimits kLimits{.max_syscall_priority = kMinRtosIrqPriority,
                                                    .priority_bits = 4U};
};

/** @name Compile-time checks of a stream tag `S` (with `kAssignment`)
 * @{ */
/** @brief The engine is DMA1 or DMA2 and the interrupt tier suits the RTOS. */
template <typename S>
consteval void RequireStream() {
  static_assert((S::kAssignment.engine == kEngineDma1) || (S::kAssignment.engine == kEngineDma2),
                "kb::port::stm32: the stream's engine is not DMA1 or DMA2");
  kb::dma::RequireInterruptTier<S, PortInterruptLimits>();
}
/** @brief The stream's engine reaches region `R` (a DTCM buffer for DMA1 fails here). */
template <typename S, kb::dma::Region R>
consteval void RequireStreamReaches() {
  kb::dma::RequireReachable<MasterOfEngine(S::kAssignment.engine), R>();
}
/** @} */

/** @name The stream flags, the same bits on every family
 * @{ */
inline constexpr uint32_t kDmaFlagComplete = 1U; ///< TCIF: the transfer (or a pass) ended.
inline constexpr uint32_t kDmaFlagHalf = 2U;     ///< HTIF: half of the transfer.
inline constexpr uint32_t kDmaFlagTransferError =
    4U; ///< TEIF: bus error; the stream disabled itself.
inline constexpr uint32_t kDmaFlagDirectError = 8U; ///< DMEIF (F4, H7): direct-mode error.
inline constexpr uint32_t kDmaFlagFifoError = 16U;  ///< FEIF (F4, H7): FIFO error.
inline constexpr uint32_t kDmaFlagsAll = 31U;       ///< Every flag above.
/** @} */

/** @brief The width of one data item on the peripheral side (the memory side uses the same). */
enum class DmaWidth : uint8_t {
  Byte = 0,     ///< 8 bits (SPI frames).
  HalfWord = 1, ///< 16 bits (ADC data).
  Word = 2,     ///< 32 bits.
};

/** @brief The bytes of one item of `width`. */
[[nodiscard]] constexpr std::size_t DmaItemBytes(DmaWidth width) noexcept {
  std::size_t bytes = 1U;
  if (width == DmaWidth::HalfWord) {
    bytes = 2U;
  } else if (width == DmaWidth::Word) {
    bytes = 4U;
  } else {
    bytes = 1U;
  }
  return bytes;
}

/** @brief The peripheral end of a stream: its data register and the item width. */
struct DmaPeripheral {
  std::uintptr_t address{0U};     ///< The data register (SPI DR/TXDR/RXDR, ADC DR).
  DmaWidth width{DmaWidth::Byte}; ///< The item width.
  kb::dma::Direction dir{kb::dma::Direction::FromDevice}; ///< The stream's direction.
};

/** @brief What a stream may carry. */
struct DmaStreamLimits {
  std::size_t max_items{65535U}; ///< The largest transfer in items (NDTR is 16 bits).
  std::size_t fallback_max_bytes{
      0U}; ///< The ceiling of the declared fallback (no stream assigned).
};

/** @brief Counters of one stream. */
struct DmaStats {
  uint32_t starts{0U};          ///< Transfers started.
  uint32_t completions{0U};     ///< Transfers that ran to their end.
  uint32_t transfer_errors{0U}; ///< TEIF seen.
  uint32_t direct_errors{0U};   ///< DMEIF seen.
  uint32_t fifo_errors{0U};     ///< FEIF seen (counted, not fatal: direct mode).
  uint32_t halves{0U};          ///< HTIF seen.
  uint32_t unexpected{0U};      ///< Interrupt entries that found flags with nothing in flight.
  uint32_t timeouts{0U};        ///< Transfers stopped at their deadline.
  uint32_t cancels{0U};         ///< Transfers cancelled.
  uint32_t quarantines{0U};     ///< Stops that could not be confirmed.
  uint32_t recoveries{0U};      ///< Recover() calls that returned the stream to service.
  uint32_t last_flags{0U};      ///< The flags of the last interrupt entry that found any.
  uint32_t max_stop_cycles{0U}; ///< The longest confirmed stop, in core cycles.
  uint32_t max_isr_cycles{0U};  ///< The longest interrupt entry, in core cycles.
};

/** @brief What `Poll()` found. */
enum class DmaPollState : uint8_t {
  Idle = 0,    ///< Nothing in flight, nothing to report.
  Pending = 1, ///< In flight.
  Done = 2,    ///< Finished; reported once.
};

/** @brief One poll result. */
struct DmaPoll {
  DmaPollState state{DmaPollState::Idle};
  kb::Status status{kb::Status::Ok}; ///< Valid when `Done`.
  std::size_t count{0U};             ///< Bytes moved (valid when `Done`).
  uint32_t detail{0U};               ///< The stream flags of an error.
};

/** @brief How the hardware left the memory of the last transfer (for the lease). */
enum class DmaEnd : uint8_t {
  None = 0,        ///< Nothing to settle.
  Completed = 1,   ///< Ran to its end: complete the lease with Outcome::Ok.
  Failed = 2,      ///< A transfer error stopped it (EN read 0): complete with Outcome::Error.
  Aborted = 3,     ///< Stopped with proof (EN read 0): abort the lease, confirmed.
  Unconfirmed = 4, ///< EN did not read 0 in its bound: abort unconfirmed (quarantine).
};

/**
 * @brief The interrupt target of a stream whose owner drains it itself (a circular ADC stream): the
 * owner derives from it and registers it with `DmaStream::SetIrqHook`; HandleDmaIrq then calls
 * `serve(*this)` instead of `DmaStream::OnIrq()`.
 */
struct DmaIrqHook {
  void (*serve)(DmaIrqHook& self) noexcept {nullptr};
};

/** @brief The cache hooks of the port (kb::dma::CacheOps): by address on the H7 Cortex-M7 D-cache,
 * barriers only on the cacheless families. */
class PortCacheOps {
public:
  /** @brief Writes back every dirty line of `range`. */
  static void Clean(std::span<const std::byte> range) noexcept;
  /** @brief Discards every line of `range`. */
  static void Invalidate(std::span<std::byte> range) noexcept;
  /** @brief Data synchronisation barrier. */
  static void Barrier() noexcept;
};
static_assert(kb::dma::CacheOps<PortCacheOps>, "PortCacheOps satisfies kb::dma::CacheOps");

/**
 * @brief Start-time placement check of a buffer (`Verify` against the region's window) and of the
 * region's cache attributes: a `NonCacheable` region needs an MPU region that makes the whole
 * window uncached when the D-cache is on. `port_code::kPlacement` when either fails.
 */
[[nodiscard]] uint32_t DmaRegionCheck(kb::dma::CachePolicy policy, std::uintptr_t base,
                                      std::size_t size) noexcept;

/** @brief The placement and region check of `buffer` in region `R` (see DmaRegionCheck). */
template <PlacedRegion R, std::size_t Bytes>
[[nodiscard]] uint32_t DmaPlacementCheck(const kb::dma::Buffer<R, Bytes>& buffer) noexcept {
  uint32_t code =
      (buffer.Verify(R::kBase, R::kSize) == kb::Status::Ok) ? hal::kOk : port_code::kPlacement;
  if (code == hal::kOk) {
    code = DmaRegionCheck(R::kPolicy, R::kBase, R::kSize);
  }
  return code;
}

/**
 * @brief One DMA stream (F4, H7) or channel (G4, L4): routing, priority, start, the interrupt, the
 * confirmed stop. Platform-free (src/dma.cpp); the registers are behind src/boundary/dma.cpp.
 *
 * A default-constructed stream has no assignment: it reports the fallback and refuses transfers.
 */
class DmaStream {
public:
  /** @brief No stream assigned. */
  constexpr DmaStream() noexcept = default;
  /** @brief Stream `assignment` serving `peripheral`. */
  DmaStream(const kb::dma::StreamAssignment& assignment, const DmaPeripheral& peripheral,
            const DmaStreamLimits& limits = DmaStreamLimits{}) noexcept
      : assignment_(assignment), peripheral_(peripheral), limits_(limits) {}
  DmaStream(const DmaStream&) = delete;
  DmaStream& operator=(const DmaStream&) = delete;
  DmaStream(DmaStream&&) = delete;
  DmaStream& operator=(DmaStream&&) = delete;
  /** @brief Takes the stream out of the dispatch table. */
  ~DmaStream() noexcept;

  /** @brief Sets the assignment and the peripheral end before EnsureReady() (an owner that learns
   * the data register at its own start); InvalidArgument once ready. */
  [[nodiscard]] kb::Status Bind(const kb::dma::StreamAssignment& assignment,
                                const DmaPeripheral& peripheral,
                                const DmaStreamLimits& limits = DmaStreamLimits{}) noexcept;
  /** @brief The Completion the interrupt completes when a transfer ends (`errors_only`: only when
   * an error ends it), with the vendor code of the end. */
  void SetNotify(Completion* done, bool errors_only) noexcept;

  /**
   * @brief Checks the assignment, refuses a stream already enabled, routes the request, writes and
   * reads back the NVIC priority and enables the interrupt; idempotent.
   */
  [[nodiscard]] kb::Status EnsureReady() noexcept;
  /** @brief Stops the stream (confirmed), disables its interrupt and leaves the dispatch table. */
  [[nodiscard]] kb::Status Release() noexcept;
  /** @brief True when the board assigned a stream. */
  [[nodiscard]] bool Assigned() const noexcept {
    return assignment_.engine != kEngineNone;
  }
  /** @brief True after EnsureReady() succeeded. */
  [[nodiscard]] bool IsReady() const noexcept {
    return ready_;
  }
  /** @brief The assignment. */
  [[nodiscard]] const kb::dma::StreamAssignment& Assignment() const noexcept {
    return assignment_;
  }
  /** @brief The peripheral end. */
  [[nodiscard]] const DmaPeripheral& Peripheral() const noexcept {
    return peripheral_;
  }
  /** @brief The largest transfer in bytes (the fallback's ceiling when no stream is assigned). */
  [[nodiscard]] std::size_t MaxBytes() const noexcept;

  /**
   * @brief Starts a transfer of `bytes` over `memory` (normal mode) or a circular pass over it
   * (`ChannelMode::Circular`). The caller has lent the memory. `deadline` ends a normal transfer.
   * @return Ok; Unsupported (no stream); NotReady; Busy (in flight); OutOfRange; Fault (faulted).
   */
  [[nodiscard]] kb::Status Begin(std::span<std::byte> memory, std::size_t bytes,
                                 kb::utils::Deadline deadline) noexcept;
  /** @brief Reports a finished transfer once, stops an overdue one (`Timeout`). Task context. */
  [[nodiscard]] DmaPoll Poll(kb::Instant now) noexcept;
  /** @brief Stops the transfer in flight: Ok once the stream is proven stopped, Fault otherwise. */
  [[nodiscard]] kb::Status Cancel() noexcept;
  /**
   * @brief For a stream whose completion interrupt is off (`IrqUse::ErrorsOnly`): after its partner
   * finished, completes it when the hardware shows the transfer done (EN 0, nothing left, no
   * error), else stops it. Ok when it completed.
   */
  [[nodiscard]] kb::Status Conclude() noexcept;
  /** @brief How the hardware left the memory of the transfer last reported; then None. */
  [[nodiscard]] DmaEnd TakeEnd() noexcept;
  /** @brief Re-initialises the stream (EN off, flags cleared one by one) and proves it stopped. */
  [[nodiscard]] kb::Status Recover() noexcept;
  /** @brief Stops a circular pass: EN cleared, bounded wait for EN 0. */
  [[nodiscard]] kb::dma::StopEvidence Stop() noexcept;

  /** @brief The interrupt entry: services every flag (errors first), each cleared on its own. */
  void OnIrq() noexcept;
  /** @brief The flags the last interrupt entry serviced (for a circular owner's drain). */
  [[nodiscard]] uint32_t LastServiced() const noexcept {
    return serviced_;
  }
  /** @brief Items left in the transfer (the transfer counter). */
  [[nodiscard]] uint32_t Remaining() const noexcept;
  /** @brief Installs `hook` as the stream's interrupt target instead of OnIrq() (circular owners).
   */
  void SetIrqHook(DmaIrqHook* hook) noexcept;

  /** @brief What the stream offers. */
  [[nodiscard]] kb::dma::Capabilities Capabilities() const noexcept;
  /** @brief The DMA part of a health record. */
  [[nodiscard]] kb::dma::DmaHealth Health() const noexcept;
  /** @brief The counters (a consistent copy). */
  [[nodiscard]] DmaStats Stats() const noexcept;
  /** @brief True after a stop could not be confirmed, until Recover(). */
  [[nodiscard]] bool Faulted() const noexcept {
    return faulted_;
  }
  /** @brief The last call's status and its code. */
  [[nodiscard]] kb::Result<void> LastError() const noexcept {
    return error_.Get();
  }

  /** @brief The dispatch of HandleDmaIrq: the hook when one is installed, else OnIrq(). */
  void Dispatch() noexcept;

private:
  [[nodiscard]] uint32_t checkAssignment() const noexcept;
  [[nodiscard]] uint32_t irqMask() const noexcept;
  [[nodiscard]] kb::dma::StopEvidence stop() noexcept;
  void clearAll() const noexcept;
  [[nodiscard]] kb::Status stopClaimed(bool timeout) noexcept;
  [[nodiscard]] uint32_t serviceFlags() noexcept;
  void endTransfer(uint32_t serviced) noexcept;
  void record(uint32_t flags) noexcept;

  kb::dma::StreamAssignment assignment_{};
  DmaPeripheral peripheral_{};
  DmaStreamLimits limits_{};
  kb::dma::CompletionLatch latch_{};
  kb::utils::Deadline deadline_;
  DmaPoll result_{};
  DmaStats stats_{};
  LastErrorRecord error_;
  DmaIrqHook* irq_hook_{nullptr};
  Completion* notify_{nullptr};
  bool notify_errors_only_{false};
  std::size_t bytes_{0U};
  uint32_t serviced_{0U};
  DmaEnd end_{DmaEnd::None};
  DmaEnd pending_end_{DmaEnd::None};
  bool ready_{false};
  bool faulted_{false};
};

/**
 * @brief One buffer transfer on a DmaStream with the lease of a `kb::dma::Buffer` in region `R`:
 * the lend (with the region's maintenance), the start, the report, the cancel and the recovery.
 * The memory returns to the CPU only through a completion or a confirmed stop.
 */
template <PlacedRegion R>
class DmaTransfer {
public:
  /** @brief A transfer over `stream`, which must outlive it. */
  explicit DmaTransfer(DmaStream& stream) noexcept : stream_(stream) {}
  DmaTransfer(const DmaTransfer&) = delete;
  DmaTransfer& operator=(const DmaTransfer&) = delete;
  DmaTransfer(DmaTransfer&&) = delete;
  DmaTransfer& operator=(DmaTransfer&&) = delete;
  /** @brief An active lease quarantines its buffer (kb::dma::Lease). */
  ~DmaTransfer() = default;

  /** @brief The key the port gives the buffers it lends. */
  [[nodiscard]] kb::dma::PortKey<DmaTransfer> Key() const noexcept {
    return kb::dma::PortKey<DmaTransfer>{};
  }

  /**
   * @brief Lends `used` bytes of `buffer` in `dir` and starts the stream.
   * @return Ok; OutOfRange (above the stream's limit); Unsupported (no stream); Busy; Fault
   * (quarantined or faulted); the Lend failures.
   */
  template <std::size_t Bytes>
  [[nodiscard]] kb::Status Start(kb::dma::Buffer<R, Bytes>& buffer, kb::dma::Direction dir,
                                 std::size_t used, kb::utils::Deadline deadline) noexcept {
    kb::Status status = kb::Status::Ok;
    if (used > stream_.MaxBytes()) {
      status = kb::Status::OutOfRange;
    } else if (!stream_.Assigned()) {
      status = kb::Status::Unsupported;
    } else if (dir != stream_.Peripheral().dir) {
      status = kb::Status::InvalidArgument;
    } else {
      status = buffer.Lend(Key(), dir, used, ops_, lease_);
      if (status == kb::Status::Ok) {
        status = stream_.Begin(lease_.DeviceView(Key()), used, deadline);
        if (status != kb::Status::Ok) {
          static_cast<void>(lease_.Abort(Key(), ops_, kb::dma::StopEvidence::Confirmed));
        }
      }
    }
    return status;
  }

  /** @brief The interrupt's work is done by the stream; the report and the lease settle here. */
  [[nodiscard]] DmaPoll Poll(kb::Instant now) noexcept {
    const DmaPoll poll = stream_.Poll(now);
    settle();
    return poll;
  }

  /** @brief Ok once the memory is the CPU's again; Fault (quarantined) when the stop failed. */
  [[nodiscard]] kb::Status Cancel() noexcept {
    const kb::Status status = stream_.Cancel();
    settle();
    return status;
  }

  /** @brief Re-initialises the stream and releases the quarantined `buffer` on proof. */
  template <std::size_t Bytes>
  [[nodiscard]] kb::Status Recover(kb::dma::Buffer<R, Bytes>& buffer) noexcept {
    kb::Status status = stream_.Recover();
    if (status == kb::Status::Ok) {
      status = buffer.Recover(Key(), ops_, kb::dma::StopEvidence::Confirmed);
      if (status == kb::Status::InvalidArgument) {
        status = kb::Status::Ok; // the CPU already owned it
      }
    }
    return status;
  }

  /** @brief What the stream offers (`zero_copy`: this transfer lends typed buffers). */
  [[nodiscard]] kb::dma::Capabilities Capabilities() const noexcept {
    kb::dma::Capabilities c = stream_.Capabilities();
    c.zero_copy = stream_.Assigned();
    c.align_bytes = R::kAlignBytes;
    return c;
  }
  /** @brief The DMA part of a health record. */
  [[nodiscard]] kb::dma::DmaHealth Health() const noexcept {
    return stream_.Health();
  }
  /** @brief The stream's counters. */
  [[nodiscard]] DmaStats Stats() const noexcept {
    return stream_.Stats();
  }
  /** @brief The stream. */
  [[nodiscard]] DmaStream& Stream() noexcept {
    return stream_;
  }

private:
  void settle() noexcept {
    const DmaEnd end = stream_.TakeEnd();
    if (end == DmaEnd::Completed) {
      static_cast<void>(lease_.Complete(Key(), ops_, kb::dma::Outcome::Ok));
    } else if (end == DmaEnd::Failed) {
      static_cast<void>(lease_.Complete(Key(), ops_, kb::dma::Outcome::Error));
    } else if (end == DmaEnd::Aborted) {
      static_cast<void>(lease_.Abort(Key(), ops_, kb::dma::StopEvidence::Confirmed));
    } else if (end == DmaEnd::Unconfirmed) {
      static_cast<void>(lease_.Abort(Key(), ops_, kb::dma::StopEvidence::NotConfirmed));
    } else {
      // Nothing ended.
    }
  }

  DmaStream& stream_;
  PortCacheOps ops_{};
  kb::dma::Lease<R> lease_{};
};

} // namespace kb::port::stm32

#endif // KB_PORT_STM32_DMA_HPP

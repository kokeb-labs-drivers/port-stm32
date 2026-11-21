/**
 * @file adc_stream.hpp
 * @brief `kb::port::stm32::AdcDmaStream<Frame, Region>`: a timer-triggered ADC scan streamed by
 * circular DMA into a `kb::dma::Stream<Frame, 2, Region>` (docs/dma.md, docs/peripherals/adc.md).
 *
 * **Hardware.** One ADC instance scans its ranks (`channels`, in order) at each edge of the
 * board's trigger timer; the DMA stream runs circular over a two-frame ring (`Frames = 2`: the
 * half- and full-transfer interrupts each end one frame). The ADC runs with its overrun mode
 * data-preserved (OVRMOD 0: an overrun stops the DMA requests visibly instead of overwriting) and
 * its own interrupts off; the overrun flag is read and counted in the drain, never by an interrupt.
 *
 * **One drainer.** The stream's interrupt is the only code that reads and clears the DMA flags: it
 * services every flag (errors first, each by its own bit), copies each finished half out into the
 * stream (a frame whose half the DMA lapped is published `Torn`), counts an overrun with
 * `OnOverrun` (never a frame), and stamps each frame with the configured clock. The owner task
 * never touches the flags: `Service()` takes at most `frames_per_pass` frames, publishes the latest
 * scan through a seqlock (`Latest()`, for any other task), and runs the stream's supervision
 * (signed stall time, restart, back-off, fallback).
 *
 * **Start.** `Start()` runs on the owner task after the scheduler (it refuses while the HAL time
 * base does not run) and is idempotent (a second start of a running stream is `Ok`). It refuses
 * (`NotReady`) when the ADC kernel clock reads 0 (the port never writes a shared kernel-clock
 * selection). It offers DMA only when the board assigned a stream and the conversion count the
 * peripheral reads back equals the stream's frame elements (`StreamShape.elements`); otherwise it
 * runs the one-shot fallback (each `Service()` converts every rank once by interrupt), reported in
 * `Mode()`, `Capabilities()`, `Health()` and on every frame (`Fallback()`).
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 The port-stm32 authors
 */
#ifndef KB_PORT_STM32_ADC_STREAM_HPP
#define KB_PORT_STM32_ADC_STREAM_HPP

#include <algorithm>
#include <array>
// The seqlock's words are shared between tasks: std::atomic (as kb-core's Seqlock).
// misra-lint: ignore KB-CPP-012
#include <atomic>
#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>

#include "kb/dma/assignment.hpp"
#include "kb/dma/buffer.hpp"
#include "kb/dma/capabilities.hpp"
#include "kb/dma/stream.hpp"
#include "kb/port/stm32/adc.hpp"
#include "kb/port/stm32/completion.hpp"
#include "kb/port/stm32/dma.hpp"
#include "kb/port/stm32/error.hpp"
#include "kb/utils/result.hpp"
#include "kb/utils/status.hpp"
#include "kb/utils/time.hpp"

namespace kb::port::stm32 {

/** @brief The most ranks of one scan (the regular sequence of every family holds 16). */
inline constexpr std::size_t kAdcMaxRanks = 16U;
/** @brief The flag `OnOverrun` reports for the ADC's own overrun (beside the kDmaFlag* bits). */
inline constexpr uint32_t kAdcFlagOverrun = 0x100U;

/** @brief One scan: one 16-bit sample per rank, in rank order. */
template <std::size_t Ranks>
struct AdcScan {
  std::array<uint16_t, Ranks> v{};
};

/** @brief A frame an ADC stream can fill: trivially copyable, its member `v` an array of samples.
 */
template <typename F>
concept AdcFrame = kb::dma::StreamFrame<F> && requires(const F& f) {
  { f.v.size() } -> std::convertible_to<std::size_t>;
  requires std::same_as<std::remove_cvref_t<decltype(f.v[0])>, uint16_t>;
} && (sizeof(F) == (F{}.v.size() * sizeof(uint16_t)));

/** @brief How a scan stream is wired and supervised. */
struct AdcStreamConfig {
  uint8_t instance{1U};                      ///< ADC1 is 1 (the instance is the stream's alone).
  std::span<const uint8_t> channels;         ///< The channel of each rank, in order.
  AdcSampling sampling{AdcSampling::Medium}; ///< Sampling time of every rank.
  uint32_t trigger{0U};            ///< The family's ADC_EXTERNALTRIG_* value (the board's timer).
  kb::dma::StreamAssignment dma{}; ///< The stream (engine 0: none, the one-shot fallback serves).
  kb::dma::StreamShape shape{};    ///< The board's declaration of the stream.
  kb::dma::StreamConfig supervision{};     ///< Stall budget, restart and fallback policy.
  kb::Instant (*now)() noexcept {nullptr}; ///< The clock frames are stamped with (ISR-safe).
  uint32_t frames_per_pass{2U};            ///< The most frames one Service() takes.
  kb::Milliseconds conversion_timeout{kb::Milliseconds{5U}}; ///< One-shot wait of the fallback.
  uint8_t irq_priority{5U}; ///< The ADC interrupt of the fallback's one-shot conversions.
};

/** @brief Why a stream serves the fallback (FallbackReason()). */
enum class AdcFallback : uint8_t {
  None = 0,     ///< DMA serves.
  NoStream = 1, ///< The board assigned no stream.
  Shape = 2,    ///< The conversion count read back differs from the frame's elements.
  Restarts = 3, ///< The supervisor asked for it (restarts did not bring good frames back).
};

/** @brief What the drain of one interrupt entry found (AdcStreamCore::Drain). */
struct AdcDrain {
  uint32_t flags{0U};   ///< The DMA flags serviced (kDmaFlag*), kAdcFlagOverrun when OVR was set.
  bool overrun{false};  ///< The ADC's overrun flag was set (now cleared).
  bool half0{false};    ///< The first half finished.
  bool half1{false};    ///< The second half finished.
  bool lapped{false};   ///< Both were pending: the first half may already be overwritten.
  uint32_t started{0U}; ///< The cycle count at entry.
};

/**
 * @brief The platform-free part of an ADC scan stream (src/adc_stream.cpp): the checks, the ADC and
 * DMA set-up, the drain of the interrupt, the one-shot fallback, the stop.
 */
class AdcStreamCore {
public:
  /** @brief A core with `config` serving frames of `ranks` samples (`kb::dma::Stream` elements). */
  AdcStreamCore(const AdcStreamConfig& config, std::size_t ranks) noexcept
      : config_(config), ranks_(ranks) {}
  AdcStreamCore(const AdcStreamCore&) = delete;
  AdcStreamCore& operator=(const AdcStreamCore&) = delete;
  AdcStreamCore(AdcStreamCore&&) = delete;
  AdcStreamCore& operator=(AdcStreamCore&&) = delete;
  /** @brief Closes the instance. */
  ~AdcStreamCore() noexcept;

  /**
   * @brief The start checks and the set-up: the vendor code (0: ready), with `Dma()` telling which
   * producer serves. Idempotent while open.
   */
  [[nodiscard]] uint32_t Open() noexcept;
  /** @brief The DMA circular over `ring` (the lent hardware buffer), then the ADC armed. */
  [[nodiscard]] uint32_t Arm(std::span<std::byte> ring, std::size_t bytes) noexcept;
  /** @brief ADC stopped, the stream stopped with proof; the ADC overrun read (stop reason). */
  [[nodiscard]] kb::dma::StopEvidence Halt(bool& overrun) noexcept;
  /** @brief Closes the instance and releases the stream. */
  void Close() noexcept;
  /** @brief One scan of every rank by one-shot conversions (the fallback); the vendor code. */
  [[nodiscard]] uint32_t OneShotScan(std::span<uint16_t> out) noexcept;
  /** @brief Switches to the one-shot fallback for `reason` (the DMA side must be halted). */
  [[nodiscard]] uint32_t EnterFallback(AdcFallback reason) noexcept;

  /** @brief Interrupt: services every DMA flag, reads the ADC overrun and says what finished. */
  [[nodiscard]] AdcDrain Drain() noexcept;
  /** @brief Interrupt: true when the DMA writes inside `half` now (after the copy: torn). */
  [[nodiscard]] bool Inside(std::size_t half) const noexcept;
  /** @brief The cycles since `started`. */
  [[nodiscard]] static uint32_t CyclesSince(uint32_t started) noexcept;
  /** @brief The frame clock (the configured one). */
  [[nodiscard]] kb::Instant Now() const noexcept;

  /** @brief True when DMA serves (after Open()). */
  [[nodiscard]] bool Dma() const noexcept {
    return reason_ == AdcFallback::None;
  }
  /** @brief Why the fallback serves. */
  [[nodiscard]] AdcFallback FallbackReason() const noexcept {
    return reason_;
  }
  /** @brief True after Open() succeeded. */
  [[nodiscard]] bool IsOpen() const noexcept {
    return open_;
  }
  /** @brief The stream (installs the owner's interrupt hook). */
  [[nodiscard]] DmaStream& Stream() noexcept {
    return dma_;
  }
  /** @brief The stream's counters. */
  [[nodiscard]] DmaStats DmaCounters() const noexcept {
    return dma_.Stats();
  }
  /** @brief The configuration. */
  [[nodiscard]] const AdcStreamConfig& Config() const noexcept {
    return config_;
  }
  /** @brief The conversion count read back at the last Open() (0 when not read). */
  [[nodiscard]] uint32_t ConversionsReadBack() const noexcept {
    return conversions_;
  }
  /** @brief The last call's status and its vendor code. */
  [[nodiscard]] kb::Result<void> LastError() const noexcept {
    return error_.Get();
  }

private:
  [[nodiscard]] bool configValid() const noexcept;
  [[nodiscard]] bool claim() const noexcept;
  void unclaim() const noexcept;
  [[nodiscard]] uint32_t openFallback() noexcept;
  [[nodiscard]] uint32_t startChecks(bool& claimed) noexcept;
  [[nodiscard]] uint32_t chooseProducer() noexcept;
  [[nodiscard]] uint32_t openStream() noexcept;

  AdcStreamConfig config_;
  std::size_t ranks_;
  DmaStream dma_;
  Completion conversion_;
  LastErrorRecord error_;
  uint32_t conversions_{0U};
  AdcFallback reason_{AdcFallback::None};
  bool open_{false};
  bool adc_open_{false};
};

/**
 * @brief A single-writer, multi-reader lock-free cell (the owner publishes, any task reads): the
 * value as words of std::atomic<uint32_t> between an even/odd sequence; a read gives up with `Busy`
 * after a few torn copies instead of spinning.
 */
template <typename T>
  requires std::is_trivially_copyable_v<T> && std::is_nothrow_default_constructible_v<T>
class AdcSeqlock {
public:
  /** @brief Publishes `value` (the one writer only). */
  void Publish(const T& value) noexcept {
    const auto bytes = std::bit_cast<std::array<std::byte, sizeof(T)>>(value);
    const uint32_t begin = seq_.load(std::memory_order_relaxed) + 1U;
    seq_.store(begin, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_release);
    for (std::size_t w = 0U; w < kWords; ++w) {
      uint32_t word = 0U;
      for (std::size_t b = 0U; b < 4U; ++b) {
        const std::size_t i = (w * 4U) + b;
        word |= (i < sizeof(T)) ? (std::to_integer<uint32_t>(bytes[i]) << (b * 8U)) : 0U;
      }
      words_[w].store(word, std::memory_order_relaxed);
    }
    seq_.store(begin + 1U, std::memory_order_release);
  }
  /** @brief The value; `Busy` after kAttempts torn copies, `Empty` before the first publish. */
  [[nodiscard]] kb::Result<T> Read() const noexcept {
    std::array<std::byte, sizeof(T)> bytes{};
    bool clean = false;
    uint32_t seq = 0U;
    for (uint32_t attempt = 0U; (attempt < kAttempts) && !clean; ++attempt) {
      seq = seq_.load(std::memory_order_acquire);
      for (std::size_t w = 0U; w < kWords; ++w) {
        const uint32_t word = words_[w].load(std::memory_order_relaxed);
        for (std::size_t b = 0U; b < 4U; ++b) {
          const std::size_t i = (w * 4U) + b;
          if (i < sizeof(T)) {
            bytes[i] = static_cast<std::byte>((word >> (b * 8U)) & 0xFFU);
          }
        }
      }
      std::atomic_thread_fence(std::memory_order_acquire);
      clean = ((seq & 1U) == 0U) && (seq == seq_.load(std::memory_order_relaxed));
    }
    kb::Result<T> out = kb::Result<T>::Failure(kb::Status::Busy);
    if (clean && (seq == 0U)) {
      out = kb::Result<T>::Failure(kb::Status::Empty);
    } else if (clean) {
      out = kb::Result<T>::Success(std::bit_cast<T>(bytes));
    } else {
      out = kb::Result<T>::Failure(kb::Status::Busy);
    }
    return out;
  }

private:
  static constexpr uint32_t kAttempts = 4U;
  static constexpr std::size_t kWords = (sizeof(T) + 3U) / 4U;
  std::atomic<uint32_t> seq_{0U};
  std::array<std::atomic<uint32_t>, kWords> words_{};
};

/** @brief The latest scan the owner published (`Latest()`). */
template <AdcFrame Frame>
struct AdcSnapshot {
  Frame scan{};       ///< The samples.
  uint32_t seq{0U};   ///< The frame's sequence number.
  uint64_t at_us{0U}; ///< The frame's completion time.
  uint32_t epoch{0U}; ///< The producer epoch (changes on a restart or a mode switch).
  uint8_t flags{0U};  ///< Bit 0 gap, bit 1 torn, bit 2 fallback.
};

/**
 * @brief The timer-triggered scan stream of one ADC instance (see the file note).
 * @tparam Frame One scan (`AdcFrame`, e.g. AdcScan<6>).
 * @tparam R     The region of the ring buffer (default H7 region: h7::D2Sram).
 */
template <AdcFrame Frame, PlacedRegion R>
class AdcDmaStream : public DmaIrqHook {
public:
  /// The ranks of one scan.
  static constexpr std::size_t kRanks = Frame{}.v.size();
  /// The ring: two frames, contiguous (the DMA's circular buffer), padded to the region's unit.
  using Ring = kb::dma::Buffer<R, 2U * sizeof(Frame)>;
  /// The stream type.
  using StreamType = kb::dma::Stream<Frame, 2U, R>;
  static_assert(kRanks <= kAdcMaxRanks, "an ADC scan has at most 16 ranks");

  /** @brief A stream over `ring` (static storage in the board's region) with `config`. */
  AdcDmaStream(Ring& ring, const AdcStreamConfig& config) noexcept
      : DmaIrqHook{&AdcDmaStream::serve}, ring_(ring), core_(config, kRanks),
        stream_(config.supervision) {}
  AdcDmaStream(const AdcDmaStream&) = delete;
  AdcDmaStream& operator=(const AdcDmaStream&) = delete;
  AdcDmaStream(AdcDmaStream&&) = delete;
  AdcDmaStream& operator=(AdcDmaStream&&) = delete;
  /** @brief Stops the stream. */
  ~AdcDmaStream() noexcept {
    static_cast<void>(Stop());
  }

  /** @brief Starts the stream (owner task, after the scheduler; idempotent). See the file note. */
  [[nodiscard]] kb::Status Start() noexcept {
    kb::Status status = kb::Status::Ok;
    if (!stream_.Stats().running) {
      uint32_t code = core_.Open();
      if ((code == hal::kOk) && core_.Dma()) {
        code = DmaPlacementCheck(ring_);
      }
      if ((code == hal::kOk) && core_.Dma()) {
        code = armDma();
      } else if (code == hal::kOk) {
        static_cast<void>(stream_.Start(core_.Now()));
        static_cast<void>(stream_.EnterFallback(core_.Now()));
      } else {
        core_.Close();
      }
      status = MapHalStatus(code & 0xFFU);
    }
    return status;
  }

  /** @brief Stops the stream (the DMA stopped with proof, or the ring quarantined). */
  [[nodiscard]] kb::Status Stop() noexcept {
    kb::Status status = kb::Status::Ok;
    if (core_.IsOpen()) {
      bool overrun = false;
      const kb::dma::StopEvidence evidence = core_.Halt(overrun);
      settle(evidence);
      static_cast<void>(stream_.Stop());
      core_.Close();
      status = (evidence == kb::dma::StopEvidence::Confirmed) ? kb::Status::Ok : kb::Status::Fault;
    }
    return status;
  }

  /**
   * @brief One owner pass: the fallback's one-shot scan (when it serves), at most
   * `frames_per_pass` frames taken and the latest published, then the supervision. The action the
   * supervisor asked for and that this pass carried out.
   */
  [[nodiscard]] kb::dma::SupervisorAction Service() noexcept {
    if (stream_.Stats().running && !core_.Dma()) {
      Frame scan{};
      if (core_.OneShotScan(std::span<uint16_t>(scan.v)) == hal::kOk) {
        static_cast<void>(stream_.Publish(scan, core_.Now(), 0U));
      }
    }
    typename StreamType::FrameView view{};
    for (uint32_t taken = 0U;
         (taken < core_.Config().frames_per_pass) && (stream_.TryTake(view) == kb::Status::Ok);
         ++taken) {
      AdcSnapshot<Frame> snap{};
      snap.scan = view.Data();
      snap.seq = view.Seq();
      snap.at_us = view.At().Us();
      snap.epoch = view.Epoch();
      snap.flags = static_cast<uint8_t>((view.Gap() ? 1U : 0U) | (view.Torn() ? 2U : 0U) |
                                        (view.Fallback() ? 4U : 0U));
      latest_.Publish(snap);
      stream_.Give(std::move(view));
    }
    const kb::dma::SupervisorAction action = stream_.Supervise(core_.Now());
    if (action == kb::dma::SupervisorAction::Restart) {
      static_cast<void>(Restart());
    } else if (action == kb::dma::SupervisorAction::UseFallback) {
      useFallback(AdcFallback::Restarts);
    } else {
      // None, or BackOff: wait for the next pass.
    }
    return action;
  }

  /**
   * @brief Stops the hardware, records the stop reason (an ADC overrun is counted with
   * `OnOverrun`, so the next frame shows the gap) and starts it again: a new producer epoch.
   */
  [[nodiscard]] kb::Status Restart() noexcept {
    kb::Status status = kb::Status::Ok;
    if (core_.Dma() && core_.IsOpen()) {
      bool overrun = false;
      const kb::dma::StopEvidence evidence = core_.Halt(overrun);
      if (overrun) {
        stream_.OnOverrun(kAdcFlagOverrun); // the producer is stopped: the owner may report it
      }
      settle(evidence);
      if (evidence == kb::dma::StopEvidence::Confirmed) {
        const uint32_t code = armDma();
        stream_.NoteRestart(core_.Now());
        status = MapHalStatus(code & 0xFFU);
      } else {
        static_cast<void>(stream_.Stop());
        status = kb::Status::Fault;
      }
    }
    return status;
  }

  /** @brief The latest scan the owner published (any task); Empty before the first. */
  [[nodiscard]] kb::Result<AdcSnapshot<Frame>> Latest() const noexcept {
    return latest_.Read();
  }
  /** @brief The stream (consumer side for the owner, counters for anyone). */
  [[nodiscard]] StreamType& Stream() noexcept {
    return stream_;
  }
  /** @brief Who produces now. */
  [[nodiscard]] kb::dma::StreamMode Mode() const noexcept {
    return stream_.Mode();
  }
  /** @brief Why the fallback serves (None while DMA serves). */
  [[nodiscard]] AdcFallback FallbackReason() const noexcept {
    return core_.FallbackReason();
  }
  /** @brief What the stream offers now. */
  [[nodiscard]] kb::dma::Capabilities Capabilities() const noexcept {
    kb::dma::Capabilities c{};
    c.mode = core_.Dma() ? kb::dma::TransferMode::Dma : kb::dma::TransferMode::Interrupt;
    c.zero_copy = false;
    c.confirmed_abort = core_.Dma();
    c.max_frame_bytes = sizeof(Frame);
    c.staging_bytes = Ring::kCapacity;
    c.align_bytes = R::kAlignBytes;
    return c;
  }
  /** @brief The DMA part of a health record. */
  [[nodiscard]] kb::dma::DmaHealth Health() const noexcept {
    const DmaStats d = core_.DmaCounters();
    kb::dma::DmaHealth h{};
    h.mode = Capabilities().mode;
    h.faulted = ring_.Current() == kb::dma::BufferState::Quarantined;
    h.quarantined = d.quarantines;
    h.errors = d.transfer_errors + d.direct_errors;
    return h;
  }
  /** @brief The stream's DMA counters. */
  [[nodiscard]] DmaStats DmaCounters() const noexcept {
    return core_.DmaCounters();
  }
  /** @brief The core (checks, read-back). */
  [[nodiscard]] const AdcStreamCore& Core() const noexcept {
    return core_;
  }

private:
  [[nodiscard]] kb::dma::PortKey<AdcDmaStream> key() const noexcept {
    return kb::dma::PortKey<AdcDmaStream>{};
  }

  [[nodiscard]] uint32_t armDma() noexcept {
    uint32_t code = port_code::kQuarantined;
    if (ring_.Lend(key(), kb::dma::Direction::FromDevice, Ring::kBytes, ops_, lease_) ==
        kb::Status::Ok) {
      core_.Stream().SetIrqHook(this);
      static_cast<void>(
          stream_.Start(core_.Now())); // the stall clock runs before the hardware does
      code = core_.Arm(lease_.DeviceView(key()), Ring::kBytes);
      if (code != hal::kOk) {
        static_cast<void>(stream_.Stop());
        static_cast<void>(lease_.Abort(key(), ops_, kb::dma::StopEvidence::Confirmed));
      }
    }
    return code;
  }

  void settle(kb::dma::StopEvidence evidence) noexcept {
    if (lease_.Active()) {
      static_cast<void>(lease_.Abort(key(), ops_, evidence));
    }
  }

  void useFallback(AdcFallback reason) noexcept {
    bool overrun = false;
    const kb::dma::StopEvidence evidence = core_.Halt(overrun);
    settle(evidence);
    if ((evidence == kb::dma::StopEvidence::Confirmed) &&
        (core_.EnterFallback(reason) == hal::kOk)) {
      static_cast<void>(stream_.EnterFallback(core_.Now()));
    } else {
      static_cast<void>(stream_.Stop());
    }
  }

  static void serve(DmaIrqHook& hook) noexcept {
    static_cast<AdcDmaStream&>(hook).isr();
  }

  // The interrupt: the only drainer. Halves are published oldest first; a half the DMA lapped (both
  // flags pending, or the transfer counter inside the half after its copy) is published Torn.
  void isr() noexcept {
    const AdcDrain d = core_.Drain();
    const kb::Instant at = core_.Now();
    if (d.overrun) {
      stream_.OnOverrun(d.flags);
    }
    if (d.half0) {
      publishHalf(0U, at, d.flags, d.lapped);
    }
    if (d.half1) {
      publishHalf(1U, at, d.flags, false);
    }
    stream_.NoteIsrCycles(AdcStreamCore::CyclesSince(d.started));
  }

  void publishHalf(std::size_t half, kb::Instant at, uint32_t flags, bool lapped) noexcept {
    const std::span<std::byte> device = lease_.DeviceView(key());
    const std::size_t offset = half * sizeof(Frame);
    if (device.size() >= (offset + sizeof(Frame))) {
      if constexpr (R::kPolicy == kb::dma::CachePolicy::Maintained) {
        // Whole lines of the ring around the half (the CPU never writes the ring: no dirty line).
        const std::size_t first = offset & ~(R::kLineBytes - 1U);
        const std::size_t last = kb::dma::RoundUp(offset + sizeof(Frame), R::kLineBytes);
        PortCacheOps::Invalidate(device.subspan(first, std::min(last, device.size()) - first));
      }
      PortCacheOps::Barrier();
      std::array<std::byte, sizeof(Frame)> raw{};
      static_cast<void>(std::copy_n(device.begin() + static_cast<std::ptrdiff_t>(offset),
                                    sizeof(Frame), raw.begin()));
      const bool torn = lapped || core_.Inside(half);
      static_cast<void>(
          stream_.Publish(std::bit_cast<Frame>(raw), at, flags,
                          torn ? kb::dma::Integrity::Torn : kb::dma::Integrity::Clean));
    }
  }

  Ring& ring_;
  AdcStreamCore core_;
  StreamType stream_;
  PortCacheOps ops_{};
  kb::dma::Lease<R> lease_{};
  AdcSeqlock<AdcSnapshot<Frame>> latest_{};
};

} // namespace kb::port::stm32

#endif // KB_PORT_STM32_ADC_STREAM_HPP

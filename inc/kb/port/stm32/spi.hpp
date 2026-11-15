/**
 * @file spi.hpp
 * @brief `kb::port::stm32::SpiBus` (one SPI peripheral as master) and `kb::port::stm32::SpiDevice`
 * (one chip select on it, driven as a GPIO), which satisfies `kb::iface::SpiDevice` and
 * `kb::iface::SpiClockControl`. HAL `stm32<family>xx_hal_spi`, interrupt-driven calls.
 *
 * A Transfer() takes the bus lock (at most `SpiDeviceConfig::timeout`), sets the peripheral to the
 * device's mode and baud-rate prescaler when another device used it last (re-initialised, so the
 * peripheral is disabled while it is reprogrammed), drives chip select low, clocks the frame by
 * interrupt (`HAL_SPI_TransmitReceive_IT` or `HAL_SPI_Transmit_IT`, in chunks of at most 65535
 * bytes; a read-only frame is a full-duplex transfer of the zeroed receive buffer in place, on
 * every family), and drives chip select high again, whatever the result. The task waits for the
 * HAL's completion callback on the port's clock (completion.hpp), asleep under an RTOS; each chunk
 * has `timeout`. A timeout aborts the transfer (`HAL_SPI_Abort`) and the peripheral is initialised
 * again before the next frame. The inter-data idleness (H7 MIDI) is the HAL default (none) for
 * every device.
 *
 * | Result | Meaning |
 * | --- | --- |
 * | `Ok` | The frame was clocked |
 * | `InvalidArgument` | Empty or unequal spans, a clock out of the prescaler's reach, bad config |
 * | `NotReady` | (also) the SPI kernel clock reads 0 |
 * | `Busy` | The bus lock was not had in time; nothing was clocked |
 * | `Timeout` | The HAL did not finish in time; the transfer was aborted |
 * | `BusError` | Mode fault, overrun or CRC error (`HAL_SPI_ERROR_*` in Detail) |
 * | `NotReady` | EnsureReady() has not succeeded |
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 The port-stm32 authors
 */
#ifndef KB_PORT_STM32_SPI_HPP
#define KB_PORT_STM32_SPI_HPP

#include <cstddef>
#include <cstdint>
#include <span>

#include "kb/interfaces/spi.hpp"
#include "kb/port/stm32/completion.hpp"
#include "kb/port/stm32/error.hpp"
#include "kb/rtos/mutex.hpp"
#include "kb/utils/result.hpp"
#include "kb/utils/status.hpp"
#include "kb/utils/time.hpp"

namespace kb::port::stm32 {

/** @brief How an SPI peripheral is wired. */
struct SpiBusConfig {
  uint8_t instance{1U};         ///< SPI1 is 1, ... SPI6 is 6 (where the device has it).
  PinId sck{};                  ///< SCK with its alternate function.
  PinId mosi{};                 ///< MOSI with its alternate function, or unused.
  PinId miso{};                 ///< MISO with its alternate function, or unused.
  uint32_t kernel_clock_hz{0U}; ///< The SPI kernel clock; 0 reads it from the RCC.
  uint8_t irq_priority{5U};     ///< NVIC priority of the SPI interrupt.
};

/** @brief One device on an SpiBus. */
struct SpiDeviceConfig {
  PinId cs{};       ///< Chip select, driven as a push-pull GPIO (active low).
  uint8_t mode{0U}; ///< SPI mode 0 to 3 (CPOL, CPHA).
  /// The highest SCK wanted; the prescaler rounds down from it, and from the bus's interrupt
  /// ceiling (SpiBus::InterruptCeilingHz) when that is lower: ActualClockHz() says what is used.
  uint32_t clock_hz{1000000U};
  kb::Milliseconds timeout{
      kb::Milliseconds{20U}}; ///< Bus lock wait and per-chunk limit, to kMaxTimeout.
};

/** @brief Counters of an SPI device. */
struct SpiStats {
  uint32_t transfers{0U};       ///< Frames started.
  uint32_t failures{0U};        ///< Frames that failed.
  uint32_t rebuilds{0U};        ///< Peripheral initialisations after a timeout.
  uint32_t lock_timeouts{0U};   ///< Calls refused with Busy.
  uint32_t clamps{0U};          ///< Clock selections lowered to the bus's interrupt ceiling.
  uint32_t actual_clock_hz{0U}; ///< The SCK of the next frame (ActualClockHz()); 0 before start.
};

/**
 * @brief The baud-rate prescaler exponent for at most `hz` from `kernel_hz`: SCK is kernel /
 * 2^(n+1), n from 0 (divide by 2) to 7 (divide by 256). `ok` is false when even 256 is too fast.
 */
[[nodiscard]] uint8_t SpiPrescalerExponent(uint32_t kernel_hz, uint32_t hz, bool& ok) noexcept;

/**
 * @brief Core cycles per SCK bit an interrupt-driven transfer needs, without and with an SPI FIFO.
 * The HAL's interrupt handler runs once for each transmit-empty and each receive-not-empty event:
 * about 300 core cycles per byte with entry and exit. Without a FIFO (F4) a received byte must be
 * read within one byte time, with room for other interrupts: 64 cycles per bit (512 per byte).
 * With a FIFO (G4, L4: 4 bytes; H7: 16) the slack is several bytes, but the handler must still
 * keep up on average: 40 cycles per bit (320 per byte). Estimates from the pinned HAL's handler
 * structure, not bench measurements (docs/peripherals/spi.md).
 */
inline constexpr uint32_t kSpiItCyclesPerBitNoFifo = 64U;
inline constexpr uint32_t kSpiItCyclesPerBitFifo = 40U; ///< See kSpiItCyclesPerBitNoFifo.

/** @brief The highest SCK an interrupt-driven transfer is run at on a `core_hz` core. */
[[nodiscard]] constexpr uint32_t SpiInterruptCeilingHz(uint32_t core_hz, bool fifo) noexcept {
  return core_hz / (fifo ? kSpiItCyclesPerBitFifo : kSpiItCyclesPerBitNoFifo);
}

/** @brief One SPI peripheral as master: initialises it and serialises its devices. */
class SpiBus {
public:
  /** @brief Keeps `config`. */
  explicit SpiBus(const SpiBusConfig& config) noexcept : config_(config) {}
  SpiBus(const SpiBus&) = delete;
  SpiBus& operator=(const SpiBus&) = delete;
  SpiBus(SpiBus&&) = delete;
  SpiBus& operator=(SpiBus&&) = delete;
  /** @brief De-initialises the peripheral if it is up. */
  ~SpiBus() noexcept;

  /** @brief Clock, pins and HAL_SPI_Init once; idempotent. Call once before sharing. */
  [[nodiscard]] kb::Status EnsureReady() noexcept;
  /** @brief HAL_SPI_DeInit. */
  [[nodiscard]] kb::Status Release() noexcept;
  /** @brief The last call's status and its vendor code in Detail(). */
  [[nodiscard]] kb::Result<void> LastError() const noexcept {
    return error_.Get();
  }
  /** @brief The configuration. */
  [[nodiscard]] const SpiBusConfig& Config() const noexcept {
    return config_;
  }
  /** @brief True after EnsureReady() succeeded. */
  [[nodiscard]] bool IsReady() const noexcept {
    return ready_;
  }
  /** @brief The kernel clock in Hz (configured, or read from the RCC by EnsureReady()). */
  [[nodiscard]] uint32_t KernelClockHz() const noexcept {
    return kernel_hz_;
  }
  /** @brief Takes the bus lock, waiting at most `timeout` (devices hold it for one frame). */
  [[nodiscard]] bool TryLockFor(kb::Milliseconds timeout) noexcept {
    return mutex_.TryLockFor(timeout);
  }
  /** @brief Gives the bus lock back. */
  void Unlock() noexcept {
    mutex_.Unlock();
  }
  /** @brief The highest SCK of an interrupt-driven transfer (SpiInterruptCeilingHz, at start). */
  [[nodiscard]] uint32_t InterruptCeilingHz() const noexcept {
    return ceiling_hz_;
  }
  /** @brief With the lock held: the peripheral in `mode` at prescaler exponent `prescaler`. */
  [[nodiscard]] uint32_t SelectLocked(uint8_t mode, uint8_t prescaler) noexcept;
  /** @brief With the lock held: the next frame of any device initialises the peripheral again. */
  void InvalidateLocked() noexcept {
    mode_ = 0xFFU;
    prescaler_ = 0xFFU;
  }
  /** @brief With the lock held: one interrupt-driven chunk, waited for on `deadline`. */
  [[nodiscard]] uint32_t TransferLocked(std::span<const uint8_t> tx, std::span<uint8_t> rx,
                                        Deadline& deadline) noexcept;

private:
  SpiBusConfig config_;
  kb::rtos::Mutex mutex_;
  Completion done_; ///< Completed by the HAL's transfer callbacks.
  LastErrorRecord error_;
  uint32_t kernel_hz_{0U};
  uint32_t ceiling_hz_{0U};
  uint8_t mode_{0xFFU};
  uint8_t prescaler_{0xFFU};
  bool ready_{false};
};

/** @brief One chip select on an SpiBus; satisfies kb::iface::SpiDevice and SpiClockControl. */
class SpiDevice {
public:
  /** @brief Refers to `bus`, which must outlive the device. */
  SpiDevice(SpiBus& bus, const SpiDeviceConfig& config) noexcept : bus_(bus), config_(config) {}
  SpiDevice(const SpiDevice&) = delete;
  SpiDevice& operator=(const SpiDevice&) = delete;
  SpiDevice(SpiDevice&&) = delete;
  SpiDevice& operator=(SpiDevice&&) = delete;
  /** @brief Releases the chip select pin. */
  ~SpiDevice() noexcept;

  /** @brief Brings the bus up, configures chip select high; idempotent. */
  [[nodiscard]] kb::Status EnsureReady() noexcept;
  /** @brief One full-duplex frame with chip select held (see the file comment). */
  [[nodiscard]] kb::Status Transfer(std::span<const uint8_t> tx, std::span<uint8_t> rx) noexcept;
  /** @brief Changes the highest SCK; applied at the next frame. */
  [[nodiscard]] kb::Status SetClockHz(uint32_t hz) noexcept;
  /** @brief Gives the chip select pin back. */
  [[nodiscard]] kb::Status Release() noexcept;
  /** @brief The last call's status and its vendor code in Detail(). */
  [[nodiscard]] kb::Result<void> LastError() const noexcept {
    return error_.Get();
  }
  /** @brief The counters (a consistent copy). */
  [[nodiscard]] SpiStats Stats() const noexcept;
  /** @brief The configuration (with the current clock). */
  [[nodiscard]] const SpiDeviceConfig& Config() const noexcept {
    return config_;
  }
  /** @brief The SCK of the next frame in Hz. */
  [[nodiscard]] uint32_t ActualClockHz() const noexcept;

private:
  [[nodiscard]] uint32_t frame(std::span<const uint8_t> tx, std::span<uint8_t> rx) noexcept;
  [[nodiscard]] uint32_t claimCs() const noexcept;
  [[nodiscard]] kb::Status finish(uint32_t code) noexcept;
  [[nodiscard]] bool configValid() const noexcept;
  [[nodiscard]] uint32_t limited(uint32_t hz) const noexcept;
  void noteClock(uint32_t asked) noexcept;
  void count(uint32_t SpiStats::* field) noexcept;

  SpiBus& bus_;
  SpiDeviceConfig config_;
  LastErrorRecord error_;
  SpiStats stats_;
  uint8_t prescaler_{0U};
  bool ready_{false};
};

static_assert(kb::iface::SpiDevice<SpiDevice>, "SpiDevice satisfies kb::iface::SpiDevice");
static_assert(kb::iface::SpiClockControl<SpiDevice>, "SpiDevice satisfies SpiClockControl");

} // namespace kb::port::stm32

#endif // KB_PORT_STM32_SPI_HPP

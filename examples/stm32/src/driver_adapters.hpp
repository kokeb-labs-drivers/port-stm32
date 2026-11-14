// Thin adapters from kb::iface contracts to three drivers' own port concepts: each derives from the
// driver's CRTP port base and forwards each hook to a kb contract in a line or two. They are written
// against the contracts, not against this port, so the same adapter runs over any MCU port (or a
// *Ref chosen at run time). Example code.
// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The port-stm32 authors
#ifndef KB_PORT_STM32_EXAMPLE_DRIVER_ADAPTERS_HPP
#define KB_PORT_STM32_EXAMPLE_DRIVER_ADAPTERS_HPP

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "bno08x_interface.hpp"
#include "kb/interfaces/interfaces.hpp"
#include "kb/utils/time.hpp"
#include "pcal9555_i2c_interface.hpp"
#include "tmc5160_spi_interface.hpp"

namespace adapters {

inline bool Ok(kb::Status s) noexcept {
  return s == kb::Status::Ok;
}

inline kb::iface::LogLevel LevelOf(pcal9555::LogLevel level) noexcept {
  kb::iface::LogLevel out = kb::iface::LogLevel::Debug;
  if (level == pcal9555::LogLevel::Error) {
    out = kb::iface::LogLevel::Error;
  } else if (level == pcal9555::LogLevel::Warning) {
    out = kb::iface::LogLevel::Warning;
  } else if (level == pcal9555::LogLevel::Info) {
    out = kb::iface::LogLevel::Info;
  } else {
    out = kb::iface::LogLevel::Debug;
  }
  return out;
}

// --- pcal9555: I2C register bus with the optional failure and log hooks -------------------------
template <kb::iface::I2cBus Bus, kb::iface::LogSink Sink>
class Pcal9555I2c : public pcal9555::I2cInterface<Pcal9555I2c<Bus, Sink>> {
public:
  Pcal9555I2c(Bus& bus, Sink& log) noexcept : bus_(bus), log_(log) {}
  bool EnsureInitialized() noexcept {
    return Ok(bus_.EnsureReady());
  }
  bool Write(uint8_t address, const uint8_t* data, std::size_t len) noexcept {
    return Ok(bus_.Write(address, std::span<const uint8_t>{data, len}));
  }
  bool WriteRead(uint8_t address, const uint8_t* tx, std::size_t tx_len, uint8_t* rx,
                 std::size_t rx_len) noexcept {
    return Ok(bus_.WriteRead(address, std::span<const uint8_t>{tx, tx_len},
                             std::span<uint8_t>{rx, rx_len}));
  }
  bool GetLastFailure(pcal9555::I2cFailure& failure) noexcept {
    const kb::iface::I2cFailure f = bus_.LastFailure();
    if (f == kb::iface::I2cFailure::AddressNack) {
      failure = pcal9555::I2cFailure::AddressNack;
    } else if (f == kb::iface::I2cFailure::DataNack) {
      failure = pcal9555::I2cFailure::DataNack;
    } else {
      failure = pcal9555::I2cFailure::Other;
    }
    return f != kb::iface::I2cFailure::None;
  }
  void Log(pcal9555::LogLevel level, const char* message) noexcept {
    log_.Write(LevelOf(level), std::string_view{message});
  }

private:
  Bus& bus_;
  Sink& log_;
};

// --- tmc5160: SPI datagrams, DRV_ENN on a pin, the delay from BusyWait ------------------------
template <kb::iface::SpiDevice Dev, kb::iface::GpioOutput Enable, kb::iface::BusyWait Delay>
class Tmc5160Spi : public tmc5160::SpiInterface<Tmc5160Spi<Dev, Enable, Delay>> {
public:
  Tmc5160Spi(Dev& dev, Enable& drv_enn, Delay& delay) noexcept
      : dev_(dev), drv_enn_(drv_enn), delay_(delay) {}
  bool EnsureInitialized() noexcept {
    return Ok(dev_.EnsureReady());
  }
  bool Transfer(const uint8_t* tx, uint8_t* rx, std::size_t len) noexcept {
    return Ok(dev_.Transfer(std::span<const uint8_t>{tx, len}, std::span<uint8_t>{rx, len}));
  }
  void DelayUs(uint32_t us) noexcept {
    delay_.WaitUs(kb::Microseconds{us});
  }
  bool GpioSet(tmc5160::CtrlPin pin, tmc5160::GpioSignal signal) noexcept {
    bool ok = false;
    if (pin == tmc5160::CtrlPin::DrvEnn) {
      // DRV_ENN is active low: ACTIVE (bridge enabled) drives the pin low.
      const bool active = signal == tmc5160::GpioSignal::ACTIVE;
      ok = Ok(drv_enn_.Set(active ? kb::iface::Level::Low : kb::iface::Level::High));
    }
    return ok;
  }

private:
  Dev& dev_;
  Enable& drv_enn_;
  Delay& delay_;
};

// --- bno08x: SHTP over I2C with H_INTN, the clock narrowed for the driver ----------------------
template <kb::iface::I2cBus Bus, kb::iface::GpioInput Int, kb::iface::MonotonicClock Clock>
class Bno08xI2c : public bno08x::HubInterface<Bno08xI2c<Bus, Int, Clock>> {
public:
  static constexpr bno08x::Transport kTransport = bno08x::Transport::I2c;
  Bno08xI2c(Bus& bus, Int& int_pin, const Clock& clock) noexcept
      : bus_(bus), int_(int_pin), clock_(clock) {}
  bool EnsureInitialized() noexcept {
    return Ok(bus_.EnsureReady());
  }
  uint32_t NowUs() noexcept {
    return kb::utils::NarrowUs(clock_.Now());
  }
  bool I2cWrite(uint8_t address, const uint8_t* data, std::size_t len) noexcept {
    return Ok(bus_.Write(address, std::span<const uint8_t>{data, len}));
  }
  bool I2cRead(uint8_t address, uint8_t* data, std::size_t len) noexcept {
    return Ok(bus_.Read(address, std::span<uint8_t>{data, len}));
  }
  bool IntAsserted() noexcept {
    kb::iface::Level level = kb::iface::Level::High;
    return Ok(int_.Get(level)) && (level == kb::iface::Level::Low);
  }

private:
  Bus& bus_;
  Int& int_;
  const Clock& clock_;
};

} // namespace adapters

#endif // KB_PORT_STM32_EXAMPLE_DRIVER_ADAPTERS_HPP

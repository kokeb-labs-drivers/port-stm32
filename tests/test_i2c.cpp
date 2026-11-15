// I2cController over the fake HAL I2C: TIMINGR, every member, every result, the interrupt-driven
// transfers and their bounded waits, the handle restore, the BUSY reset and the bit-banged
// recovery.
// Test code. SPDX-License-Identifier: MIT
// Copyright (c) 2026 The port-stm32 authors
#include "support.hpp"

namespace {
using kb::iface::I2cFailure;
constexpr uint8_t kDev = 0x20U;
constexpr int kPortB = 1;

stm::I2cConfig Config() {
  stm::I2cConfig c{};
  c.instance = 1U;
  c.scl = PinId{GpioPort::B, 8U, 4U};
  c.sda = PinId{GpioPort::B, 9U, 4U};
  c.clock_hz = 400000U;
  return c;
}

// Checks a TIMINGR against the I2C specification's minimum times (in kernel clock ticks).
bool Meets(uint32_t kernel, uint32_t bus, const stm::I2cTiming& t) {
  const double tick = (t.presc + 1.0) / kernel;
  const double low = (t.scll + 1.0) * tick;
  const double high = (t.sclh + 1.0) * tick;
  const double period = low + high;
  const bool fast = bus > 100000U;
  return (low >= (fast ? 1.3e-6 : 4.7e-6)) && (high >= (fast ? 0.6e-6 : 4.0e-6)) &&
         (period <= (1.0 / bus) * 1.001) && (period >= (1.0 / bus) * 0.85);
}

void ReenterWrite(void* context) {
  const std::array<uint8_t, 1> b{0x00U};
  KB_CHECK_EQ(static_cast<stm::I2cController*>(context)->Write(kDev, b), Status::Busy);
}
} // namespace

KB_TEST("i2c timing: standard and fast mode meet the specification at common kernel clocks") {
  for (const uint32_t kernel : {16000000U, 64000000U, 80000000U, 170000000U}) {
    for (const uint32_t bus : {100000U, 400000U}) {
      bool ok = false;
      const stm::I2cTiming t = stm::ComputeI2cTiming(kernel, bus, ok);
      KB_CHECK(ok);
      KB_CHECK(Meets(kernel, bus, t));
      KB_CHECK(t.scldel <= 15U);
      KB_CHECK(t.sdadel <= 15U);
    }
  }
  bool ok = false;
  const stm::I2cTiming plus = stm::ComputeI2cTiming(64000000U, 1000000U, ok);
  KB_CHECK(ok);
  KB_CHECK(plus.Register() != 0U);
  static_cast<void>(stm::ComputeI2cTiming(16000000U, 9999U, ok));
  KB_CHECK(!ok);
  static_cast<void>(stm::ComputeI2cTiming(16000000U, 1000001U, ok));
  KB_CHECK(!ok);
  static_cast<void>(stm::ComputeI2cTiming(1000000U, 400000U, ok)); // kernel too slow
  KB_CHECK(!ok);
  static_cast<void>(stm::ComputeI2cTiming(4000000000U, 10000U, ok)); // no prescaler is large enough
  KB_CHECK(!ok);
  constexpr stm::I2cTiming t{1U, 2U, 3U, 4U, 5U};
  static_assert(t.Register() == 0x10230405U);
}

KB_TEST("i2c: a bad configuration is refused and nothing is touched") {
  Fresh();
  const auto refused = [](stm::I2cConfig c) {
    stm::I2cController bus{c};
    return bus.EnsureReady() == Status::InvalidArgument;
  };
  stm::I2cConfig c = Config();
  c.instance = 7U;
  KB_CHECK(refused(c));
  c = Config();
  c.scl = PinId{};
  KB_CHECK(refused(c));
  c = Config();
  c.sda.af = 16U;
  KB_CHECK(refused(c));
  c = Config();
  c.sda.port = GpioPort::K; // no such port on the fake device
  KB_CHECK(refused(c));
  c = Config();
  c.clock_hz = 5000U;
  KB_CHECK(refused(c));
  c = Config();
  c.clock_hz = 2000000U;
  KB_CHECK(refused(c));
  c = Config();
  c.timeout = kb::Milliseconds{0U};
  KB_CHECK(refused(c));
  if (!kF4) {
    c = Config();
    c.kernel_clock_hz = 1000U; // no TIMINGR can be computed
    KB_CHECK(refused(c));
  }
  KB_CHECK_EQ(fake::Calls("HAL_I2C_Init"), 0U);
  c = Config();
  c.clock_hz = 1000000U; // Fast-mode Plus: I2C v2 only
  KB_CHECK_EQ(refused(c), kF4);
}

KB_TEST("i2c: EnsureReady clocks the instance, sets the pins and initialises the HAL once") {
  Fresh();
  stm::I2cController bus{Config()};
  const std::array<uint8_t, 1> tx{1U};
  KB_CHECK_EQ(bus.Write(kDev, tx), Status::NotReady);
  KB_CHECK_EQ(bus.RecoverBus(), Status::NotReady);
  KB_REQUIRE(bus.EnsureReady() == Status::Ok);
  KB_CHECK_EQ(bus.EnsureReady(), Status::Ok);
  KB_CHECK_EQ(fake::Calls("HAL_I2C_Init"), 1U);
  KB_CHECK(fake::ClockEnabled("I2C1"));
  KB_CHECK_EQ(fake::ModeOf(kPortB, 8), GPIO_MODE_AF_OD);
  KB_CHECK_EQ(fake::AlternateOf(kPortB, 9), 4U);
  if (kF4) {
    KB_CHECK_EQ(fake::I2cInit().ClockSpeed, 400000U);
  } else {
    KB_CHECK(fake::I2cInit().Timing != 0U);
    KB_CHECK_EQ(fake::Calls("HAL_I2CEx_ConfigAnalogFilter"), 1U);
  }
  KB_CHECK(bus.Config().clock_hz == 400000U);
  KB_CHECK_EQ(bus.Release(), Status::Ok);
  KB_CHECK_EQ(bus.Release(), Status::Ok);
}

KB_TEST("i2c: a raw TIMINGR is used as given; internal pull-ups") {
  Fresh();
  stm::I2cConfig c = Config();
  c.timing = 0x10802D9BU;
  c.internal_pullups = true;
  stm::I2cController bus{c};
  KB_REQUIRE(bus.EnsureReady() == Status::Ok);
  if (!kF4) {
    KB_CHECK_EQ(fake::I2cInit().Timing, 0x10802D9BU);
  }
  KB_CHECK_EQ(fake::PullOf(kPortB, 8), GPIO_PULLUP);
}

KB_TEST("i2c: HAL failures while initialising are reported") {
  Fresh();
  stm::I2cController bus{Config()};
  fake::Fail("HAL_I2C_Init", HAL_ERROR, 0x20U);
  KB_CHECK_EQ(bus.EnsureReady(), Status::Fault);
  KB_CHECK(FailedWith(bus.LastError(), Status::Fault, Code(HAL_ERROR, 0x20U)));
  if (!kF4) {
    fake::Fail("HAL_I2CEx_ConfigAnalogFilter", HAL_BUSY);
    KB_CHECK_EQ(bus.EnsureReady(), Status::Busy);
  }
  KB_CHECK_EQ(bus.EnsureReady(), Status::Ok);
  fake::Fail("HAL_I2C_DeInit", HAL_ERROR);
  KB_CHECK_EQ(bus.Release(), Status::Fault);
}

KB_TEST("i2c: write, write-read of any length with a repeated START, and read") {
  Fresh();
  fake::I2cPresent(kDev, true);
  stm::I2cController bus{Config()};
  KB_REQUIRE(bus.EnsureReady() == Status::Ok);
  const std::array<uint8_t, 3> tx{0x06U, 0xA5U, 0x5AU};
  KB_CHECK_EQ(bus.Write(kDev, tx), Status::Ok);
  KB_CHECK_EQ(fake::I2cWritten().size(), 3U);
  KB_CHECK_EQ(fake::I2cLastOptions(), I2C_FIRST_AND_LAST_FRAME);
  const std::array<uint8_t, 2> reply{0x12U, 0x34U};
  fake::I2cReply(reply);
  std::array<uint8_t, 2> rx{};
  KB_CHECK_EQ(bus.WriteRead(kDev, std::span<const uint8_t>{tx}.first(1), rx), Status::Ok);
  KB_CHECK_EQ(fake::I2cWritten().size(), 1U);
  KB_CHECK_EQ(fake::I2cLastOptions(), I2C_LAST_FRAME); // the read after the write, no STOP between
  KB_CHECK_EQ(rx[1], 0x34U);
  // Three bytes or more before the repeated START (was Unsupported with HAL_I2C_Mem_Read).
  rx = {};
  KB_CHECK_EQ(bus.WriteRead(kDev, tx, rx), Status::Ok);
  KB_CHECK_EQ(fake::I2cWritten().size(), 3U);
  KB_CHECK_EQ(rx[0], 0x12U);
  rx = {};
  KB_CHECK_EQ(bus.Read(kDev, rx), Status::Ok);
  KB_CHECK_EQ(fake::I2cLastOptions(), I2C_FIRST_AND_LAST_FRAME);
  KB_CHECK_EQ(rx[0], 0x12U);
  KB_CHECK(bus.LastFailure() == I2cFailure::None);
  KB_CHECK_EQ(bus.Stats().transfers, 4U);
  KB_CHECK(bus.LastError().IsOk());
  KB_CHECK_EQ(fake::Calls("HAL_I2C_Master_Transmit_IT"), 0U); // no probe anywhere
}

KB_TEST("i2c: arguments outside the contract are refused before the bus") {
  Fresh();
  stm::I2cController bus{Config()};
  KB_REQUIRE(bus.EnsureReady() == Status::Ok);
  const std::array<uint8_t, 3> tx{0U, 1U, 2U};
  std::array<uint8_t, 1> rx{};
  KB_CHECK_EQ(bus.Write(0x07U, tx), Status::InvalidArgument);
  KB_CHECK_EQ(bus.Write(0x78U, tx), Status::InvalidArgument);
  KB_CHECK_EQ(bus.WriteRead(kDev, std::span<const uint8_t>{}, rx), Status::InvalidArgument);
  KB_CHECK_EQ(bus.WriteRead(kDev, tx, std::span<uint8_t>{}), Status::InvalidArgument);
  KB_CHECK_EQ(bus.Read(kDev, std::span<uint8_t>{}), Status::InvalidArgument);
  static std::array<uint8_t, 0x10000U> big{}; // one byte more than the HAL's uint16_t length
  KB_CHECK_EQ(bus.Write(kDev, big), Status::InvalidArgument);
  KB_CHECK_EQ(bus.Read(kDev, big), Status::InvalidArgument);
  KB_CHECK_EQ(bus.Stats().transfers, 0U);
}

KB_TEST("i2c: timeouts above the port maximum are refused; init never probes") {
  Fresh();
  stm::I2cConfig c = Config();
  c.timeout = kb::Milliseconds{stm::kMaxTimeout.Count() + 1U};
  stm::I2cController too_long{c};
  KB_CHECK_EQ(too_long.EnsureReady(), Status::InvalidArgument);
  c.timeout = kb::Milliseconds{0xFFFFFFFFU}; // HAL_MAX_DELAY: would be "forever" in the HAL
  stm::I2cController forever{c};
  KB_CHECK_EQ(forever.EnsureReady(), Status::InvalidArgument);
  c.timeout = stm::kMaxTimeout;
  stm::I2cController longest{c};
  KB_CHECK_EQ(longest.EnsureReady(), Status::Ok);
  KB_CHECK_EQ(fake::I2cFrames(), 0U); // nothing on the bus at start
}

KB_TEST("i2c: an empty write probes; a write NACK is not split unless nack_probe is set") {
  Fresh();
  stm::I2cController bus{Config()};
  KB_REQUIRE(bus.EnsureReady() == Status::Ok);
  const std::array<uint8_t, 1> tx{0U};
  KB_CHECK_EQ(bus.Write(kDev, std::span<const uint8_t>{}), Status::NoDevice);
  KB_CHECK(bus.LastFailure() == I2cFailure::AddressNack);
  KB_CHECK_EQ(bus.Write(kDev, tx), Status::Nack); // unsplit: no address-only frame follows
  KB_CHECK(FailedWith(bus.LastError(), Status::Nack, Code(HAL_ERROR, HAL_I2C_ERROR_AF)));
  KB_CHECK_EQ(fake::Calls("HAL_I2C_Master_Transmit_IT"), 1U); // only the explicit probe
  std::array<uint8_t, 1> rx{};
  KB_CHECK_EQ(bus.Read(kDev, rx), Status::NoDevice); // a read's NACK is the address's
  fake::I2cPresent(kDev, true);
  KB_CHECK_EQ(bus.Write(kDev, std::span<const uint8_t>{}), Status::Ok);
  KB_CHECK_EQ(fake::Calls("HAL_I2C_DeInit"), 0U); // a NACK never resets the peripheral
  KB_CHECK_EQ(bus.Stats().failures, 3U);
  KB_CHECK_EQ(bus.Release(), Status::Ok);

  stm::I2cConfig c = Config();
  c.nack_probe = true;
  stm::I2cController probing{c};
  KB_REQUIRE(probing.EnsureReady() == Status::Ok);
  fake::I2cPresent(kDev, false);
  KB_CHECK_EQ(probing.Write(kDev, tx), Status::NoDevice);
  KB_CHECK(probing.LastFailure() == I2cFailure::AddressNack);
  fake::I2cPresent(kDev, true);
  fake::I2cDataNack(true);
  KB_CHECK_EQ(probing.Write(kDev, tx), Status::Nack);
  KB_CHECK(probing.LastFailure() == I2cFailure::DataNack);
}

KB_TEST("i2c: a transfer that never completes times out on the port clock, the handle restored") {
  Fresh();
  fake::I2cPresent(kDev, true);
  stm::I2cConfig c = Config();
  c.timeout = kb::Milliseconds{5U};
  stm::I2cController bus{c};
  KB_REQUIRE(bus.EnsureReady() == Status::Ok);
  const std::array<uint8_t, 1> tx{0U};
  fake::ForceLevel(kPortB, 8, 1);
  fake::ForceLevel(kPortB, 9, 1);
  fake::SetTickStep(0U); // the HAL tick stands still: the wait must not depend on it
  fake::Stall("I2C", true);
  KB_CHECK_EQ(bus.Write(kDev, tx), Status::Timeout);
  KB_CHECK(bus.LastFailure() == I2cFailure::Timeout);
  KB_CHECK(FailedWith(bus.LastError(), Status::Timeout, Code(HAL_TIMEOUT, HAL_I2C_ERROR_TIMEOUT)));
  KB_CHECK(fake::CyclesRead() > 0U); // measured on the DWT (or the port clock), not HAL_GetTick
  fake::Stall("I2C", false);
  KB_CHECK_EQ(bus.Write(kDev, tx), Status::Ok); // the restored handle works again
  fake::Fail("i2c_error", HAL_ERROR, HAL_I2C_ERROR_ARLO);
  KB_CHECK_EQ(bus.Write(kDev, tx), Status::BusError);
  KB_CHECK(bus.LastFailure() == I2cFailure::Bus);
  KB_CHECK_EQ(bus.Stats().recoveries, 0U);
  KB_CHECK_EQ(fake::Calls("HAL_I2C_DeInit"), 0U);
}

KB_TEST("i2c: BUSY that stays set with idle lines resets the peripheral (PE), then recovers") {
  Fresh();
  fake::I2cPresent(kDev, true);
  stm::I2cController bus{Config()};
  KB_REQUIRE(bus.EnsureReady() == Status::Ok);
  const std::array<uint8_t, 1> tx{0U};
  fake::ForceLevel(kPortB, 8, 1);
  fake::ForceLevel(kPortB, 9, 1);
  fake::I2cBusyFlag(true); // after an error: BUSY set, both lines high
  KB_CHECK_EQ(bus.Write(kDev, tx), Status::BusError);
  KB_CHECK(bus.LastFailure() == I2cFailure::Bus);
  KB_CHECK_EQ(bus.Stats().recoveries, 1U);
  KB_CHECK_EQ(fake::Calls("HAL_I2C_DeInit"), 1U); // the reset: PE low, SWRST on F4
  KB_CHECK_EQ(fake::Calls("HAL_I2C_Init"), 2U);
  KB_CHECK(fake::IrqEnabled(I2C1_EV_IRQn));
  KB_CHECK_EQ(bus.Write(kDev, tx), Status::Ok); // the next transfer works without the application
}

KB_TEST("i2c: the data registers are flushed and stale CR2 transfer fields cleared each time") {
  Fresh();
  fake::I2cPresent(kDev, true);
  stm::I2cController bus{Config()};
  KB_REQUIRE(bus.EnsureReady() == Status::Ok);
#if !defined(KB_FAKE_STM32F4)
  {
    kb_fake_i2c[1].ISR |= I2C_ISR_TXIS | I2C_ISR_RXNE; // TXIS left latched by an earlier call
    kb_fake_i2c[1].CR2 = I2C_CR2_NBYTES | I2C_CR2_AUTOEND | I2C_CR2_RD_WRN;
    kb_fake_i2c[1].TXDR = 0x77U;
    const std::array<uint8_t, 1> tx{0x01U};
    KB_CHECK_EQ(bus.Write(kDev, tx), Status::Ok);
    KB_CHECK_EQ(fake::I2cRegs(1).TXDR, 0U);               // the dummy byte that clears TXIS
    KB_CHECK((fake::I2cRegs(1).ISR & I2C_ISR_TXE) != 0U); // the flush
    KB_CHECK_EQ(fake::I2cRegs(1).CR2 & (I2C_CR2_NBYTES | I2C_CR2_AUTOEND | I2C_CR2_RD_WRN), 0U);
  }
#endif
}

KB_TEST("i2c: a stuck line is recovered by the nine clocks") {
  Fresh();
  fake::I2cPresent(kDev, true);
  stm::I2cController bus{Config()};
  KB_REQUIRE(bus.EnsureReady() == Status::Ok);
  const std::array<uint8_t, 1> tx{0U};
  fake::ForceLevel(kPortB, 8, 1);
  fake::ForceLevel(kPortB, 9, 0); // SDA held low by a slave
  fake::Fail("HAL_I2C_Master_Seq_Transmit_IT", HAL_BUSY);
  KB_CHECK_EQ(bus.Write(kDev, tx), Status::BusError);
  KB_CHECK(bus.LastFailure() == I2cFailure::BusStuck);
  KB_CHECK_EQ(bus.Stats().recoveries, 1U);
  KB_CHECK_EQ(fake::Calls("HAL_I2C_DeInit"), 1U);
  KB_CHECK_EQ(fake::Calls("HAL_I2C_Init"), 2U); // the peripheral again after the nine clocks
  KB_CHECK_EQ(fake::ModeOf(kPortB, 8), GPIO_MODE_AF_OD);
}

KB_TEST("i2c: RecoverBus clocks SCL until SDA is released, makes a STOP and checks the lines") {
  Fresh();
  stm::I2cConfig c = Config();
  c.auto_recover = false;
  c.internal_pullups = true;
  c.timeout = kb::Milliseconds{2U};
  stm::I2cController bus{c};
  KB_REQUIRE(bus.EnsureReady() == Status::Ok);
  fake::ForceLevel(kPortB, 8, 1);
  fake::ForceLevel(kPortB, 9, 0);
  std::array<uint8_t, 1> rx{};
  fake::I2cPresent(kDev, true);
  fake::Stall("I2C", true);
  KB_CHECK_EQ(bus.Read(kDev, rx), Status::BusError);
  fake::Stall("I2C", false);
  KB_CHECK(bus.LastFailure() == I2cFailure::BusStuck);
  KB_CHECK_EQ(bus.Stats().recoveries, 0U);
  KB_CHECK_EQ(bus.RecoverBus(), Status::BusError); // SDA still low
  KB_CHECK(bus.LastFailure() == I2cFailure::BusStuck);
  KB_CHECK_EQ(fake::PullOf(kPortB, 9), GPIO_PULLUP); // the configured pull stays while recovering
  KB_CHECK(fake::CyclesRead() > 0U); // the half periods were timed on the cycle counter
  fake::ForceLevel(kPortB, 9, -1);
  fake::ForceLevel(kPortB, 8, -1);
  KB_CHECK_EQ(bus.RecoverBus(), Status::Ok);
  KB_CHECK(bus.LastFailure() == I2cFailure::None);
  fake::Fail("HAL_I2C_Init", HAL_ERROR);
  KB_CHECK_EQ(bus.RecoverBus(), Status::Fault);
  KB_CHECK_EQ(bus.Stats().recoveries, 3U);
}

KB_TEST("i2c: a call while the controller is held is Busy") {
  Fresh();
  fake::I2cPresent(kDev, true);
  stm::I2cController bus{Config()};
  KB_REQUIRE(bus.EnsureReady() == Status::Ok);
  const std::array<uint8_t, 1> tx{0U};
  fake::Hook("HAL_I2C_Master_Seq_Transmit_IT", ReenterWrite, &bus);
  KB_CHECK_EQ(bus.Write(kDev, tx), Status::Ok);
  fake::Hook(
      "HAL_I2C_DeInit",
      [](void* context) {
        KB_CHECK_EQ(static_cast<stm::I2cController*>(context)->RecoverBus(), Status::Busy);
      },
      &bus);
  KB_CHECK_EQ(bus.RecoverBus(), Status::Ok);
  KB_CHECK_EQ(bus.Stats().lock_timeouts, 2U);
}

KB_TEST("i2c: one controller per instance; free again after Release or a failed start") {
  Fresh();
  stm::I2cController first{Config()};
  stm::I2cController second{Config()};
  fake::Fail("HAL_I2C_Init", HAL_ERROR);
  KB_CHECK_EQ(first.EnsureReady(), Status::Fault);
  KB_REQUIRE(second.EnsureReady() == Status::Ok); // the failed start left the instance free
  KB_CHECK(fake::IrqEnabled(I2C1_EV_IRQn) && fake::IrqEnabled(I2C1_ER_IRQn));
  KB_CHECK_EQ(first.EnsureReady(), Status::Busy);
  KB_CHECK(FailedWith(first.LastError(), Status::Busy, stm::port_code::kOwned));
  KB_CHECK_EQ(second.Release(), Status::Ok);
  KB_CHECK(!fake::IrqEnabled(I2C1_EV_IRQn));
  KB_CHECK_EQ(first.EnsureReady(), Status::Ok);
  stm::HandleI2cIrq(1U, false); // the vectors reach the HAL's handlers of the open instance
  stm::HandleI2cIrq(1U, true);
  stm::HandleI2cIrq(2U, false); // not open: nothing
  stm::HandleI2cIrq(9U, false);
  KB_CHECK_EQ(fake::Calls("HAL_I2C_EV_IRQHandler"), 1U);
  KB_CHECK_EQ(fake::Calls("HAL_I2C_ER_IRQHandler"), 1U);
}

KB_TEST("i2c: a failed re-initialisation in recovery leaves the controller not ready") {
  Fresh();
  fake::I2cPresent(kDev, true);
  stm::I2cController bus{Config()};
  KB_REQUIRE(bus.EnsureReady() == Status::Ok);
  fake::ForceLevel(kPortB, 8, 1);
  fake::ForceLevel(kPortB, 9, 0);        // SDA held low: a failure starts the recovery
  fake::Fail("HAL_I2C_Init", HAL_ERROR); // the re-initialisation after the nine clocks fails
  const std::array<uint8_t, 1> tx{0U};
  fake::Fail("HAL_I2C_Master_Seq_Transmit_IT", HAL_BUSY);
  KB_CHECK_EQ(bus.Write(kDev, tx), Status::BusError);
  KB_CHECK_EQ(bus.Write(kDev, tx), Status::NotReady); // never a transfer on a null handle
  fake::ForceLevel(kPortB, 9, -1);
  fake::ForceLevel(kPortB, 8, -1);
  KB_CHECK_EQ(bus.EnsureReady(), Status::Ok); // started again by its owner
  KB_CHECK_EQ(bus.Write(kDev, tx), Status::Ok);
}

KB_TEST("i2c: a recovery that finds SCL held low is counted apart and sends no clocks") {
  Fresh();
  stm::I2cConfig c = Config();
  c.auto_recover = false;
  stm::I2cController bus{c};
  KB_REQUIRE(bus.EnsureReady() == Status::Ok);
  fake::ForceLevel(kPortB, 8, 0); // SCL held low by a device
  fake::ForceLevel(kPortB, 9, 1);
  const uint32_t writes = fake::Calls("HAL_GPIO_WritePin");
  KB_CHECK_EQ(bus.RecoverBus(), Status::BusError);
  KB_CHECK_EQ(bus.Stats().scl_held, 1U);
  KB_CHECK(fake::Calls("HAL_GPIO_WritePin") - writes <= 4U); // the pins set and a STOP, no clocks
}

KB_TEST("i2c: an interrupt of an instance the port does not own is switched off") {
  Fresh();
  HAL_NVIC_EnableIRQ(I2C2_EV_IRQn); // as an application that owns I2C2 would
  stm::HandleI2cIrq(2U, false);
  KB_CHECK(!fake::IrqEnabled(I2C2_EV_IRQn)); // no endless re-entry
}

KB_TEST("i2c: SCL that rises late (or a short clock stretch) is waited for, then the bus clocked") {
  Fresh();
  stm::I2cConfig c = Config();
  c.auto_recover = false;
  stm::I2cController bus{c};
  KB_REQUIRE(bus.EnsureReady() == Status::Ok);
  fake::ForceLevelFor(kPortB, 8, 0, 50U); // SCL low for its first 50 reads, then released
  fake::ForceLevelFor(kPortB, 9, 0, 3U);  // SDA held by a slave for a few clocks
  const uint32_t writes = fake::Calls("HAL_GPIO_WritePin");
  KB_CHECK_EQ(bus.RecoverBus(), Status::Ok);
  KB_CHECK_EQ(bus.Stats().scl_held, 0U);                    // not given up on the first low read
  KB_CHECK(fake::Calls("HAL_GPIO_WritePin") - writes > 4U); // the clocks were sent
}

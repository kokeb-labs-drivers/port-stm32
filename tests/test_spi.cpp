// SpiBus and SpiDevice over the fake HAL SPI: the prescaler, chip select around frames, per-device
// mode and clock, chunks, timeouts with abort and re-initialisation, and every refusal. Test code.
// SPDX-License-Identifier: MIT Copyright (c) 2026 The port-stm32 authors
#include <vector>

#include "support.hpp"

namespace {
constexpr int kPortA = 0;
constexpr int kPortB = 1;

stm::SpiBusConfig BusConfig() {
  stm::SpiBusConfig c{};
  c.instance = 1U;
  c.sck = PinId{GpioPort::A, 5U, 5U};
  c.miso = PinId{GpioPort::A, 6U, 5U};
  c.mosi = PinId{GpioPort::A, 7U, 5U};
  return c;
}

stm::SpiDeviceConfig DevConfig() {
  stm::SpiDeviceConfig c{};
  c.cs = PinId{GpioPort::B, 6U, 0U};
  c.mode = 3U;
  c.clock_hz = 4000000U;
  return c;
}

void CheckCsLow(void*) {
  KB_CHECK_EQ(fake::Driven(1, 6), 0); // chip select held low during the frame
}
} // namespace

KB_TEST("spi prescaler: the fastest SCK at or below the asked clock") {
  bool ok = false;
  KB_CHECK_EQ(stm::SpiPrescalerExponent(160000000U, 80000000U, ok), 0U);
  KB_CHECK(ok);
  KB_CHECK_EQ(stm::SpiPrescalerExponent(160000000U, 4000000U, ok), 5U); // 160 / 64 = 2.5 MHz
  KB_CHECK(ok);
  KB_CHECK_EQ(stm::SpiPrescalerExponent(160000000U, 625000U, ok), 7U);
  KB_CHECK(ok);
  static_cast<void>(stm::SpiPrescalerExponent(160000000U, 600000U, ok)); // 256 is not enough
  KB_CHECK(!ok);
  static_cast<void>(stm::SpiPrescalerExponent(160000000U, 0U, ok));
  KB_CHECK(!ok);
  static_cast<void>(stm::SpiPrescalerExponent(0U, 1000U, ok));
  KB_CHECK(!ok);
}

KB_TEST("spi: bad bus and device configurations are refused") {
  Fresh();
  SystemCoreClock = 640000000U; // a ceiling of 16 MHz (FIFO) or 10 MHz (F4)
  stm::SpiBusConfig b = BusConfig();
  b.instance = 9U;
  stm::SpiBus none{b};
  KB_CHECK_EQ(none.EnsureReady(), Status::InvalidArgument);
  b = BusConfig();
  b.mosi = PinId{};
  b.miso = PinId{};
  stm::SpiBus no_data{b};
  KB_CHECK_EQ(no_data.EnsureReady(), Status::InvalidArgument);
  b = BusConfig();
  b.miso.pin = 16U;
  stm::SpiBus bad_pin{b};
  KB_CHECK_EQ(bad_pin.EnsureReady(), Status::InvalidArgument);
  stm::SpiBus bus{BusConfig()};
  const auto status = [&bus](stm::SpiDeviceConfig c) {
    stm::SpiDevice dev{bus, c};
    return dev.EnsureReady();
  };
  stm::SpiDeviceConfig d = DevConfig();
  d.mode = 4U;
  KB_CHECK_EQ(status(d), Status::InvalidArgument);
  d = DevConfig();
  d.cs = PinId{};
  KB_CHECK_EQ(status(d), Status::InvalidArgument);
  d = DevConfig();
  d.clock_hz = 0U;
  KB_CHECK_EQ(status(d), Status::InvalidArgument);
  d = DevConfig();
  d.timeout = kb::Milliseconds{0U};
  KB_CHECK_EQ(status(d), Status::InvalidArgument);
  fake::Fail("HAL_SPI_Init", HAL_ERROR);
  KB_CHECK_EQ(status(DevConfig()), Status::NotReady);
  KB_CHECK(FailedWith(bus.LastError(), Status::Fault, Code(HAL_ERROR)));
  d = DevConfig();
  d.clock_hz = 1000U; // below what a prescaler of 256 reaches
  KB_CHECK_EQ(status(d), Status::InvalidArgument);
}

KB_TEST("spi: a frame drives chip select around it at the device's mode and clock") {
  Fresh();
  SystemCoreClock = 640000000U; // a ceiling of 16 MHz (FIFO) or 10 MHz (F4)
  stm::SpiBus bus{BusConfig()};
  stm::SpiDevice dev{bus, DevConfig()};
  const std::array<uint8_t, 5> tx{0x80U, 1U, 2U, 3U, 4U};
  std::array<uint8_t, 5> rx{};
  KB_CHECK_EQ(dev.Transfer(tx, rx), Status::NotReady);
  KB_REQUIRE(dev.EnsureReady() == Status::Ok);
  KB_CHECK_EQ(dev.EnsureReady(), Status::Ok);
  KB_CHECK(bus.IsReady());
  KB_CHECK_EQ(bus.KernelClockHz(), 160000000U);
  KB_CHECK_EQ(dev.ActualClockHz(), 2500000U);
  KB_CHECK_EQ(fake::Driven(kPortB, 6), 1); // released
  KB_CHECK_EQ(fake::ModeOf(kPortA, 5), GPIO_MODE_AF_PP);
  const std::array<uint8_t, 5> reply{0x0FU, 0x11U, 0x22U, 0x33U, 0x44U};
  fake::SpiReply(reply);
  fake::Hook("HAL_SPI_TransmitReceive", CheckCsLow, nullptr);
  KB_CHECK_EQ(dev.Transfer(tx, rx), Status::Ok);
  KB_CHECK_EQ(rx[4], 0x44U);
  KB_CHECK_EQ(fake::Driven(kPortB, 6), 1);
  KB_CHECK_EQ(fake::SpiInit().CLKPolarity, SPI_POLARITY_HIGH);
  KB_CHECK_EQ(fake::SpiInit().CLKPhase, SPI_PHASE_2EDGE);
  KB_CHECK_EQ(fake::SpiInit().BaudRatePrescaler, SPI_BAUDRATEPRESCALER_64);
  const uint32_t inits = fake::Calls("HAL_SPI_Init");
  KB_CHECK_EQ(dev.Transfer(tx, rx), Status::Ok);
  KB_CHECK_EQ(fake::Calls("HAL_SPI_Init"), inits); // same device: no re-initialisation
  KB_CHECK_EQ(dev.Stats().transfers, 2U);
  KB_CHECK(dev.LastError().IsOk());
  KB_CHECK(bus.LastError().IsOk());
  KB_CHECK(bus.Config().instance == 1U);
}

KB_TEST("spi: two devices with different modes switch the peripheral; write-only and read-only "
        "frames") {
  Fresh();
  SystemCoreClock = 640000000U; // a ceiling of 16 MHz (FIFO) or 10 MHz (F4)
  stm::SpiBus bus{BusConfig()};
  stm::SpiDevice a{bus, DevConfig()};
  stm::SpiDeviceConfig other = DevConfig();
  other.cs = PinId{GpioPort::B, 7U, 0U};
  other.mode = 0U;
  stm::SpiDevice b{bus, other};
  KB_REQUIRE(a.EnsureReady() == Status::Ok);
  KB_REQUIRE(b.EnsureReady() == Status::Ok);
  const std::array<uint8_t, 2> tx{0xAAU, 0xBBU};
  KB_CHECK_EQ(a.Transfer(tx, std::span<uint8_t>{}), Status::Ok);
  KB_CHECK_EQ(fake::SpiInit().CLKPolarity, SPI_POLARITY_HIGH);
  std::array<uint8_t, 2> rx{0x55U, 0x55U};
  KB_CHECK_EQ(b.Transfer(std::span<const uint8_t>{}, rx), Status::Ok);
  KB_CHECK_EQ(fake::SpiInit().CLKPolarity, SPI_POLARITY_LOW);
  KB_CHECK_EQ(fake::SpiSent()[2], 0x00U); // zeros clocked out for the read
  KB_CHECK_EQ(fake::Calls("HAL_SPI_Transmit_IT"), 1U);
  // A read-only frame is full duplex on every family (never the H7 simplex receive).
  KB_CHECK_EQ(fake::Calls("HAL_SPI_TransmitReceive_IT"), 1U);
  KB_CHECK_EQ(a.Transfer(tx, std::span<uint8_t>{}), Status::Ok);
  KB_CHECK_EQ(fake::SpiInit().CLKPolarity, SPI_POLARITY_HIGH);
  stm::SpiDevice same_cs{bus, DevConfig()};
  KB_CHECK_EQ(same_cs.EnsureReady(), Status::Busy); // the chip select pin belongs to `a`
}

KB_TEST("spi: frames above 65535 bytes are chunked; spans are checked") {
  Fresh();
  SystemCoreClock = 640000000U; // a ceiling of 16 MHz (FIFO) or 10 MHz (F4)
  stm::SpiBus bus{BusConfig()};
  stm::SpiDevice dev{bus, DevConfig()};
  KB_REQUIRE(dev.EnsureReady() == Status::Ok);
  std::vector<uint8_t> tx(70000U, 0x5AU);
  KB_CHECK_EQ(dev.Transfer(tx, std::span<uint8_t>{}), Status::Ok);
  KB_CHECK_EQ(fake::SpiTransfers(), 2U);
  const std::array<uint8_t, 2> two{};
  std::array<uint8_t, 3> three{};
  KB_CHECK_EQ(dev.Transfer(two, three), Status::InvalidArgument);
  KB_CHECK_EQ(dev.Transfer(std::span<const uint8_t>{}, std::span<uint8_t>{}),
              Status::InvalidArgument);
}

KB_TEST("spi: a timeout aborts and re-initialises before the next frame; errors are BusError") {
  Fresh();
  SystemCoreClock = 640000000U; // a ceiling of 16 MHz (FIFO) or 10 MHz (F4)
  stm::SpiBus bus{BusConfig()};
  stm::SpiDevice dev{bus, DevConfig()};
  KB_REQUIRE(dev.EnsureReady() == Status::Ok);
  const std::array<uint8_t, 2> tx{1U, 2U};
  fake::SetTickStep(0U);    // the HAL tick stands still: the wait is on the port's clock
  fake::Stall("SPI", true); // the transfer's interrupt never ends it
  KB_CHECK_EQ(dev.Transfer(tx, std::span<uint8_t>{}), Status::Timeout);
  fake::Stall("SPI", false);
  KB_CHECK_EQ(fake::Calls("HAL_SPI_Abort"), 1U);
  KB_CHECK_EQ(dev.Stats().rebuilds, 1U);
  KB_CHECK_EQ(fake::Driven(kPortB, 6), 1); // released after the failure too
  const uint32_t inits = fake::Calls("HAL_SPI_Init");
  KB_CHECK_EQ(dev.Transfer(tx, std::span<uint8_t>{}), Status::Ok);
  KB_CHECK_EQ(fake::Calls("HAL_SPI_Init"), inits + 1U);
  fake::Stall("SPI", true);
  fake::Fail("HAL_SPI_Abort", HAL_ERROR); // the abort failing too is reported in ErrorCode
  KB_CHECK_EQ(dev.Transfer(tx, std::span<uint8_t>{}), Status::Timeout);
  fake::Stall("SPI", false);
  KB_CHECK(FailedWith(dev.LastError(), Status::Timeout, Code(HAL_TIMEOUT, HAL_SPI_ERROR_ABORT)));
  KB_CHECK_EQ(dev.Transfer(tx, std::span<uint8_t>{}), Status::Ok);
  fake::Fail("spi_error", HAL_ERROR, HAL_SPI_ERROR_OVR); // ended by the error callback
  KB_CHECK_EQ(dev.Transfer(tx, std::span<uint8_t>{}), Status::BusError);
  KB_CHECK(FailedWith(dev.LastError(), Status::BusError, Code(HAL_ERROR, HAL_SPI_ERROR_OVR)));
  fake::Fail("HAL_SPI_DeInit", HAL_BUSY); // the next selection fails
  stm::SpiDeviceConfig other = DevConfig();
  other.cs = PinId{GpioPort::B, 7U, 0U};
  other.mode = 0U;
  stm::SpiDevice b{bus, other};
  KB_REQUIRE(b.EnsureReady() == Status::Ok);
  KB_CHECK_EQ(b.Transfer(tx, std::span<uint8_t>{}), Status::Busy);
  KB_CHECK_EQ(dev.Stats().failures, 3U);
  fake::Fail("HAL_SPI_Transmit_IT", HAL_BUSY); // the start itself refused
  KB_CHECK_EQ(dev.Transfer(tx, std::span<uint8_t>{}), Status::Busy);
  stm::HandleSpiIrq(1U); // the vector reaches the HAL's handler of the open bus
  stm::HandleSpiIrq(3U);
  stm::HandleSpiIrq(9U);
  KB_CHECK_EQ(fake::Calls("HAL_SPI_IRQHandler"), 1U);
}

KB_TEST("spi: SetClockHz before and after EnsureReady; Release") {
  Fresh();
  SystemCoreClock = 640000000U; // a ceiling of 16 MHz (FIFO) or 10 MHz (F4)
  stm::SpiBus bus{BusConfig()};
  stm::SpiDevice dev{bus, DevConfig()};
  KB_CHECK_EQ(dev.SetClockHz(0U), Status::InvalidArgument);
  KB_CHECK_EQ(dev.SetClockHz(1000000U), Status::Ok);
  KB_REQUIRE(dev.EnsureReady() == Status::Ok);
  KB_CHECK_EQ(dev.ActualClockHz(), 625000U);
  KB_CHECK_EQ(dev.SetClockHz(5000000U), Status::Ok);
  KB_CHECK_EQ(dev.ActualClockHz(), 5000000U);
  KB_CHECK(dev.Config().clock_hz == 5000000U);
  KB_CHECK_EQ(dev.SetClockHz(100U), Status::InvalidArgument);
  const std::array<uint8_t, 1> tx{1U};
  KB_CHECK_EQ(dev.Transfer(tx, std::span<uint8_t>{}), Status::Ok);
  KB_CHECK_EQ(fake::SpiInit().BaudRatePrescaler, SPI_BAUDRATEPRESCALER_32);
  KB_CHECK_EQ(dev.Release(), Status::Ok);
  KB_CHECK_EQ(fake::ModeOf(kPortB, 6), GPIO_MODE_ANALOG);
  fake::Fail("HAL_SPI_DeInit", HAL_ERROR);
  KB_CHECK_EQ(bus.Release(), Status::Fault);
}

KB_TEST("spi: a held bus makes a transfer Busy") {
  Fresh();
  SystemCoreClock = 640000000U; // a ceiling of 16 MHz (FIFO) or 10 MHz (F4)
  stm::SpiBus bus{BusConfig()};
  stm::SpiDevice dev{bus, DevConfig()};
  KB_REQUIRE(dev.EnsureReady() == Status::Ok);
  KB_REQUIRE(bus.TryLockFor(kb::Milliseconds{1U}));
  const std::array<uint8_t, 1> tx{1U};
  KB_CHECK_EQ(dev.Transfer(tx, std::span<uint8_t>{}), Status::Busy);
  KB_CHECK_EQ(dev.Stats().lock_timeouts, 1U);
  bus.Unlock();
  KB_CHECK_EQ(dev.Transfer(tx, std::span<uint8_t>{}), Status::Ok);
}

KB_TEST("spi: one bus object per instance; free again after Release or a failed start") {
  Fresh();
  SystemCoreClock = 640000000U; // a ceiling of 16 MHz (FIFO) or 10 MHz (F4)
  stm::SpiBus first{BusConfig()};
  stm::SpiBus second{BusConfig()};
  fake::Fail("HAL_SPI_Init", HAL_ERROR);
  KB_CHECK_EQ(first.EnsureReady(), Status::Fault);
  KB_REQUIRE(second.EnsureReady() == Status::Ok);
  KB_CHECK_EQ(first.EnsureReady(), Status::Busy);
  KB_CHECK_EQ(second.Release(), Status::Ok);
  KB_CHECK_EQ(first.EnsureReady(), Status::Ok);
}

KB_TEST("spi: the SCK of interrupt transfers is limited to the family's ceiling") {
  static_assert(stm::SpiInterruptCeilingHz(168000000U, false) == 2625000U); // F4: core / 64
  static_assert(stm::SpiInterruptCeilingHz(170000000U, true) == 4250000U);  // G4: core / 40
  static_assert(stm::SpiInterruptCeilingHz(480000000U, true) == 12000000U); // H7: core / 40
  Fresh();
  SystemCoreClock = 160000000U;
  stm::SpiBus bus{BusConfig()};
  stm::SpiDeviceConfig fast = DevConfig();
  fast.clock_hz = 40000000U; // above the ceiling: the prescaler is chosen for the ceiling
  stm::SpiDevice dev{bus, fast};
  KB_REQUIRE(dev.EnsureReady() == Status::Ok);
  const uint32_t ceiling = kF4 ? 2500000U : 4000000U;
  KB_CHECK_EQ(bus.InterruptCeilingHz(), ceiling);
  KB_CHECK(dev.ActualClockHz() <= ceiling);
  KB_CHECK_EQ(dev.ActualClockHz(), 2500000U);   // 160 MHz / 64, at or below either ceiling
  KB_CHECK(dev.Config().clock_hz == 40000000U); // what was asked is kept
  KB_CHECK_EQ(dev.SetClockHz(80000000U), Status::Ok);
  KB_CHECK(dev.ActualClockHz() <= ceiling);
  KB_CHECK_EQ(dev.Release(), Status::Ok);
  KB_CHECK_EQ(bus.Release(), Status::Ok);
  // A core so slow that even the largest prescaler is above the ceiling: refused.
  SystemCoreClock = 4000000U;
  stm::SpiBus slow_bus{BusConfig()};
  stm::SpiDevice slow{slow_bus, DevConfig()};
  KB_CHECK_EQ(slow.EnsureReady(), Status::InvalidArgument);
}

KB_TEST("spi: an abort makes every device's next frame initialise the peripheral again") {
  Fresh();
  SystemCoreClock = 640000000U;
  stm::SpiBus bus{BusConfig()};
  stm::SpiDevice a{bus, DevConfig()};
  stm::SpiDeviceConfig other = DevConfig();
  other.cs = PinId{GpioPort::B, 7U, 0U}; // the same mode and clock as `a`
  stm::SpiDevice b{bus, other};
  KB_REQUIRE(a.EnsureReady() == Status::Ok);
  KB_REQUIRE(b.EnsureReady() == Status::Ok);
  const std::array<uint8_t, 1> tx{1U};
  KB_CHECK_EQ(a.Transfer(tx, std::span<uint8_t>{}), Status::Ok);
  fake::Stall("SPI", true);
  KB_CHECK_EQ(a.Transfer(tx, std::span<uint8_t>{}), Status::Timeout);
  fake::Stall("SPI", false);
  const uint32_t inits = fake::Calls("HAL_SPI_Init");
  KB_CHECK_EQ(b.Transfer(tx, std::span<uint8_t>{}), Status::Ok); // b, not a: still re-initialised
  KB_CHECK_EQ(fake::Calls("HAL_SPI_Init"), inits + 1U);
}

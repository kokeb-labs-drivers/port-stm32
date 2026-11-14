// Uart over the fake HAL UART: framing, the interrupt-fed ring, latched errors, RS-485 direction,
// baud changes, the bounded waits and every refusal. Test code. SPDX-License-Identifier: MIT
// Copyright (c) 2026 The port-stm32 authors
#include <vector>

#include "support.hpp"

namespace {
constexpr int kPortA = 0;

stm::UartConfig Config() {
  stm::UartConfig c{};
  c.instance = 2U;
  c.tx = PinId{GpioPort::A, 2U, 7U};
  c.rx = PinId{GpioPort::A, 3U, 7U};
  c.baud = 115200U;
  return c;
}

USART_TypeDef& Regs(uint8_t instance) {
  return kb_fake_usart[(instance == stm::kLpuart1) ? 11U : instance];
}

// One received byte, as the peripheral presents it to the interrupt (optionally with error flags).
void Receive(uint8_t instance, uint8_t byte, uint32_t errors = 0U) {
  USART_TypeDef& u = Regs(instance);
#if defined(KB_FAKE_STM32F4)
  u.SR = USART_SR_RXNE | errors;
  u.DR = byte;
#else
  u.ISR = USART_ISR_RXNE_RXFNE | errors;
  u.RDR = byte;
#endif
  stm::HandleUartIrq(instance);
#if defined(KB_FAKE_STM32F4)
  u.SR = 0U;
#else
  u.ISR = 0U;
#endif
}

void SetTxComplete(uint8_t instance, bool done) {
#if defined(KB_FAKE_STM32F4)
  Regs(instance).SR = done ? USART_SR_TC : 0U;
#else
  Regs(instance).ISR = done ? USART_ISR_TC : 0U;
#endif
}

#if defined(KB_FAKE_STM32F4)
constexpr uint32_t kOre = USART_SR_ORE;
constexpr uint32_t kFe = USART_SR_FE;
constexpr uint32_t kPe = USART_SR_PE;
constexpr uint32_t kNe = USART_SR_NE;
#else
constexpr uint32_t kOre = USART_ISR_ORE;
constexpr uint32_t kFe = USART_ISR_FE;
constexpr uint32_t kPe = USART_ISR_PE;
constexpr uint32_t kNe = USART_ISR_NE;
#endif

void ReenterRead(void* context) {
  std::array<uint8_t, 1> rx{};
  const kb::Result<std::size_t> r =
      static_cast<stm::Uart*>(context)->Read(rx, kb::Milliseconds{1U});
  KB_CHECK(!r.IsOk() && (r.Error() == Status::Busy));
}
} // namespace

KB_TEST("uart: a bad configuration is refused") {
  Fresh();
  const auto refused = [](stm::UartConfig c) {
    stm::Uart u{c};
    return u.EnsureReady() == Status::InvalidArgument;
  };
  stm::UartConfig c = Config();
  c.instance = 9U;
  KB_CHECK(refused(c));
  c = Config();
  c.tx = PinId{};
  KB_CHECK(refused(c));
  c = Config();
  c.de.pin = 17U;
  KB_CHECK(refused(c));
  c = Config();
  c.baud = 0U;
  KB_CHECK(refused(c));
  c = Config();
  c.baud = 20000000U;
  KB_CHECK(refused(c));
  c = Config();
  c.data_bits = 6U;
  KB_CHECK(refused(c));
  c = Config();
  c.data_bits = 9U;
  KB_CHECK(refused(c));
  c = Config();
  c.write_timeout = kb::Milliseconds{0U};
  KB_CHECK(refused(c));
}

KB_TEST("uart: EnsureReady sets pins, frame, the receive interrupt and the NVIC; one owner") {
  Fresh();
  stm::UartConfig c = Config();
  c.parity = stm::UartParity::Even;
  c.stop_bits = stm::UartStopBits::Two;
  c.irq_priority = 7U;
  {
    stm::Uart u{c};
    KB_REQUIRE(u.EnsureReady() == Status::Ok);
    KB_CHECK_EQ(u.EnsureReady(), Status::Ok);
    KB_CHECK(fake::ClockEnabled("USART2"));
    KB_CHECK_EQ(fake::ModeOf(kPortA, 2), GPIO_MODE_AF_PP);
    KB_CHECK_EQ(fake::AlternateOf(kPortA, 3), 7U);
    KB_CHECK_EQ(fake::UartInit().WordLength, UART_WORDLENGTH_9B); // 8 data bits and the parity bit
    KB_CHECK_EQ(fake::UartInit().Parity, UART_PARITY_EVEN);
    KB_CHECK_EQ(fake::UartInit().StopBits, UART_STOPBITS_2);
    KB_CHECK(fake::IrqEnabled(USART2_IRQn));
    KB_CHECK_EQ(fake::IrqPriority(USART2_IRQn), 7U);
    stm::Uart second{Config()};
    KB_CHECK_EQ(second.EnsureReady(), Status::Busy);
  }
  KB_CHECK(!fake::IrqEnabled(USART2_IRQn));
  c = Config();
  c.parity = stm::UartParity::Odd;
  c.data_bits = 7U;
  stm::Uart odd{c};
  KB_REQUIRE(odd.EnsureReady() == Status::Ok);
  KB_CHECK_EQ(fake::UartInit().Parity, UART_PARITY_ODD);
  KB_CHECK_EQ(fake::UartInit().WordLength, UART_WORDLENGTH_8B);
}

KB_TEST("uart: HAL failures while initialising are reported") {
  Fresh();
  stm::Uart u{Config()};
  fake::Fail("HAL_UART_Init", HAL_ERROR, 0x10U);
  KB_CHECK_EQ(u.EnsureReady(), Status::Fault);
  KB_CHECK(FailedWith(u.LastError(), Status::Fault, Code(HAL_ERROR, 0x10U)));
  KB_CHECK_EQ(u.EnsureReady(), Status::Ok);
  fake::Fail("HAL_UART_DeInit", HAL_BUSY);
  KB_CHECK_EQ(u.Release(), Status::Busy);
  KB_CHECK_EQ(u.Release(), Status::Ok);
}

KB_TEST("uart: calls before EnsureReady are NotReady; empty spans are refused") {
  Fresh();
  stm::Uart u{Config()};
  const std::array<uint8_t, 1> tx{1U};
  std::array<uint8_t, 1> rx{};
  KB_CHECK_EQ(u.Write(tx), Status::NotReady);
  KB_CHECK(
      FailedWith(u.Read(rx, kb::Milliseconds{1U}), Status::NotReady, stm::port_code::kNotReady));
  KB_CHECK_EQ(u.FlushInput(), Status::NotReady);
  KB_CHECK_EQ(u.WaitTxDone(kb::Milliseconds{1U}), Status::NotReady);
  KB_CHECK_EQ(u.Write(std::span<const uint8_t>{}), Status::InvalidArgument);
  KB_CHECK(FailedWith(u.Read(std::span<uint8_t>{}, kb::Milliseconds{1U}), Status::InvalidArgument,
                      stm::port_code::kInvalidArgument));
}

KB_TEST("uart: Write sends through the HAL; failures are reported") {
  Fresh();
  stm::Uart u{Config()};
  KB_REQUIRE(u.EnsureReady() == Status::Ok);
  const std::array<uint8_t, 3> tx{1U, 2U, 3U};
  KB_CHECK_EQ(u.Write(tx), Status::Ok);
  KB_CHECK_EQ(fake::UartSent().size(), 3U);
  KB_CHECK_EQ(u.Stats().bytes_written, 3U);
  fake::Fail("HAL_UART_Transmit", HAL_TIMEOUT);
  KB_CHECK_EQ(u.Write(tx), Status::Timeout);
  static std::array<uint8_t, 70000U> big{}; // above the HAL's uint16_t length: two calls
  const uint32_t before = fake::Calls("HAL_UART_Transmit");
  KB_CHECK_EQ(u.Write(big), Status::Ok);
  KB_CHECK_EQ(fake::Calls("HAL_UART_Transmit") - before, 2U);
  KB_CHECK_EQ(fake::UartSent().size(), 3U + 70000U);
  fake::Fail("HAL_UART_Transmit", HAL_TIMEOUT); // the first chunk fails: the second is not sent
  KB_CHECK_EQ(u.Write(big), Status::Timeout);
  KB_CHECK_EQ(fake::Calls("HAL_UART_Transmit") - before, 3U);
}

KB_TEST("uart: RS-485 drives DE high while sending and low after the last stop bit") {
  Fresh();
  stm::UartConfig c = Config();
  c.de = PinId{GpioPort::A, 1U, 0U};
  stm::Uart u{c};
  KB_REQUIRE(u.EnsureReady() == Status::Ok);
  KB_CHECK_EQ(fake::Driven(kPortA, 1), 0);
  const std::array<uint8_t, 2> tx{1U, 2U};
  fake::Hook("HAL_UART_Transmit", [](void*) { KB_CHECK_EQ(fake::Driven(0, 1), 1); }, nullptr);
  SetTxComplete(2U, true);
  KB_CHECK_EQ(u.Write(tx), Status::Ok);
  KB_CHECK_EQ(fake::Driven(kPortA, 1), 0);
  SetTxComplete(2U, false); // the last byte never leaves: DE is dropped after the timeout anyway
  KB_CHECK_EQ(u.Write(tx), Status::Timeout);
  KB_CHECK_EQ(fake::Driven(kPortA, 1), 0);
  KB_CHECK_EQ(u.Release(), Status::Ok);
  KB_CHECK_EQ(fake::ModeOf(kPortA, 1), GPIO_MODE_ANALOG);
}

KB_TEST("uart: bytes from the interrupt are read; Read waits for the first only") {
  Fresh();
  stm::Uart u{Config()};
  KB_REQUIRE(u.EnsureReady() == Status::Ok);
  Receive(2U, 0x10U);
  Receive(2U, 0x20U);
  Receive(2U, 0x30U);
  std::array<uint8_t, 8> rx{};
  const kb::Result<std::size_t> r = u.Read(rx, kb::Milliseconds{25U});
  KB_REQUIRE(r.IsOk());
  KB_CHECK_EQ(r.Value(), 3U);
  KB_CHECK_EQ(rx[2], 0x30U);
  KB_CHECK_EQ(u.Stats().bytes_read, 3U);
  const uint32_t before = fake::Ticks();
  KB_CHECK(FailedWith(u.Read(rx, kb::Milliseconds{7U}), Status::Timeout, stm::hal::kTimeout));
  KB_CHECK(fake::Ticks() - before >= 7U);
  stm::HandleUartIrq(2U); // an interrupt with nothing pending changes nothing
  stm::HandleUartIrq(9U); // an instance without an owner
  KB_CHECK(FailedWith(u.Read(rx, kb::Milliseconds{1U}), Status::Timeout, stm::hal::kTimeout));
}

KB_TEST("uart: receive errors are reported once, then the data; corrupt bytes are dropped") {
  Fresh();
  stm::Uart u{Config()};
  KB_REQUIRE(u.EnsureReady() == Status::Ok);
  Receive(2U, 0xAAU, kOre);
  std::array<uint8_t, 4> rx{};
  KB_CHECK(
      FailedWith(u.Read(rx, kb::Milliseconds{1U}), Status::Overflow, stm::uart_flags::kOverrun));
  KB_CHECK(u.Read(rx, kb::Milliseconds{1U}).ValueOr(0U) == 1U); // an overrun keeps the byte read
  Receive(2U, 0xBBU, kFe);
  Receive(2U, 0xCCU, kPe | kNe);
  KB_CHECK(FailedWith(
      u.Read(rx, kb::Milliseconds{1U}), Status::BusError,
      stm::uart_flags::kFrameError | stm::uart_flags::kParityError | stm::uart_flags::kNoise));
  KB_CHECK(FailedWith(u.Read(rx, kb::Milliseconds{1U}), Status::Timeout, stm::hal::kTimeout));
  for (uint32_t i = 0U; i < (stm::kUartRxBytes + 1U); ++i) {
    Receive(2U, static_cast<uint8_t>(i));
  }
  KB_CHECK(
      FailedWith(u.Read(rx, kb::Milliseconds{1U}), Status::Overflow, stm::uart_flags::kRingFull));
  KB_CHECK_EQ(u.Stats().errors, 3U);
#if !defined(KB_FAKE_STM32F4)
  KB_CHECK_EQ(Regs(2U).ICR, USART_ICR_ORECF | USART_ICR_FECF | USART_ICR_PECF | USART_ICR_NECF);
#endif
}

KB_TEST("uart: FlushInput drops data and errors; SetBaud; WaitTxDone") {
  Fresh();
  stm::Uart u{Config()};
  KB_CHECK_EQ(u.SetBaud(0U), Status::InvalidArgument);
  KB_CHECK_EQ(u.SetBaud(57600U), Status::Ok);
  KB_REQUIRE(u.EnsureReady() == Status::Ok);
  KB_CHECK_EQ(fake::UartInit().BaudRate, 57600U);
  Receive(2U, 1U, kFe);
  Receive(2U, 2U);
  KB_CHECK_EQ(u.FlushInput(), Status::Ok);
  std::array<uint8_t, 4> rx{};
  KB_CHECK(FailedWith(u.Read(rx, kb::Milliseconds{1U}), Status::Timeout, stm::hal::kTimeout));
  KB_CHECK_EQ(u.SetBaud(230400U), Status::Ok);
  KB_CHECK_EQ(fake::UartInit().BaudRate, 230400U);
  KB_CHECK(u.Config().baud == 230400U);
  fake::Fail("HAL_UART_Init", HAL_ERROR);
  KB_CHECK_EQ(u.SetBaud(9600U), Status::Fault);
  KB_CHECK(u.Config().baud == 230400U);
  SetTxComplete(2U, true);
  KB_CHECK_EQ(u.WaitTxDone(kb::Milliseconds{5U}), Status::Ok);
  SetTxComplete(2U, false);
  KB_CHECK_EQ(u.WaitTxDone(kb::Milliseconds{5U}), Status::Timeout);
}

KB_TEST("uart: LPUART1 and the reader and writer locks") {
  Fresh();
  stm::UartConfig c = Config();
  c.instance = kF4 ? 6U : stm::kLpuart1;
  stm::Uart u{c};
  KB_REQUIRE(u.EnsureReady() == Status::Ok);
  KB_CHECK(fake::IrqEnabled(kF4 ? USART6_IRQn : LPUART1_IRQn));
  Receive(c.instance, 9U);
  std::array<uint8_t, 1> rx{};
  KB_CHECK(u.Read(rx, kb::Milliseconds{1U}).IsOk());
  fake::Hook("HAL_GetTick", ReenterRead, &u); // while Read waits, the reader lock is held
  KB_CHECK(!u.Read(rx, kb::Milliseconds{2U}).IsOk());
  fake::Hook(
      "HAL_GetTick",
      [](void* context) {
        KB_CHECK_EQ(static_cast<stm::Uart*>(context)->FlushInput(), Status::Busy);
      },
      &u);
  KB_CHECK(!u.Read(rx, kb::Milliseconds{2U}).IsOk());
  const std::array<uint8_t, 1> tx{1U};
  fake::Hook(
      "HAL_UART_Transmit",
      [](void* context) {
        auto* uart = static_cast<stm::Uart*>(context);
        KB_CHECK_EQ(uart->SetBaud(9600U), Status::Busy);
        const std::array<uint8_t, 1> b{2U};
        KB_CHECK_EQ(uart->Write(b), Status::Busy);
      },
      &u);
  KB_CHECK_EQ(u.Write(tx), Status::Ok);
  KB_CHECK_EQ(u.Stats().lock_timeouts, 4U);
}

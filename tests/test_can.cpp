// CanController over the fake HAL CAN (FDCAN on the G4 variant, bxCAN on the F4 variant): bit
// timing, frames both ways, bounded waits, bus-off and recovery, and every refusal. Test code.
// SPDX-License-Identifier: MIT Copyright (c) 2026 The port-stm32 authors
#include "support.hpp"

namespace {
using kb::iface::CanFrame;

stm::CanConfig Config() {
  stm::CanConfig c{};
  c.controller = 1U;
  c.tx = PinId{GpioPort::A, 12U, 9U};
  c.rx = PinId{GpioPort::A, 11U, 9U};
  c.bitrate = 500000U;
  return c;
}

void ReenterSend(void* context) {
  KB_CHECK_EQ(static_cast<stm::CanController*>(context)->Send(CanFrame{}, kb::Milliseconds{1U}),
              Status::Busy);
}
} // namespace

KB_TEST("can timing: exact divisions near 87.5 % within the controller's limits") {
  bool ok = false;
  stm::CanTiming t = stm::ComputeCanTiming(40000000U, 500000U, stm::kFdCanLimits, ok);
  KB_CHECK(ok);
  KB_CHECK_EQ(t.prescaler * (1U + t.seg1 + t.seg2) * 500000U, 40000000U);
  KB_CHECK_EQ(t.prescaler, 4U); // 20 quanta
  KB_CHECK_EQ(t.seg1, 17U);
  KB_CHECK_EQ(t.seg2, 2U);
  KB_CHECK_EQ(t.sjw, 2U);
  t = stm::ComputeCanTiming(42000000U, 1000000U, stm::kBxCanLimits, ok);
  KB_CHECK(ok);
  KB_CHECK_EQ(t.prescaler * (1U + t.seg1 + t.seg2) * 1000000U, 42000000U);
  KB_CHECK(t.seg1 <= 16U);
  KB_CHECK(t.seg2 <= 8U);
  static_cast<void>(stm::ComputeCanTiming(40000000U, 9000U, stm::kFdCanLimits, ok));
  KB_CHECK(!ok);
  static_cast<void>(stm::ComputeCanTiming(40000000U, 1000001U, stm::kFdCanLimits, ok));
  KB_CHECK(!ok);
  static_cast<void>(
      stm::ComputeCanTiming(40000001U, 500000U, stm::kFdCanLimits, ok)); // no exact division
  KB_CHECK(!ok);
  static_cast<void>(
      stm::ComputeCanTiming(400000000U, 10000U, stm::kBxCanLimits, ok)); // prescaler too large
  KB_CHECK(!ok);
}

KB_TEST("can: frame validity follows CAN 2.0") {
  CanFrame f{};
  f.id = 0x7FFU;
  f.dlc = 8U;
  KB_CHECK(stm::CanFrameValid(f));
  f.id = 0x800U;
  KB_CHECK(!stm::CanFrameValid(f));
  f.extended = true;
  KB_CHECK(stm::CanFrameValid(f));
  f.id = 0x20000000U;
  KB_CHECK(!stm::CanFrameValid(f));
  f.id = 1U;
  f.dlc = 9U;
  KB_CHECK(!stm::CanFrameValid(f));
}

KB_TEST("can: EnsureReady sets pins, timing, mode and starts once; one owner; refusals") {
  Fresh();
  stm::CanConfig c = Config();
  c.mode = stm::CanMode::Loopback;
  {
    stm::CanController can{c};
    KB_REQUIRE(can.EnsureReady() == Status::Ok);
    KB_CHECK_EQ(can.EnsureReady(), Status::Ok);
    KB_CHECK(fake::ClockEnabled(kF4 ? "CAN1" : "FDCAN"));
    KB_CHECK_EQ(fake::ModeOf(0, 12), GPIO_MODE_AF_PP);
    KB_CHECK_EQ(fake::AlternateOf(0, 11), 9U);
    KB_CHECK_EQ(fake::CanPrescaler(), can.Timing().prescaler);
#if defined(KB_FAKE_STM32F4)
    KB_CHECK_EQ(fake::CanMode(), CAN_MODE_LOOPBACK);
    KB_CHECK_EQ(fake::Calls("HAL_CAN_Start"), 1U);
#else
    KB_CHECK_EQ(fake::CanMode(), FDCAN_MODE_INTERNAL_LOOPBACK);
    KB_CHECK_EQ(fake::Calls("HAL_FDCAN_Start"), 1U);
#endif
    stm::CanController second{Config()};
    KB_CHECK_EQ(second.EnsureReady(), Status::Busy);
  }
  c = Config();
  c.mode = stm::CanMode::ListenOnly;
  stm::CanController listen{c};
  KB_REQUIRE(listen.EnsureReady() == Status::Ok);
#if defined(KB_FAKE_STM32F4)
  KB_CHECK_EQ(fake::CanMode(), CAN_MODE_SILENT);
#else
  KB_CHECK_EQ(fake::CanMode(), FDCAN_MODE_BUS_MONITORING);
#endif
  KB_CHECK_EQ(listen.Release(), Status::Ok);
  const auto status = [](stm::CanConfig e) {
    stm::CanController x{e};
    return x.EnsureReady();
  };
  stm::CanConfig e = Config();
  e.controller = 3U;
  KB_CHECK_EQ(status(e), Status::Unsupported);
  e = Config();
  e.tx = PinId{};
  KB_CHECK_EQ(status(e), Status::InvalidArgument);
  e = Config();
  e.lock_timeout = kb::Milliseconds{0U};
  KB_CHECK_EQ(status(e), Status::InvalidArgument);
  e = Config();
  e.bitrate = 333333U;
  KB_CHECK_EQ(status(e), Status::InvalidArgument);
  e = Config();
  e.kernel_clock_hz = 48000000U;
  KB_CHECK_EQ(status(e), Status::Ok);
  stm::CanController failing{Config()};
#if defined(KB_FAKE_STM32F4)
  fake::Fail("HAL_CAN_Init", HAL_ERROR);
  KB_CHECK_EQ(failing.EnsureReady(), Status::Fault);
  fake::Fail("HAL_CAN_ConfigFilter", HAL_ERROR);
  KB_CHECK_EQ(failing.EnsureReady(), Status::Fault);
  fake::Fail("HAL_CAN_Start", HAL_TIMEOUT);
  KB_CHECK_EQ(failing.EnsureReady(), Status::Timeout);
  KB_REQUIRE(failing.EnsureReady() == Status::Ok);
  fake::Fail("HAL_CAN_DeInit", HAL_ERROR);
#else
  fake::Fail("HAL_FDCAN_Init", HAL_ERROR);
  KB_CHECK_EQ(failing.EnsureReady(), Status::Fault);
  fake::Fail("HAL_FDCAN_ConfigGlobalFilter", HAL_ERROR);
  KB_CHECK_EQ(failing.EnsureReady(), Status::Fault);
  fake::Fail("HAL_FDCAN_Start", HAL_TIMEOUT);
  KB_CHECK_EQ(failing.EnsureReady(), Status::Timeout);
  KB_REQUIRE(failing.EnsureReady() == Status::Ok);
  fake::Fail("HAL_FDCAN_DeInit", HAL_ERROR);
#endif
  KB_CHECK_EQ(failing.Release(), Status::Fault);
}

KB_TEST("can: Send and Receive carry identifier, flags and data") {
  Fresh();
  stm::CanController can{Config()};
  CanFrame f{};
  KB_CHECK_EQ(can.Send(f, kb::Milliseconds{1U}), Status::NotReady);
  KB_CHECK_EQ(can.Receive(f, kb::Milliseconds{1U}), Status::NotReady);
  KB_REQUIRE(can.EnsureReady() == Status::Ok);
  f.id = 0x18FEF100U;
  f.extended = true;
  f.remote = true;
  f.dlc = 3U;
  f.data = {1U, 2U, 3U, 0U, 0U, 0U, 0U, 0U};
  KB_CHECK_EQ(can.Send(f, kb::Milliseconds{5U}), Status::Ok);
  KB_REQUIRE(fake::CanSent().size() == 1U);
  KB_CHECK_EQ(fake::CanSent()[0].id, 0x18FEF100U);
  KB_CHECK(fake::CanSent()[0].extended && fake::CanSent()[0].remote);
  KB_CHECK_EQ(fake::CanSent()[0].dlc, 3U);
  f.id = 0x123U;
  f.extended = false;
  f.remote = false;
  KB_CHECK_EQ(can.Send(f, kb::Milliseconds{5U}), Status::Ok);
  KB_CHECK_EQ(fake::CanSent()[1].id, 0x123U);
  f.dlc = 9U;
  KB_CHECK_EQ(can.Send(f, kb::Milliseconds{5U}), Status::InvalidArgument);
  fake::Frame in{};
  in.id = 0x456U;
  in.dlc = 12U; // cut to 8
  in.data[7] = 0x77U;
  fake::CanFeed(in);
  fake::Frame ext{};
  ext.id = 0x1ABCDEFU;
  ext.extended = true;
  ext.remote = true;
  fake::CanFeed(ext);
  CanFrame got{};
  KB_CHECK_EQ(can.Receive(got, kb::Milliseconds{5U}), Status::Ok);
  KB_CHECK_EQ(got.id, 0x456U);
  KB_CHECK_EQ(got.dlc, 8U);
  KB_CHECK_EQ(got.data[7], 0x77U);
  KB_CHECK_EQ(can.Receive(got, kb::Milliseconds{5U}), Status::Ok);
  KB_CHECK(got.extended && got.remote);
  KB_CHECK_EQ(got.id, 0x1ABCDEFU);
  KB_CHECK_EQ(can.Receive(got, kb::Milliseconds{5U}), Status::Timeout);
  KB_CHECK(FailedWith(can.LastError(), Status::Timeout, stm::hal::kTimeout));
}

KB_TEST("can: a full transmit queue times out; bus-off refuses Send; HAL refusals") {
  Fresh();
  stm::CanController can{Config()};
  KB_REQUIRE(can.EnsureReady() == Status::Ok);
  CanFrame f{};
  f.id = 1U;
  fake::CanTxFree(0U);
  KB_CHECK_EQ(can.Send(f, kb::Milliseconds{3U}), Status::Timeout);
  fake::CanBusOff(true, 1000U);
  KB_CHECK_EQ(can.Send(f, kb::Milliseconds{3U}), Status::BusError);
  fake::CanBusOff(false);
  fake::CanTxFree(1U);
#if defined(KB_FAKE_STM32F4)
  fake::Fail("HAL_CAN_AddTxMessage", HAL_ERROR, 0x200000U);
  KB_CHECK_EQ(can.Send(f, kb::Milliseconds{3U}), Status::BusError);
  fake::Fail("HAL_CAN_GetRxMessage", HAL_ERROR);
  fake::Frame in{};
  fake::CanFeed(in);
#else
  fake::Fail("HAL_FDCAN_AddMessageToTxFifoQ", HAL_ERROR, 0x200U);
  KB_CHECK_EQ(can.Send(f, kb::Milliseconds{3U}), Status::BusError);
  fake::Fail("HAL_FDCAN_GetRxMessage", HAL_ERROR);
  fake::Frame in{};
  fake::CanFeed(in);
#endif
  CanFrame got{};
  // The interrupt could not read the frame: it stays in the FIFO, nothing is lost or invented.
  KB_CHECK_EQ(can.Receive(got, kb::Milliseconds{3U}), Status::Timeout);
  fake::CanRxInterrupt(true); // the next interrupt takes it
  KB_CHECK_EQ(can.Receive(got, kb::Milliseconds{3U}), Status::Ok);
  // A Send() while another holds the lock (inside the fill-level read) is Busy.
  fake::Hook(kF4 ? "HAL_CAN_GetTxMailboxesFreeLevel" : "HAL_FDCAN_GetTxFifoFreeLevel", ReenterSend,
             &can);
  fake::CanTxFree(0U);
  KB_CHECK_EQ(can.Send(f, kb::Milliseconds{2U}), Status::Timeout);
}

KB_TEST("can: a Receive() that waits does not hold off a Send()") {
  Fresh();
  stm::CanController can{Config()};
  KB_REQUIRE(can.EnsureReady() == Status::Ok);
  fake::Hook(
      "time_read",
      [](void* context) {
        CanFrame f{};
        f.id = 0x55U;
        KB_CHECK_EQ(static_cast<stm::CanController*>(context)->Send(f, kb::Milliseconds{1U}),
                    Status::Ok);
      },
      &can);
  CanFrame got{};
  KB_CHECK_EQ(can.Receive(got, kb::Milliseconds{3U}), Status::Timeout);
  KB_REQUIRE(fake::CanSent().size() == 1U);
  KB_CHECK_EQ(fake::CanSent()[0].id, 0x55U);
}

KB_TEST("can: Status reports the state and counters") {
  Fresh();
  stm::CanController can{Config()};
  KB_CHECK(FailedWith(can.Status(), Status::NotReady, stm::port_code::kNotReady));
  KB_REQUIRE(can.EnsureReady() == Status::Ok);
  fake::CanCounters(5U, 7U);
  kb::Result<stm::CanStatus> s = can.Status();
  KB_REQUIRE(s.IsOk());
  KB_CHECK(s.Value().state == stm::CanState::ErrorActive);
  KB_CHECK_EQ(s.Value().tx_error_counter, 5U);
  KB_CHECK_EQ(s.Value().rx_error_counter, 7U);
  KB_CHECK_EQ(s.Value().last_error_code, 3U);
  fake::CanCounters(128U, 0U);
  KB_CHECK(can.Status().Value().state == stm::CanState::ErrorPassive);
  fake::CanCounters(0U, 130U);
  KB_CHECK(can.Status().Value().state == stm::CanState::ErrorPassive);
  fake::CanBusOff(true, 1000U);
  KB_CHECK(can.Status().Value().state == stm::CanState::BusOff);
#if !defined(KB_FAKE_STM32F4)
  fake::Fail("HAL_FDCAN_GetProtocolStatus", HAL_ERROR);
  KB_CHECK(FailedWith(can.Status(), Status::Fault, Code(HAL_ERROR)));
  fake::Fail("HAL_FDCAN_GetErrorCounters", HAL_ERROR);
  KB_CHECK(FailedWith(can.Status(), Status::Fault, Code(HAL_ERROR)));
#endif
}

KB_TEST("can: Recover leaves bus-off within the timeout, or reports Timeout") {
  Fresh();
  stm::CanController can{Config()};
  KB_CHECK_EQ(can.Recover(kb::Milliseconds{5U}), Status::NotReady);
  KB_REQUIRE(can.EnsureReady() == Status::Ok);
  KB_CHECK_EQ(can.Recover(kb::Milliseconds{5U}), Status::Ok); // on the bus: nothing to do
  fake::CanBusOff(true, 3U);
  KB_CHECK_EQ(can.Recover(kb::Milliseconds{50U}), Status::Ok);
  KB_CHECK(can.Status().Value().state == stm::CanState::ErrorActive);
#if !defined(KB_FAKE_STM32F4)
  KB_CHECK_EQ(fake::Calls("HAL_FDCAN_Stop"), 1U);
#endif
  fake::CanBusOff(true, 100000U);
  KB_CHECK_EQ(can.Recover(kb::Milliseconds{5U}), Status::Timeout);
#if !defined(KB_FAKE_STM32F4)
  fake::CanBusOff(true, 2U);
  fake::Fail("HAL_FDCAN_Stop", HAL_ERROR);
  KB_CHECK_EQ(can.Recover(kb::Milliseconds{5U}), Status::Fault);
#endif
  fake::Hook(
      "time_read",
      [](void* context) {
        KB_CHECK_EQ(static_cast<stm::CanController*>(context)->Recover(kb::Milliseconds{1U}),
                    Status::Busy);
      },
      &can);
  fake::CanBusOff(true, 2U);
  KB_CHECK_EQ(can.Recover(kb::Milliseconds{50U}), Status::Ok);
}

KB_TEST("can: Release de-initialises even when the stop fails, and frees the controller") {
  Fresh();
  stm::CanController can{Config()};
  KB_REQUIRE(can.EnsureReady() == Status::Ok);
  fake::Fail(kF4 ? "HAL_CAN_Stop" : "HAL_FDCAN_Stop", HAL_ERROR);
  KB_CHECK_EQ(can.Release(), Status::Fault);
  KB_CHECK_EQ(fake::Calls(kF4 ? "HAL_CAN_DeInit" : "HAL_FDCAN_DeInit"), 1U);
  stm::CanController next{Config()};
  KB_CHECK_EQ(next.EnsureReady(), Status::Ok);
}

KB_TEST("can: frames arrive by interrupt into the ring; a FIFO overrun is reported as Overflow") {
  Fresh();
  stm::CanController can{Config()};
  KB_REQUIRE(can.EnsureReady() == Status::Ok);
  KB_CHECK(fake::IrqEnabled(kF4 ? CAN1_RX0_IRQn : FDCAN1_IT0_IRQn));
  KB_CHECK_EQ(fake::IrqPriority(kF4 ? CAN1_RX0_IRQn : FDCAN1_IT0_IRQn), 5U);
  fake::CanRxInterrupt(false); // the task (and the interrupt) held off while a burst arrives
  for (uint32_t i = 0U; i < 4U; ++i) {
    fake::Frame f{};
    f.id = 0x100U + i;
    fake::CanFeed(f); // the 3-deep hardware FIFO: the fourth is lost
  }
  fake::CanRxInterrupt(true);
  CanFrame got{};
  KB_CHECK_EQ(can.Receive(got, kb::Milliseconds{3U}), Status::Overflow);
  KB_CHECK(FailedWith(can.LastError(), Status::Overflow, stm::can_flags::kFifoOverrun));
  for (uint32_t i = 0U; i < 3U; ++i) {
    KB_CHECK_EQ(can.Receive(got, kb::Milliseconds{3U}), Status::Ok); // the frames kept
    KB_CHECK_EQ(got.id, 0x100U + i);
  }
  // A burst the interrupt takes in time is not lost, even far beyond the hardware FIFO.
  for (uint32_t i = 0U; i < stm::kCanRxFrames; ++i) {
    fake::Frame f{};
    f.id = i;
    fake::CanFeed(f);
  }
  for (uint32_t i = 0U; i < stm::kCanRxFrames; ++i) {
    KB_CHECK_EQ(can.Receive(got, kb::Milliseconds{3U}), Status::Ok);
  }
  for (uint32_t i = 0U; i <= stm::kCanRxFrames; ++i) { // one more than the ring holds
    fake::CanFeed(fake::Frame{});
  }
  KB_CHECK_EQ(can.Receive(got, kb::Milliseconds{3U}), Status::Overflow);
  KB_CHECK(FailedWith(can.LastError(), Status::Overflow, stm::can_flags::kRingFull));
  stm::HandleCanIrq(1U);
  stm::HandleCanIrq(2U); // not open: nothing
  KB_CHECK_EQ(fake::Calls(kF4 ? "HAL_CAN_IRQHandler" : "HAL_FDCAN_IRQHandler"), 1U);
  KB_CHECK_EQ(can.Receive(got, kb::Milliseconds{stm::kMaxTimeout.Count() + 1U}),
              Status::InvalidArgument);
  KB_CHECK_EQ(can.Send(got, kb::Milliseconds{stm::kMaxTimeout.Count() + 1U}),
              Status::InvalidArgument);
}

KB_TEST("can: frames received before a Release() are not delivered after the reopen") {
  Fresh();
  stm::CanController can{Config()};
  KB_REQUIRE(can.EnsureReady() == Status::Ok);
  fake::Frame f{};
  f.id = 0x42U;
  fake::CanFeed(f); // into the ring
  KB_CHECK_EQ(can.Release(), Status::Ok);
  KB_REQUIRE(can.EnsureReady() == Status::Ok);
  CanFrame got{};
  KB_CHECK_EQ(can.Receive(got, kb::Milliseconds{2U}), Status::Timeout);
}

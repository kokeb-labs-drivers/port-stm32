// Boundary file (deviations/boundary-files.yml): the CAN calls of the STM32 HAL behind sdk.hpp:
// FDCAN in classic mode on G4 and H7, bxCAN on F4 and L4, their receive, overrun and transmit
// notifications, the HAL callbacks that pass them to the owner, and the interrupt entry point. Each
// function converts its arguments, calls the HAL and returns VendorCode(HAL status, the handle's
// ErrorCode); the handles live in the table below. SPDX-License-Identifier: MIT Copyright (c) 2026
// The port-stm32 authors
#include <array>
// The owners shared with the interrupts are std::atomic.
// misra-lint: ignore KB-CPP-012
#include <atomic>
#include <cstddef>
#include <cstdint>

#include "kb/interfaces/types.hpp"
#include "kb/port/stm32/can.hpp"
#include "kb/port/stm32/error.hpp"
#include "kb/port/stm32/irq.hpp"
#include "sdk.hpp"
#if defined(KB_PORT_STM32_G4)
#include "stm32g4xx_hal.h"
#elif defined(KB_PORT_STM32_H7)
#include "stm32h7xx_hal.h"
#elif defined(KB_PORT_STM32_F4)
#include "stm32f4xx_hal.h"
#elif defined(KB_PORT_STM32_L4)
#include "stm32l4xx_hal.h"
#else
#error "define one of KB_PORT_STM32_G4, KB_PORT_STM32_H7, KB_PORT_STM32_F4, KB_PORT_STM32_L4"
#endif

namespace kb::port::stm32::sdk {

namespace {
constexpr std::size_t kControllers = 4U;

[[nodiscard]] uint32_t Code(HAL_StatusTypeDef status, uint32_t error_code) noexcept {
  return VendorCode(static_cast<uint32_t>(status), error_code);
}

#if defined(STM32G4) || defined(STM32H7)
// Each family's helpers in a namespace of its own, so a coverage merge of both host variants sees
// distinct functions; the common part below reaches them through `family`.
namespace fdcan {
// --- FDCAN ---------------------------------------------------------------------------------------
using Handle = FDCAN_HandleTypeDef;

[[nodiscard]] FDCAN_GlobalTypeDef* InstanceOf(uint8_t controller) noexcept {
  FDCAN_GlobalTypeDef* f = nullptr;
  if (controller == 1U) {
    f = FDCAN1;
#if defined(FDCAN2)
  } else if (controller == 2U) {
    f = FDCAN2;
#endif
#if defined(FDCAN3)
  } else if (controller == 3U) {
    f = FDCAN3;
#endif
  } else {
    f = nullptr;
  }
  return f;
}

[[nodiscard]] uint32_t ModeOf(CanMode mode) noexcept {
  uint32_t m = FDCAN_MODE_NORMAL;
  if (mode == CanMode::Loopback) {
    m = FDCAN_MODE_INTERNAL_LOOPBACK;
  } else if (mode == CanMode::ListenOnly) {
    m = FDCAN_MODE_BUS_MONITORING;
  } else {
    m = FDCAN_MODE_NORMAL;
  }
  return m;
}

void InitFields(Handle& h, const CanConfig& config, const CanTiming& timing) noexcept {
  h.Init.FrameFormat = FDCAN_FRAME_CLASSIC;
  h.Init.Mode = ModeOf(config.mode);
  h.Init.AutoRetransmission = ENABLE;
  h.Init.TransmitPause = DISABLE;
  h.Init.ProtocolException = DISABLE;
  h.Init.NominalPrescaler = timing.prescaler;
  h.Init.NominalSyncJumpWidth = timing.sjw;
  h.Init.NominalTimeSeg1 = timing.seg1;
  h.Init.NominalTimeSeg2 = timing.seg2;
  h.Init.DataPrescaler = 1U;
  h.Init.DataSyncJumpWidth = 1U;
  h.Init.DataTimeSeg1 = 1U;
  h.Init.DataTimeSeg2 = 1U;
  h.Init.StdFiltersNbr = 0U;
  h.Init.ExtFiltersNbr = 0U;
  h.Init.TxFifoQueueMode = FDCAN_TX_FIFO_OPERATION;
#if defined(STM32G4)
  h.Init.ClockDivider = FDCAN_CLOCK_DIV1;
#else
  constexpr uint32_t kRamWordsPerController = 0x200U; // 16 RX + 8 TX elements of 4 words, with room
  h.Init.MessageRAMOffset = (uint32_t{config.controller} - 1U) * kRamWordsPerController;
  h.Init.RxFifo0ElmtsNbr = 16U;
  h.Init.RxFifo0ElmtSize = FDCAN_DATA_BYTES_8;
  h.Init.TxFifoQueueElmtsNbr = 8U;
  h.Init.TxElmtSize = FDCAN_DATA_BYTES_8;
#endif
}

[[nodiscard]] uint32_t Open(Handle& h, const CanConfig& config, const CanTiming& timing) noexcept {
  __HAL_RCC_FDCAN_CLK_ENABLE();
  h.Instance = InstanceOf(config.controller);
  InitFields(h, config, timing);
  HAL_StatusTypeDef s = HAL_FDCAN_Init(&h);
  if (s == HAL_OK) {
    s = HAL_FDCAN_ConfigGlobalFilter(&h, FDCAN_ACCEPT_IN_RX_FIFO0, FDCAN_ACCEPT_IN_RX_FIFO0,
                                     FDCAN_FILTER_REMOTE, FDCAN_FILTER_REMOTE);
  }
  if (s == HAL_OK) {
#if defined(STM32H7)
    constexpr uint32_t kTxBuffers = 0xFFU; // the 8 elements of the TX FIFO configured above
#else
    constexpr uint32_t kTxBuffers = FDCAN_TX_BUFFER0 | FDCAN_TX_BUFFER1 | FDCAN_TX_BUFFER2;
#endif
    s = HAL_FDCAN_ActivateNotification(
        &h, FDCAN_IT_RX_FIFO0_NEW_MESSAGE | FDCAN_IT_RX_FIFO0_MESSAGE_LOST | FDCAN_IT_TX_COMPLETE,
        kTxBuffers); // all on interrupt line 0
  }
  if (s == HAL_OK) {
    s = HAL_FDCAN_Start(&h);
  }
  return Code(s, h.ErrorCode);
}

void Irqs(uint8_t controller, uint8_t priority, bool on) noexcept {
  IRQn_Type irq = FDCAN1_IT0_IRQn;
#if defined(FDCAN2)
  if (controller == 2U) {
    irq = FDCAN2_IT0_IRQn;
  }
#endif
#if defined(FDCAN3)
  if (controller == 3U) {
    irq = FDCAN3_IT0_IRQn;
  }
#endif
  static_cast<void>(controller);
  if (on) {
    HAL_NVIC_SetPriority(irq, priority, 0U);
    HAL_NVIC_EnableIRQ(irq);
  } else {
    HAL_NVIC_DisableIRQ(irq);
  }
}

void Interrupt(Handle& h) noexcept {
  HAL_FDCAN_IRQHandler(&h);
}

[[nodiscard]] uint32_t Close(Handle& h) noexcept {
  const HAL_StatusTypeDef stop = HAL_FDCAN_Stop(&h);
  const HAL_StatusTypeDef deinit = HAL_FDCAN_DeInit(&h); // always, so the handle is free again
  const HAL_StatusTypeDef s = (stop != HAL_OK) ? stop : deinit;
  return Code(s, h.ErrorCode);
}

[[nodiscard]] uint32_t TxFree(const Handle& h) noexcept {
  return HAL_FDCAN_GetTxFifoFreeLevel(&h);
}

[[nodiscard]] uint32_t RxCount(const Handle& h) noexcept {
  return HAL_FDCAN_GetRxFifoFillLevel(&h, FDCAN_RX_FIFO0);
}

[[nodiscard]] uint32_t Put(Handle& h, const kb::iface::CanFrame& frame) noexcept {
  FDCAN_TxHeaderTypeDef header{};
  header.Identifier = frame.id;
  header.IdType = frame.extended ? FDCAN_EXTENDED_ID : FDCAN_STANDARD_ID;
  header.TxFrameType = frame.remote ? FDCAN_REMOTE_FRAME : FDCAN_DATA_FRAME;
  header.DataLength = frame.dlc; // FDCAN_DLC_BYTES_0 to _8 are 0 to 8
  header.ErrorStateIndicator = FDCAN_ESI_ACTIVE;
  header.BitRateSwitch = FDCAN_BRS_OFF;
  header.FDFormat = FDCAN_CLASSIC_CAN;
  header.TxEventFifoControl = FDCAN_NO_TX_EVENTS;
  header.MessageMarker = 0U;
  const HAL_StatusTypeDef s = HAL_FDCAN_AddMessageToTxFifoQ(&h, &header, frame.data.data());
  return Code(s, h.ErrorCode);
}

[[nodiscard]] uint32_t Get(Handle& h, kb::iface::CanFrame& frame) noexcept {
  FDCAN_RxHeaderTypeDef header{};
  std::array<uint8_t, kb::iface::kCanMaxDlc> data{};
  const HAL_StatusTypeDef s = HAL_FDCAN_GetRxMessage(&h, FDCAN_RX_FIFO0, &header, data.data());
  if (s == HAL_OK) {
    frame = kb::iface::CanFrame{};
    frame.id = header.Identifier;
    frame.extended = header.IdType == FDCAN_EXTENDED_ID;
    frame.remote = header.RxFrameType == FDCAN_REMOTE_FRAME;
    frame.dlc = static_cast<uint8_t>(
        (header.DataLength > kb::iface::kCanMaxDlc) ? kb::iface::kCanMaxDlc : header.DataLength);
    frame.data = data;
  }
  return Code(s, h.ErrorCode);
}

[[nodiscard]] uint32_t ReadStatus(const Handle& h, CanStatus& status) noexcept {
  FDCAN_ProtocolStatusTypeDef ps{};
  FDCAN_ErrorCountersTypeDef ec{};
  HAL_StatusTypeDef s = HAL_FDCAN_GetProtocolStatus(&h, &ps);
  if (s == HAL_OK) {
    s = HAL_FDCAN_GetErrorCounters(&h, &ec);
  }
  if (s == HAL_OK) {
    status.tx_error_counter = ec.TxErrorCnt;
    status.rx_error_counter = ec.RxErrorCnt;
    status.last_error_code = ps.LastErrorCode;
    status.state = CanState::ErrorActive;
    if (ps.BusOff != 0U) {
      status.state = CanState::BusOff;
    } else if (ps.ErrorPassive != 0U) {
      status.state = CanState::ErrorPassive;
    } else {
      status.state = CanState::ErrorActive;
    }
  }
  return Code(s, h.ErrorCode);
}

[[nodiscard]] uint32_t Restart(Handle& h) noexcept {
  HAL_StatusTypeDef s = HAL_FDCAN_Stop(&h); // bus-off left the controller in INIT
  if (s == HAL_OK) {
    s = HAL_FDCAN_Start(&h); // leaving INIT starts the bus-off recovery sequence
  }
  return Code(s, h.ErrorCode);
}

[[nodiscard]] uint32_t KernelClock() noexcept {
  return HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_FDCAN);
}

[[nodiscard]] CanTimingLimits Limits() noexcept {
  return kFdCanLimits;
}
} // namespace fdcan
namespace family = fdcan;
#else
namespace bxcan {
// --- bxCAN ---------------------------------------------------------------------------------------
using Handle = CAN_HandleTypeDef;

[[nodiscard]] CAN_TypeDef* InstanceOf(uint8_t controller) noexcept {
  CAN_TypeDef* c = nullptr;
  if (controller == 1U) {
    c = CAN1;
#if defined(CAN2)
  } else if (controller == 2U) {
    c = CAN2;
#endif
#if defined(CAN3)
  } else if (controller == 3U) {
    c = CAN3;
#endif
  } else {
    c = nullptr;
  }
  return c;
}

void Clock(uint8_t controller) noexcept {
  __HAL_RCC_CAN1_CLK_ENABLE(); // CAN2 needs CAN1's clock too (shared filters)
#if defined(CAN2)
  if (controller == 2U) {
    __HAL_RCC_CAN2_CLK_ENABLE();
  }
#endif
#if defined(CAN3)
  if (controller == 3U) {
    __HAL_RCC_CAN3_CLK_ENABLE();
  }
#endif
  static_cast<void>(controller);
}

[[nodiscard]] uint32_t ModeOf(CanMode mode) noexcept {
  uint32_t m = CAN_MODE_NORMAL;
  if (mode == CanMode::Loopback) {
    m = CAN_MODE_LOOPBACK;
  } else if (mode == CanMode::ListenOnly) {
    m = CAN_MODE_SILENT;
  } else {
    m = CAN_MODE_NORMAL;
  }
  return m;
}

[[nodiscard]] uint32_t Open(Handle& h, const CanConfig& config, const CanTiming& timing) noexcept {
  constexpr uint32_t kSlaveBank = 14U;
  Clock(config.controller);
  h.Instance = InstanceOf(config.controller);
  h.Init.Prescaler = timing.prescaler;
  h.Init.Mode = ModeOf(config.mode);
  h.Init.SyncJumpWidth = (timing.sjw - 1U) << CAN_BTR_SJW_Pos;
  h.Init.TimeSeg1 = (timing.seg1 - 1U) << CAN_BTR_TS1_Pos;
  h.Init.TimeSeg2 = (timing.seg2 - 1U) << CAN_BTR_TS2_Pos;
  h.Init.TimeTriggeredMode = DISABLE;
  h.Init.AutoBusOff =
      ENABLE; // the controller leaves bus-off by itself after 128 x 11 recessive bits
  h.Init.AutoWakeUp = DISABLE;
  h.Init.AutoRetransmission = ENABLE;
  h.Init.ReceiveFifoLocked = DISABLE;
  h.Init.TransmitFifoPriority = DISABLE;
  HAL_StatusTypeDef s = HAL_CAN_Init(&h);
  if (s == HAL_OK) {
    CAN_FilterTypeDef f{};
    f.FilterBank = (config.controller == 2U) ? kSlaveBank : 0U;
    f.FilterMode = CAN_FILTERMODE_IDMASK;
    f.FilterScale = CAN_FILTERSCALE_32BIT;
    f.FilterFIFOAssignment = CAN_RX_FIFO0;
    f.FilterActivation = ENABLE;
    f.SlaveStartFilterBank = kSlaveBank;
    s = HAL_CAN_ConfigFilter(&h, &f); // mask 0: every frame
  }
  if (s == HAL_OK) {
    s = HAL_CAN_ActivateNotification(
        &h, CAN_IT_RX_FIFO0_MSG_PENDING | CAN_IT_RX_FIFO0_OVERRUN | CAN_IT_TX_MAILBOX_EMPTY);
  }
  if (s == HAL_OK) {
    s = HAL_CAN_Start(&h);
  }
  return Code(s, h.ErrorCode);
}

void IrqSet(IRQn_Type irq, uint8_t priority, bool on) noexcept {
  if (on) {
    HAL_NVIC_SetPriority(irq, priority, 0U);
    HAL_NVIC_EnableIRQ(irq);
  } else {
    HAL_NVIC_DisableIRQ(irq);
  }
}

void Irqs(uint8_t controller, uint8_t priority, bool on) noexcept {
  IRQn_Type tx = CAN1_TX_IRQn;
  IRQn_Type rx = CAN1_RX0_IRQn;
  IRQn_Type sce = CAN1_SCE_IRQn;
#if defined(CAN2)
  if (controller == 2U) {
    tx = CAN2_TX_IRQn;
    rx = CAN2_RX0_IRQn;
    sce = CAN2_SCE_IRQn;
  }
#endif
#if defined(CAN3)
  if (controller == 3U) {
    tx = CAN3_TX_IRQn;
    rx = CAN3_RX0_IRQn;
    sce = CAN3_SCE_IRQn;
  }
#endif
  static_cast<void>(controller);
  IrqSet(tx, priority, on);
  IrqSet(rx, priority, on);
  IrqSet(sce, priority, on);
}

void Interrupt(Handle& h) noexcept {
  HAL_CAN_IRQHandler(&h);
}

[[nodiscard]] uint32_t Close(Handle& h) noexcept {
  const HAL_StatusTypeDef stop = HAL_CAN_Stop(&h);
  const HAL_StatusTypeDef deinit = HAL_CAN_DeInit(&h); // always, so the handle is free again
  const HAL_StatusTypeDef s = (stop != HAL_OK) ? stop : deinit;
  return Code(s, h.ErrorCode);
}

[[nodiscard]] uint32_t TxFree(const Handle& h) noexcept {
  return HAL_CAN_GetTxMailboxesFreeLevel(&h);
}

[[nodiscard]] uint32_t RxCount(const Handle& h) noexcept {
  return HAL_CAN_GetRxFifoFillLevel(&h, CAN_RX_FIFO0);
}

[[nodiscard]] uint32_t Put(Handle& h, const kb::iface::CanFrame& frame) noexcept {
  CAN_TxHeaderTypeDef header{};
  header.StdId = frame.extended ? 0U : frame.id;
  header.ExtId = frame.extended ? frame.id : 0U;
  header.IDE = frame.extended ? CAN_ID_EXT : CAN_ID_STD;
  header.RTR = frame.remote ? CAN_RTR_REMOTE : CAN_RTR_DATA;
  header.DLC = frame.dlc;
  header.TransmitGlobalTime = DISABLE;
  uint32_t mailbox = 0U;
  const HAL_StatusTypeDef s = HAL_CAN_AddTxMessage(&h, &header, frame.data.data(), &mailbox);
  return Code(s, h.ErrorCode);
}

[[nodiscard]] uint32_t Get(Handle& h, kb::iface::CanFrame& frame) noexcept {
  CAN_RxHeaderTypeDef header{};
  std::array<uint8_t, kb::iface::kCanMaxDlc> data{};
  const HAL_StatusTypeDef s = HAL_CAN_GetRxMessage(&h, CAN_RX_FIFO0, &header, data.data());
  if (s == HAL_OK) {
    frame = kb::iface::CanFrame{};
    frame.extended = header.IDE == CAN_ID_EXT;
    frame.id = frame.extended ? header.ExtId : header.StdId;
    frame.remote = header.RTR == CAN_RTR_REMOTE;
    frame.dlc = static_cast<uint8_t>((header.DLC > kb::iface::kCanMaxDlc) ? kb::iface::kCanMaxDlc
                                                                          : header.DLC);
    frame.data = data;
  }
  return Code(s, h.ErrorCode);
}

constexpr uint32_t kErrorPassive = 128U; // either counter at 128 or above: error passive

[[nodiscard]] CanState StateOf(bool bus_off, const CanStatus& status) noexcept {
  CanState state = CanState::ErrorActive;
  if (bus_off) {
    state = CanState::BusOff;
  } else if ((status.tx_error_counter >= kErrorPassive) ||
             (status.rx_error_counter >= kErrorPassive)) {
    state = CanState::ErrorPassive;
  } else {
    state = CanState::ErrorActive;
  }
  return state;
}

[[nodiscard]] uint32_t ReadStatus(const Handle& h, CanStatus& status) noexcept {
  const uint32_t esr = h.Instance->ESR;
  status.tx_error_counter = (esr & CAN_ESR_TEC_Msk) >> CAN_ESR_TEC_Pos;
  status.rx_error_counter = (esr & CAN_ESR_REC_Msk) >> CAN_ESR_REC_Pos;
  status.last_error_code = (esr & CAN_ESR_LEC_Msk) >> CAN_ESR_LEC_Pos;
  status.state = StateOf((esr & CAN_ESR_BOFF) != 0U, status);
  return hal::kOk;
}

[[nodiscard]] uint32_t Restart(const Handle& h) noexcept {
  static_cast<void>(h); // automatic bus-off management recovers by itself
  return hal::kOk;
}

[[nodiscard]] uint32_t KernelClock() noexcept {
  return HAL_RCC_GetPCLK1Freq();
}

[[nodiscard]] CanTimingLimits Limits() noexcept {
  return kBxCanLimits;
}
} // namespace bxcan
namespace family = bxcan;
#endif

std::array<family::Handle, kControllers>& Handles() noexcept {
  // codeql::misra_deviation_next_line(DEV-001)
  static std::array<family::Handle, kControllers> handles{};
  return handles;
}

std::array<std::atomic<CanController*>, kControllers>& Owners() noexcept {
  // codeql::misra_deviation_next_line(DEV-001)
  static std::array<std::atomic<CanController*>, kControllers> owners{};
  return owners;
}

// The owner of the handle `h`, or nullptr.
[[nodiscard]] CanController* OwnerOf(const family::Handle* h) noexcept {
  CanController* found = nullptr;
  for (std::size_t i = 0U; i < kControllers; ++i) {
    if (&Handles()[i] == h) {
      found = Owners()[i].load(std::memory_order_acquire);
    }
  }
  return found;
}
} // namespace

bool CanExists(uint8_t controller) noexcept {
  return (controller < kControllers) && (family::InstanceOf(controller) != nullptr);
}

CanTimingLimits CanLimits() noexcept {
  return family::Limits();
}

uint32_t CanKernelClockHz(uint8_t controller) noexcept {
  static_cast<void>(controller);
  return family::KernelClock();
}

uint32_t CanOpen(const CanConfig& config, const CanTiming& timing, CanController& owner) noexcept {
  uint32_t code = port_code::kOwned;
  if (Owners()[config.controller].load(std::memory_order_acquire) == nullptr) {
    static_cast<void>(PinAlternate(config.tx, false, Pull::None));
    static_cast<void>(PinAlternate(config.rx, false, Pull::Up));
    Owners()[config.controller].store(
        &owner, std::memory_order_release); // before the start: a frame may arrive at once
    code = family::Open(Handles()[config.controller], config, timing);
    if (code == hal::kOk) {
      family::Irqs(config.controller, config.irq_priority, true);
    } else {
      Owners()[config.controller].store(nullptr, std::memory_order_release);
    }
  }
  return code;
}

uint32_t CanClose(uint8_t controller) noexcept {
  family::Irqs(controller, 0U, false);
  Owners()[controller].store(nullptr, std::memory_order_release);
  return family::Close(Handles()[controller]);
}

uint32_t CanTxFree(uint8_t controller, uint32_t& free) noexcept {
  free = family::TxFree(Handles()[controller]);
  return hal::kOk;
}

uint32_t CanRxCount(uint8_t controller, uint32_t& count) noexcept {
  count = family::RxCount(Handles()[controller]);
  return hal::kOk;
}

uint32_t CanPut(uint8_t controller, const kb::iface::CanFrame& frame) noexcept {
  return family::Put(Handles()[controller], frame);
}

uint32_t CanGet(uint8_t controller, kb::iface::CanFrame& frame) noexcept {
  return family::Get(Handles()[controller], frame);
}

uint32_t CanReadStatus(uint8_t controller, CanStatus& status) noexcept {
  return family::ReadStatus(Handles()[controller], status);
}

uint32_t CanRestart(uint8_t controller) noexcept {
  return family::Restart(Handles()[controller]);
}

} // namespace kb::port::stm32::sdk

namespace kb::port::stm32 {
// The interrupt entry point (irq.hpp): the HAL's handler of the open controller.
void HandleCanIrq(uint8_t controller) noexcept {
  if (controller < sdk::kControllers) {
    if (sdk::Owners()[controller].load(std::memory_order_acquire) != nullptr) {
      sdk::family::Interrupt(sdk::Handles()[controller]);
    } else {
      sdk::family::Irqs(controller, 0U, false); // not the port's: no interrupt storm
    }
  }
}
} // namespace kb::port::stm32

// The HAL's CAN callbacks (weak in the HAL, defined here): receive, overrun and transmit-complete
// passed to the owner. C linkage and global names by definition.
extern "C" {
// NOLINTBEGIN(readability-identifier-naming): the HAL fixes these names
#if defined(STM32G4) || defined(STM32H7)
void HAL_FDCAN_RxFifo0Callback(FDCAN_HandleTypeDef* hfdcan, uint32_t RxFifo0ITs) {
  kb::port::stm32::CanController* const owner = kb::port::stm32::sdk::OwnerOf(hfdcan);
  if (owner != nullptr) {
    owner->OnRxInterrupt((RxFifo0ITs & FDCAN_IT_RX_FIFO0_MESSAGE_LOST) != 0U);
  }
}
void HAL_FDCAN_TxBufferCompleteCallback(FDCAN_HandleTypeDef* hfdcan, uint32_t BufferIndexes) {
  static_cast<void>(BufferIndexes);
  kb::port::stm32::CanController* const owner = kb::port::stm32::sdk::OwnerOf(hfdcan);
  if (owner != nullptr) {
    owner->OnTxInterrupt();
  }
}
#else
namespace {
void TxDone(const CAN_HandleTypeDef* hcan) noexcept {
  kb::port::stm32::CanController* const owner = kb::port::stm32::sdk::OwnerOf(hcan);
  if (owner != nullptr) {
    owner->OnTxInterrupt();
  }
}
} // namespace
void HAL_CAN_RxFifo0MsgPendingCallback(CAN_HandleTypeDef* hcan) {
  kb::port::stm32::CanController* const owner = kb::port::stm32::sdk::OwnerOf(hcan);
  if (owner != nullptr) {
    owner->OnRxInterrupt(false);
  }
}
void HAL_CAN_ErrorCallback(CAN_HandleTypeDef* hcan) {
  kb::port::stm32::CanController* const owner = kb::port::stm32::sdk::OwnerOf(hcan);
  const bool overrun = (hcan->ErrorCode & HAL_CAN_ERROR_RX_FOV0) != 0U;
  // HAL_ERROR only for a handle neither ready nor listening: nothing more an interrupt can do.
  const bool reset = HAL_CAN_ResetError(hcan) == HAL_OK;
  static_cast<void>(reset);
  if ((owner != nullptr) && overrun) {
    owner->OnRxInterrupt(true);
  }
}
void HAL_CAN_TxMailbox0CompleteCallback(CAN_HandleTypeDef* hcan) {
  TxDone(hcan);
}
void HAL_CAN_TxMailbox1CompleteCallback(CAN_HandleTypeDef* hcan) {
  TxDone(hcan);
}
void HAL_CAN_TxMailbox2CompleteCallback(CAN_HandleTypeDef* hcan) {
  TxDone(hcan);
}
void HAL_CAN_TxMailbox0AbortCallback(CAN_HandleTypeDef* hcan) {
  TxDone(hcan);
}
void HAL_CAN_TxMailbox1AbortCallback(CAN_HandleTypeDef* hcan) {
  TxDone(hcan);
}
void HAL_CAN_TxMailbox2AbortCallback(CAN_HandleTypeDef* hcan) {
  TxDone(hcan);
}
#endif
// NOLINTEND(readability-identifier-naming)
} // extern "C"

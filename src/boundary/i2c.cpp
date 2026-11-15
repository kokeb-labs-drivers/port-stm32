// Boundary file (deviations/boundary-files.yml): the I2C calls of the STM32 HAL behind sdk.hpp, the
// HAL's I2C completion callbacks and the I2C interrupt entry point. Each function converts its
// arguments, calls the HAL and returns VendorCode(HAL status, the handle's ErrorCode); the handles
// live in the table below. Every decision is in the platform-free src/i2c.cpp.
// SPDX-License-Identifier: MIT Copyright (c) 2026 The port-stm32 authors
#include <array>
// Slot pointers shared with the interrupts are std::atomic (release before the NVIC enable).
// misra-lint: ignore KB-CPP-012
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>

#include "kb/port/stm32/completion.hpp"
#include "kb/port/stm32/error.hpp"
#include "kb/port/stm32/gpio.hpp"
#include "kb/port/stm32/i2c.hpp"
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
constexpr std::size_t kI2cs = 5U;

[[nodiscard]] uint32_t Code(HAL_StatusTypeDef status, uint32_t error_code) noexcept {
  return VendorCode(static_cast<uint32_t>(status), error_code);
}

// The vendor code of an interrupt API's start: its ErrorCode only when the start itself failed (the
// interrupt may already have ended the transfer, and set ErrorCode, before the call returns).
[[nodiscard]] uint32_t Started(HAL_StatusTypeDef status, uint32_t error_code) noexcept {
  return (status == HAL_OK) ? VendorCode(0U, 0U) : Code(status, error_code);
}

[[nodiscard]] uint16_t Size16(std::size_t n) noexcept {
  return static_cast<uint16_t>(n);
}

// --- I2C -----------------------------------------------------------------------------------------
struct I2cSlot {
  I2C_HandleTypeDef handle{};
  std::atomic<Completion*> done{nullptr}; ///< The owner's completion while it is open.
  uint8_t probe{0U};                      ///< The (unsent) buffer of an address-only probe.
};

std::array<I2cSlot, kI2cs>& I2cs() noexcept {
  // codeql::misra_deviation_next_line(DEV-001)
  static std::array<I2cSlot, kI2cs> slots{};
  return slots;
}

[[nodiscard]] I2cSlot* I2cSlotOf(const I2C_HandleTypeDef* h) noexcept {
  I2cSlot* found = nullptr;
  for (I2cSlot& slot : I2cs()) {
    if (&slot.handle == h) {
      found = &slot;
    }
  }
  return found;
}

// The event and error interrupt numbers of an instance. Each is a local value returned by value: no
// vendor enumerator is written through a reference (Rule 10.2.3).
[[nodiscard]] IRQn_Type I2cEventIrq(uint8_t instance) noexcept {
  IRQn_Type irq = I2C1_EV_IRQn;
#if defined(I2C2)
  if (instance == 2U) {
    irq = I2C2_EV_IRQn;
  }
#endif
#if defined(I2C3)
  if (instance == 3U) {
    irq = I2C3_EV_IRQn;
  }
#endif
#if defined(I2C4)
  if (instance == 4U) {
    irq = I2C4_EV_IRQn;
  }
#endif
  static_cast<void>(instance);
  return irq;
}

[[nodiscard]] IRQn_Type I2cErrorIrq(uint8_t instance) noexcept {
  IRQn_Type irq = I2C1_ER_IRQn;
#if defined(I2C2)
  if (instance == 2U) {
    irq = I2C2_ER_IRQn;
  }
#endif
#if defined(I2C3)
  if (instance == 3U) {
    irq = I2C3_ER_IRQn;
  }
#endif
#if defined(I2C4)
  if (instance == 4U) {
    irq = I2C4_ER_IRQn;
  }
#endif
  static_cast<void>(instance);
  return irq;
}

void I2cIrqsOn(uint8_t instance, uint8_t priority, bool on) noexcept {
  const IRQn_Type event = I2cEventIrq(instance);
  const IRQn_Type error = I2cErrorIrq(instance);
  if (on) {
    HAL_NVIC_SetPriority(event, priority, 0U);
    HAL_NVIC_SetPriority(error, priority, 0U);
    HAL_NVIC_EnableIRQ(event);
    HAL_NVIC_EnableIRQ(error);
  } else {
    HAL_NVIC_DisableIRQ(event);
    HAL_NVIC_DisableIRQ(error);
  }
}

[[nodiscard]] I2C_TypeDef* I2cOf(uint8_t instance) noexcept {
  I2C_TypeDef* i = nullptr;
  if (instance == 1U) {
    i = I2C1;
#if defined(I2C2)
  } else if (instance == 2U) {
    i = I2C2;
#endif
#if defined(I2C3)
  } else if (instance == 3U) {
    i = I2C3;
#endif
#if defined(I2C4)
  } else if (instance == 4U) {
    i = I2C4;
#endif
  } else {
    i = nullptr;
  }
  return i;
}

void I2cClock(uint8_t instance) noexcept {
  if (instance == 1U) {
    __HAL_RCC_I2C1_CLK_ENABLE();
#if defined(I2C2)
  } else if (instance == 2U) {
    __HAL_RCC_I2C2_CLK_ENABLE();
#endif
#if defined(I2C3)
  } else if (instance == 3U) {
    __HAL_RCC_I2C3_CLK_ENABLE();
#endif
#if defined(I2C4)
  } else if (instance == 4U) {
    __HAL_RCC_I2C4_CLK_ENABLE();
#endif
  } else {
    // no other instance
  }
}

[[nodiscard]] uint16_t Address8(uint8_t address) noexcept {
  return static_cast<uint16_t>(static_cast<uint16_t>(address) << 1U);
}
} // namespace

// --- I2C -----------------------------------------------------------------------------------------
bool I2cExists(uint8_t instance) noexcept {
  return (instance < kI2cs) && (I2cOf(instance) != nullptr);
}

bool I2cHasTimingRegister() noexcept {
#if defined(STM32F4)
  return false;
#else
  return true;
#endif
}

uint32_t I2cKernelClockHz(uint8_t instance) noexcept {
#if defined(STM32H7) && defined(I2C4)
  // I2C4 is in D3: its kernel clock is selected in D3CCIPR (rcc_pclk4 at reset), not PCLK1.
  return (instance == 4U) ? HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_I2C4) : HAL_RCC_GetPCLK1Freq();
#else
  static_cast<void>(instance); // every other instance runs from PCLK1 at reset
  return HAL_RCC_GetPCLK1Freq();
#endif
}

namespace {
// One HAL_I2C_Init of a handle the caller owns.
[[nodiscard]] uint32_t I2cInitHandle(I2C_HandleTypeDef& h, const I2cConfig& config,
                                     uint32_t timing) noexcept {
  const Pull pull = config.internal_pullups ? Pull::Up : Pull::None;
  static_cast<void>(PinAlternate(config.scl, true, pull));
  static_cast<void>(PinAlternate(config.sda, true, pull));
  I2cClock(config.instance);
  h.Instance = I2cOf(config.instance);
#if defined(STM32F4)
  static_cast<void>(timing);
  h.Init.ClockSpeed = config.clock_hz;
  h.Init.DutyCycle = I2C_DUTYCYCLE_2;
#else
  h.Init.Timing = timing;
  h.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
#endif
  h.Init.OwnAddress1 = 0U;
  h.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  h.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  h.Init.OwnAddress2 = 0U;
  h.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  h.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  HAL_StatusTypeDef s = HAL_I2C_Init(&h);
#if !defined(STM32F4)
  if (s == HAL_OK) {
    s = HAL_I2CEx_ConfigAnalogFilter(&h, I2C_ANALOGFILTER_ENABLE);
  }
#endif
  if (s != HAL_OK) {
    h.Instance = nullptr; // not opened: free for the next EnsureReady()
  }
  return Code(s, h.ErrorCode);
}

// The interrupt priority each open I2C instance was given.
std::array<uint8_t, kI2cs>& I2cPriorities() noexcept {
  // codeql::misra_deviation_next_line(DEV-001)
  static std::array<uint8_t, kI2cs> priorities{};
  return priorities;
}
} // namespace

uint32_t I2cOpen(const I2cConfig& config, uint32_t timing, Completion& done) noexcept {
  I2cSlot& slot = I2cs()[config.instance];
  // The handle's Instance is the owner mark: set while one I2cController has the peripheral open.
  uint32_t code = port_code::kOwned;
  if (slot.handle.Instance == nullptr) {
    code = I2cInitHandle(slot.handle, config, timing);
    if (code == hal::kOk) {
      slot.done.store(&done, std::memory_order_release);
      I2cPriorities()[config.instance] = config.irq_priority;
      I2cIrqsOn(config.instance, config.irq_priority, true);
    }
  }
  return code;
}

uint32_t I2cClose(uint8_t instance) noexcept {
  I2cSlot& slot = I2cs()[instance];
  I2cIrqsOn(instance, 0U, false);
  slot.done.store(nullptr, std::memory_order_release);
  const HAL_StatusTypeDef s = HAL_I2C_DeInit(&slot.handle);
  const uint32_t code = Code(s, slot.handle.ErrorCode);
  slot.handle.Instance = nullptr;
  return code;
}

void I2cPrepare(uint8_t instance) noexcept {
#if defined(STM32F4)
  static_cast<void>(instance); // I2C v1 has no TXDR/RXDR or CR2 transfer fields
#else
  I2C_TypeDef* const r = I2cs()[instance].handle.Instance;
  if (r != nullptr) { // not open after a failed re-initialisation: nothing to flush
    if ((r->ISR & I2C_ISR_TXIS) != 0U) {
      r->TXDR = 0x00U; // a latched TXIS: a dummy byte clears it, the flush below drops it
    }
    if ((r->ISR & I2C_ISR_TXE) == 0U) {
      r->ISR |= I2C_ISR_TXE; // flush the transmit data register
    }
    if ((r->ISR & I2C_ISR_RXNE) != 0U) {
      static_cast<void>(r->RXDR);
    }
    constexpr uint32_t kTransferFields = I2C_CR2_NBYTES | I2C_CR2_RELOAD | I2C_CR2_AUTOEND |
                                         I2C_CR2_START | I2C_CR2_STOP | I2C_CR2_RD_WRN;
    r->CR2 &= ~kTransferFields;
  }
#endif
}

void I2cRestore(uint8_t instance) noexcept {
  I2C_HandleTypeDef& h = I2cs()[instance].handle;
  if (h.Instance != nullptr) { // not open: nothing to restore
#if defined(STM32F4)
    __HAL_I2C_DISABLE_IT(&h, I2C_IT_EVT | I2C_IT_BUF | I2C_IT_ERR);
    __HAL_I2C_CLEAR_FLAG(&h, I2C_FLAG_AF | I2C_FLAG_BERR | I2C_FLAG_ARLO | I2C_FLAG_OVR);
#else
    __HAL_I2C_DISABLE_IT(&h, I2C_IT_ERRI | I2C_IT_TCI | I2C_IT_STOPI | I2C_IT_NACKI | I2C_IT_ADDRI |
                                 I2C_IT_RXI | I2C_IT_TXI);
    __HAL_I2C_CLEAR_FLAG(
        &h, I2C_FLAG_AF | I2C_FLAG_STOPF | I2C_FLAG_BERR | I2C_FLAG_ARLO | I2C_FLAG_OVR);
#endif
    I2cPrepare(instance);
    // The HAL has no call that returns a handle to ready without a re-initialisation (DEV-003).
    // codeql::misra_deviation_next_line(DEV-003)
    h.State = HAL_I2C_STATE_READY;
    // codeql::misra_deviation_next_line(DEV-003)
    h.Mode = HAL_I2C_MODE_NONE;
    h.PreviousState = 0U;
    __HAL_UNLOCK(&h);
  }
}

bool I2cBusy(uint8_t instance) noexcept {
  const I2C_HandleTypeDef& h = I2cs()[instance].handle;
  return (h.Instance != nullptr) && (__HAL_I2C_GET_FLAG(&h, I2C_FLAG_BUSY) != RESET);
}

uint32_t I2cReset(uint8_t instance) noexcept {
  I2C_HandleTypeDef& h = I2cs()[instance].handle;
  I2cIrqsOn(instance, 0U, false);
  HAL_StatusTypeDef s = HAL_I2C_DeInit(&h); // PE cleared (SWRST on F4 at the next init)
  if (s == HAL_OK) {
    s = HAL_I2C_Init(&h); // the Init fields and Instance are kept by DeInit
  }
#if !defined(STM32F4)
  if (s == HAL_OK) {
    s = HAL_I2CEx_ConfigAnalogFilter(&h, I2C_ANALOGFILTER_ENABLE);
  }
#endif
  I2cIrqsOn(instance, I2cPriorities()[instance], true);
  return Code(s, h.ErrorCode);
}

uint32_t I2cStartTransmit(uint8_t instance, uint8_t address, std::span<const uint8_t> tx,
                          bool hold) noexcept {
  I2cSlot& slot = I2cs()[instance];
  HAL_StatusTypeDef s = HAL_OK;
  if (tx.empty()) {
    s = HAL_I2C_Master_Transmit_IT(&slot.handle, Address8(address), &slot.probe, 0U);
  } else {
    // The HAL's transmit takes a non-const pointer but only reads through it.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast): DEV-002, the HAL lacks the const
    auto* data = const_cast<uint8_t*>(tx.data());
    s = HAL_I2C_Master_Seq_Transmit_IT(&slot.handle, Address8(address), data, Size16(tx.size()),
                                       hold ? I2C_FIRST_FRAME : I2C_FIRST_AND_LAST_FRAME);
  }
  return Started(s, slot.handle.ErrorCode);
}

uint32_t I2cStartReceive(uint8_t instance, uint8_t address, std::span<uint8_t> rx,
                         bool after_write) noexcept {
  I2cSlot& slot = I2cs()[instance];
  const HAL_StatusTypeDef s =
      HAL_I2C_Master_Seq_Receive_IT(&slot.handle, Address8(address), rx.data(), Size16(rx.size()),
                                    after_write ? I2C_LAST_FRAME : I2C_FIRST_AND_LAST_FRAME);
  return Started(s, slot.handle.ErrorCode);
}

} // namespace kb::port::stm32::sdk

namespace kb::port::stm32 {

void HandleI2cIrq(uint8_t instance, bool error) noexcept {
  if (instance < sdk::kI2cs) {
    sdk::I2cSlot& slot = sdk::I2cs()[instance];
    if (slot.done.load(std::memory_order_acquire) == nullptr) {
      sdk::I2cIrqsOn(instance, 0U, false); // not the port's: no interrupt storm
    } else {
      if (error) {
        HAL_I2C_ER_IRQHandler(&slot.handle);
      } else {
        HAL_I2C_EV_IRQHandler(&slot.handle);
      }
    }
  }
}

} // namespace kb::port::stm32

// The HAL's I2C callbacks (weak in the HAL, defined here): the end of the transfer the port
// started, into the owner's Completion. C linkage and global names by definition.
namespace {
void I2cDone(const I2C_HandleTypeDef* h, uint32_t code) noexcept {
  kb::port::stm32::sdk::I2cSlot* const slot = kb::port::stm32::sdk::I2cSlotOf(h);
  kb::port::stm32::Completion* const done =
      (slot != nullptr) ? slot->done.load(std::memory_order_acquire) : nullptr;
  if (done != nullptr) {
    done->Complete(code);
  }
}
[[nodiscard]] uint32_t Failed(uint32_t error_code) noexcept {
  return kb::port::stm32::VendorCode(static_cast<uint32_t>(HAL_ERROR), error_code);
}
} // namespace

extern "C" {
// NOLINTBEGIN(readability-identifier-naming): the HAL fixes these names
void HAL_I2C_MasterTxCpltCallback(I2C_HandleTypeDef* hi2c) {
  I2cDone(hi2c, kb::port::stm32::hal::kOk);
}
void HAL_I2C_MasterRxCpltCallback(I2C_HandleTypeDef* hi2c) {
  I2cDone(hi2c, kb::port::stm32::hal::kOk);
}
void HAL_I2C_ErrorCallback(I2C_HandleTypeDef* hi2c) {
  I2cDone(hi2c, Failed(hi2c->ErrorCode));
}
void HAL_I2C_AbortCpltCallback(I2C_HandleTypeDef* hi2c) {
  I2cDone(hi2c, Failed(hi2c->ErrorCode));
}
// NOLINTEND(readability-identifier-naming)
} // extern "C"

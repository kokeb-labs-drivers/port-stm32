// The STM32 HAL fake: definitions of every HAL and CMSIS function the boundary files call (the link
// seam), with error injection by function name and a small model of each peripheral. Test code.
// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The port-stm32 authors
#include "fake_stm32_hal.hpp"

#include "kb/port/stm32/irq.hpp"
#include "kb/port/stm32/uart.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <deque>
#include <map>
#include <set>
#include <string>
#include <vector>

uint32_t SystemCoreClock = 16000000U;
DWT_Type kb_fake_dwt{};
CoreDebug_Type kb_fake_coredebug{};
ITM_Type kb_fake_itm{};
GPIO_TypeDef kb_fake_gpio[11]{};
volatile uint32_t kb_fake_exti_pr = 0U;
TIM_TypeDef kb_fake_tim[21]{};
ADC_TypeDef kb_fake_adc[6]{};
I2C_TypeDef kb_fake_i2c[5]{};
SPI_TypeDef kb_fake_spi[7]{};
USART_TypeDef kb_fake_usart[12]{};
#if !defined(KB_FAKE_STM32F4)
FDCAN_GlobalTypeDef kb_fake_fdcan[4]{};
#else
CAN_TypeDef kb_fake_can[4]{};
#endif

namespace fake {
namespace {
struct Injection {
  std::string function;
  HAL_StatusTypeDef status;
  uint32_t error_code;
  uint32_t count;
};
struct PendingHook {
  std::string function;
  void (*fn)(void*);
  void* context;
};
struct Pin {
  uint32_t mode{GPIO_MODE_ANALOG};
  uint32_t pull{GPIO_NOPULL};
  uint32_t alternate{0U};
  int forced{-1};
  uint32_t forced_reads{0U}; ///< Reads left before `forced` lapses (0: no limit).
};

struct State {
  std::vector<Injection> injections;
  std::vector<PendingHook> hooks;
  std::map<std::string, uint32_t> calls;
  std::set<std::string> clocks;
  uint32_t tick{0U};
  uint32_t tick_step{1U};
  uint32_t counter_step{1U};
  uint32_t tick_every{1U};
  HAL_TickFreqTypeDef tick_freq{HAL_TICK_FREQ_1KHZ};
  uint32_t tick_calls{0U};
  std::set<std::string> stalled;
  bool uart_running{false};
  uint32_t i2c_options{0U};
  uint32_t i2c_frames{0U};
  bool i2c_busy{false};
  bool i2c_busy_sticky{false};
  bool can_rx_irq{true};
  bool can_lost{false};
  bool can_notify{false};
  void* can_handle{nullptr};
  uint64_t cycles{0U};
  uint32_t cycle_step{100U};
  uint32_t primask{0U};
  std::array<std::array<Pin, 16>, 11> pins{};
  uint32_t toggles{0U};
  std::map<int, uint32_t> irq_priority;
  std::set<int> irq_enabled;
  std::array<bool, 128> present{};
  bool data_nack{false};
  std::vector<uint8_t> i2c_reply;
  std::vector<uint8_t> i2c_written;
  I2C_InitTypeDef i2c_init{};
  std::vector<uint8_t> spi_reply;
  std::vector<uint8_t> spi_sent;
  uint32_t spi_transfers{0U};
  SPI_InitTypeDef spi_init{};
  std::vector<uint8_t> uart_sent;
  UART_InitTypeDef uart_init{};
  uint32_t adc_raw{0U};
  uint32_t adc_channel{0U};
  uint32_t adc_sampling{0U};
  std::array<TIM_Base_InitTypeDef, 21> tim_init{};
  uint32_t can_tx_free{3U};
  std::deque<Frame> can_rx;
  std::vector<Frame> can_tx;
  bool bus_off{false};
  bool recovering{false};
  uint32_t bus_off_reads{0U};
  uint32_t tec{0U};
  uint32_t rec{0U};
  uint32_t can_prescaler{0U};
  uint32_t can_mode{0U};
};

State& S() {
  static State state{};
  return state;
}

// Counts the call, runs a pending hook, and returns true (with the injection) when one is pending.
bool Injected(const char* function, HAL_StatusTypeDef& status, uint32_t& error_code) {
  State& s = S();
  ++s.calls[function];
  for (std::size_t h = 0U; h < s.hooks.size(); ++h) {
    if (s.hooks[h].function == function) {
      const PendingHook hook = s.hooks[h];
      s.hooks.erase(s.hooks.begin() + static_cast<std::ptrdiff_t>(h));
      hook.fn(hook.context);
      break;
    }
  }
  for (Injection& i : s.injections) {
    if ((i.count > 0U) && (i.function == function)) {
      --i.count;
      status = i.status;
      error_code = i.error_code;
      return true;
    }
  }
  return false;
}

bool Injected(const char* function) {
  HAL_StatusTypeDef s = HAL_OK;
  uint32_t e = 0U;
  return Injected(function, s, e);
}

#define FAKE_INJECT(name, handle)                                                                  \
  do {                                                                                             \
    HAL_StatusTypeDef st_ = HAL_OK;                                                                \
    uint32_t ec_ = 0U;                                                                             \
    if (Injected(name, st_, ec_)) {                                                                \
      (handle)->ErrorCode = ec_;                                                                   \
      return st_;                                                                                  \
    }                                                                                              \
    (handle)->ErrorCode = 0U;                                                                      \
  } while (false)

int PortIndex(const GPIO_TypeDef* port) {
  return static_cast<int>(port - kb_fake_gpio);
}

int PinIndex(uint32_t mask) {
  int i = 0;
  while ((i < 16) && ((mask & (1U << static_cast<unsigned>(i))) == 0U)) {
    ++i;
  }
  return i;
}

bool IsOutput(uint32_t mode) {
  return (mode == GPIO_MODE_OUTPUT_PP) || (mode == GPIO_MODE_OUTPUT_OD);
}

HAL_StatusTypeDef I2cAck(uint16_t address8, I2C_HandleTypeDef* h) {
  const bool present = S().present[(address8 >> 1U) & 0x7FU];
  if (!present || S().data_nack) {
    h->ErrorCode = HAL_I2C_ERROR_AF;
    return HAL_ERROR;
  }
  return HAL_OK;
}

bool Stalled(const char* area) {
  return S().stalled.count(area) != 0U;
}

void I2cDone(I2C_HandleTypeDef* h, HAL_StatusTypeDef st) {
  ++S().i2c_frames;
  HAL_StatusTypeDef injected = HAL_OK;
  uint32_t ec = 0U;
  if (Injected("i2c_error", injected, ec)) { // the transfer ends in the error callback instead
    h->ErrorCode = ec;
    st = HAL_ERROR;
  }
  if (!Stalled("I2C")) {
    if (st == HAL_OK) {
      HAL_I2C_MasterTxCpltCallback(h);
    } else {
      HAL_I2C_ErrorCallback(h);
    }
  }
}

void I2cCopy(uint8_t* rx, uint16_t n) {
  for (uint16_t i = 0U; i < n; ++i) {
    rx[i] = (i < S().i2c_reply.size()) ? S().i2c_reply[i] : 0xFFU;
  }
}

void SpiClock(const uint8_t* tx, uint8_t* rx, uint16_t n) {
  const std::size_t offset = S().spi_sent.size();
  for (uint16_t i = 0U; i < n; ++i) {
    S().spi_sent.push_back((tx != nullptr) ? tx[i] : 0U);
    if (rx != nullptr) {
      const std::size_t k = offset + i;
      rx[i] = (k < S().spi_reply.size()) ? S().spi_reply[k] : 0U;
    }
  }
  ++S().spi_transfers;
}
} // namespace

void Reset() {
  S() = State{};
  std::memset(static_cast<void*>(kb_fake_gpio), 0, sizeof(kb_fake_gpio));
  std::memset(static_cast<void*>(kb_fake_tim), 0, sizeof(kb_fake_tim));
  std::memset(static_cast<void*>(kb_fake_usart), 0, sizeof(kb_fake_usart));
  std::memset(static_cast<void*>(kb_fake_i2c), 0, sizeof(kb_fake_i2c));
  kb_fake_dwt.CTRL = 0U;
  kb_fake_coredebug.DEMCR = 0U;
  kb_fake_itm.TCR = 0U;
  kb_fake_itm.TER = 0U;
  kb_fake_itm.PORT[0].u32 = 1U;
  kb_fake_exti_pr = 0U;
  SystemCoreClock = 16000000U;
}
void Fail(const char* function, HAL_StatusTypeDef status, uint32_t error_code, uint32_t count) {
  S().injections.push_back(Injection{function, status, error_code, count});
}
void Hook(const char* function, void (*fn)(void*), void* context) {
  S().hooks.push_back(PendingHook{function, fn, context});
}
uint32_t Calls(const char* function) {
  const auto it = S().calls.find(function);
  return (it == S().calls.end()) ? 0U : it->second;
}
bool ClockEnabled(const char* name) {
  return S().clocks.count(name) != 0U;
}
void SetTickStep(uint32_t step) {
  S().tick_step = step;
}
void SetTickFreq(HAL_TickFreqTypeDef freq) {
  S().tick_freq = freq;
}
void SetTickEvery(uint32_t calls) {
  S().tick_every = (calls == 0U) ? 1U : calls;
}
void SetCounterStep(uint32_t step) {
  S().counter_step = step;
}
void Stall(const char* area, bool on) {
  if (on) {
    S().stalled.insert(area);
  } else {
    S().stalled.erase(area);
  }
}
void SetCycleStep(uint32_t step) {
  S().cycle_step = step;
}
uint32_t Ticks() {
  return S().tick;
}
uint64_t CyclesRead() {
  return S().cycles;
}
void ForceLevel(int port, int pin, int level) {
  fake::Pin& f = S().pins[static_cast<std::size_t>(port)][static_cast<std::size_t>(pin)];
  f.forced = level;
  f.forced_reads = 0U;
}
void ForceLevelFor(int port, int pin, int level, uint32_t reads) {
  fake::Pin& f = S().pins[static_cast<std::size_t>(port)][static_cast<std::size_t>(pin)];
  f.forced = level;
  f.forced_reads = reads;
}
int Driven(int port, int pin) {
  return ((kb_fake_gpio[port].ODR >> static_cast<unsigned>(pin)) & 1U) != 0U ? 1 : 0;
}
uint32_t ModeOf(int port, int pin) {
  return S().pins[static_cast<std::size_t>(port)][static_cast<std::size_t>(pin)].mode;
}
uint32_t AlternateOf(int port, int pin) {
  return S().pins[static_cast<std::size_t>(port)][static_cast<std::size_t>(pin)].alternate;
}
uint32_t PullOf(int port, int pin) {
  return S().pins[static_cast<std::size_t>(port)][static_cast<std::size_t>(pin)].pull;
}
uint32_t Toggles() {
  return S().toggles;
}
bool IrqEnabled(int irq) {
  return S().irq_enabled.count(irq) != 0U;
}
uint32_t IrqPriority(int irq) {
  return S().irq_priority[irq];
}
void ExtiPending(int line) {
  kb_fake_exti_pr |= (1U << static_cast<unsigned>(line));
}
void I2cPresent(uint8_t address, bool present) {
  S().present[address & 0x7FU] = present;
}
void I2cDataNack(bool on) {
  S().data_nack = on;
}
void I2cReply(std::span<const uint8_t> bytes) {
  S().i2c_reply.assign(bytes.begin(), bytes.end());
}
const std::vector<uint8_t>& I2cWritten() {
  return S().i2c_written;
}
uint32_t I2cLastOptions() {
  return S().i2c_options;
}
uint32_t I2cFrames() {
  return S().i2c_frames;
}
void I2cBusyFlag(bool on, bool sticky) {
  S().i2c_busy = on;
  S().i2c_busy_sticky = sticky;
  for (I2C_TypeDef& r : kb_fake_i2c) {
    r.ISR = on ? (r.ISR | I2C_FLAG_BUSY) : (r.ISR & ~I2C_FLAG_BUSY);
  }
}
const I2C_TypeDef& I2cRegs(int instance) {
  return kb_fake_i2c[instance];
}
const I2C_InitTypeDef& I2cInit() {
  return S().i2c_init;
}
void SpiReply(std::span<const uint8_t> bytes) {
  S().spi_reply.assign(bytes.begin(), bytes.end());
}
const std::vector<uint8_t>& SpiSent() {
  return S().spi_sent;
}
uint32_t SpiTransfers() {
  return S().spi_transfers;
}
const SPI_InitTypeDef& SpiInit() {
  return S().spi_init;
}
const std::vector<uint8_t>& UartSent() {
  return S().uart_sent;
}
const UART_InitTypeDef& UartInit() {
  return S().uart_init;
}
uint32_t UartInterrupts(int) {
  return 0U;
}
void AdcRaw(uint32_t raw) {
  S().adc_raw = raw;
}
uint32_t AdcLastChannel() {
  return S().adc_channel;
}
uint32_t AdcLastSampling() {
  return S().adc_sampling;
}
const TIM_Base_InitTypeDef& TimInit(int timer) {
  return S().tim_init[static_cast<std::size_t>(timer)];
}
void CanTxFree(uint32_t free) {
  S().can_tx_free = free;
}
void CanFeed(const Frame& frame) {
  constexpr std::size_t kFifo = 3U; // G4 FDCAN and bxCAN RX FIFO 0
  if (S().can_rx.size() < kFifo) {
    S().can_rx.push_back(frame);
  } else {
    S().can_lost = true; // overrun: the new frame is lost
  }
  if (S().can_rx_irq) {
    CanRxInterrupt(true);
  }
}
void CanRxInterrupt(bool on) {
  S().can_rx_irq = on;
  if (on && S().can_notify && (S().can_handle != nullptr) && !S().can_rx.empty()) {
    const bool lost = S().can_lost;
    S().can_lost = false;
#if !defined(KB_FAKE_STM32F4)
    HAL_FDCAN_RxFifo0Callback(
        static_cast<FDCAN_HandleTypeDef*>(S().can_handle),
        FDCAN_IT_RX_FIFO0_NEW_MESSAGE | (lost ? FDCAN_IT_RX_FIFO0_MESSAGE_LOST : 0U));
#else
    auto* h = static_cast<CAN_HandleTypeDef*>(S().can_handle);
    if (lost) {
      h->ErrorCode |= HAL_CAN_ERROR_RX_FOV0;
      HAL_CAN_ErrorCallback(h);
    }
    HAL_CAN_RxFifo0MsgPendingCallback(h);
#endif
  }
}
const std::vector<Frame>& CanSent() {
  return S().can_tx;
}
void CanBusOff(bool on, uint32_t reads_after_restart) {
  S().bus_off = on;
  S().recovering = false;
  S().bus_off_reads = reads_after_restart;
}
void CanCounters(uint32_t tec, uint32_t rec) {
  S().tec = tec;
  S().rec = rec;
}
uint32_t CanPrescaler() {
  return S().can_prescaler;
}
uint32_t CanMode() {
  return S().can_mode;
}

} // namespace fake

using fake::Injected;
using fake::S;

KbFakeCounter::operator uint32_t() {
  static_cast<void>(Injected("time_read"));
  const uint32_t now = value;
  value += S().counter_step;
  return now;
}

KbFakeTdr& KbFakeTdr::operator=(uint32_t v) {
  static_cast<void>(Injected("UART_TDR"));
  S().uart_sent.push_back(static_cast<uint8_t>(v));
  return *this;
}

KbFakeDr& KbFakeDr::operator=(uint32_t v) {
  static_cast<void>(Injected("UART_TDR"));
  S().uart_sent.push_back(static_cast<uint8_t>(v));
  return *this;
}

KbFakeCycles::operator uint32_t() const {
  static_cast<void>(Injected("time_read"));
  S().cycles += S().cycle_step;
  return static_cast<uint32_t>(S().cycles);
}

extern "C" {

// --- basics, CMSIS -------------------------------------------------------------------------------
uint32_t HAL_GetTick(void) {
  static_cast<void>(Injected("HAL_GetTick"));
  const uint32_t now = S().tick;
  ++S().tick_calls;
  if ((S().tick_calls % S().tick_every) == 0U) {
    S().tick += S().tick_step;
  }
  return now;
}
HAL_TickFreqTypeDef HAL_GetTickFreq(void) {
  return S().tick_freq;
}
uint32_t HAL_RCC_GetHCLKFreq(void) {
  return 160000000U;
}
uint32_t HAL_RCC_GetPCLK1Freq(void) {
  return 80000000U;
}
uint32_t HAL_RCC_GetPCLK2Freq(void) {
  return 160000000U;
}
uint32_t HAL_RCCEx_GetPeriphCLKFreq(uint32_t) {
  return 40000000U;
}
void HAL_NVIC_SetPriority(IRQn_Type irq, uint32_t preempt, uint32_t) {
  S().irq_priority[static_cast<int>(irq)] = preempt;
}
void HAL_NVIC_EnableIRQ(IRQn_Type irq) {
  S().irq_enabled.insert(static_cast<int>(irq));
}
void HAL_NVIC_DisableIRQ(IRQn_Type irq) {
  S().irq_enabled.erase(static_cast<int>(irq));
}
void kb_fake_clock_enable(const char* name) {
  S().clocks.insert(name);
}
uint32_t __get_PRIMASK(void) {
  return S().primask;
}
void __set_PRIMASK(uint32_t primask) {
  S().primask = primask;
}
void __disable_irq(void) {
  S().primask = 1U;
}

// --- GPIO ----------------------------------------------------------------------------------------
void HAL_GPIO_Init(GPIO_TypeDef* port, const GPIO_InitTypeDef* init) {
  static_cast<void>(Injected("HAL_GPIO_Init"));
  const int p = fake::PortIndex(port);
  for (int pin = 0; pin < 16; ++pin) {
    if ((init->Pin & (1U << static_cast<unsigned>(pin))) != 0U) {
      fake::Pin& f = S().pins[static_cast<std::size_t>(p)][static_cast<std::size_t>(pin)];
      f.mode = init->Mode;
      f.pull = init->Pull;
      f.alternate = init->Alternate;
      port->PUPDR = (port->PUPDR & ~(3U << (2U * static_cast<unsigned>(pin)))) |
                    (init->Pull << (2U * static_cast<unsigned>(pin)));
    }
  }
}
void HAL_GPIO_DeInit(GPIO_TypeDef* port, uint32_t pin) {
  static_cast<void>(Injected("HAL_GPIO_DeInit"));
  const int p = fake::PortIndex(port);
  fake::Pin& f =
      S().pins[static_cast<std::size_t>(p)][static_cast<std::size_t>(fake::PinIndex(pin))];
  f.mode = GPIO_MODE_ANALOG;
  f.pull = GPIO_NOPULL;
  port->PUPDR &= ~(3U << (2U * static_cast<unsigned>(fake::PinIndex(pin))));
}
void HAL_GPIO_WritePin(GPIO_TypeDef* port, uint16_t pin, GPIO_PinState state) {
  static_cast<void>(Injected("HAL_GPIO_WritePin"));
  port->ODR =
      (state == GPIO_PIN_SET) ? (port->ODR | pin) : (port->ODR & ~static_cast<uint32_t>(pin));
}
GPIO_PinState HAL_GPIO_ReadPin(const GPIO_TypeDef* port, uint16_t pin) {
  static_cast<void>(Injected("HAL_GPIO_ReadPin"));
  const int p = fake::PortIndex(port);
  fake::Pin& f =
      S().pins[static_cast<std::size_t>(p)][static_cast<std::size_t>(fake::PinIndex(pin))];
  bool high = false;
  if (f.forced >= 0) {
    high = f.forced != 0;
    if (f.forced_reads > 0U) {
      --f.forced_reads;
      if (f.forced_reads == 0U) {
        f.forced = -1; // the last forced read: the model's level from the next one
      }
    }
  } else if (fake::IsOutput(f.mode)) {
    high = (port->ODR & pin) != 0U;
  } else {
    high = f.pull == GPIO_PULLUP;
  }
  return high ? GPIO_PIN_SET : GPIO_PIN_RESET;
}
void HAL_GPIO_TogglePin(GPIO_TypeDef* port, uint16_t pin) {
  static_cast<void>(Injected("HAL_GPIO_TogglePin"));
  ++S().toggles;
  port->ODR ^= pin;
}

// --- timers --------------------------------------------------------------------------------------
HAL_StatusTypeDef HAL_TIM_PWM_Init(TIM_HandleTypeDef* h) {
  HAL_StatusTypeDef st = HAL_OK;
  uint32_t ec = 0U;
  if (Injected("HAL_TIM_PWM_Init", st, ec)) {
    return st;
  }
  S().tim_init[static_cast<std::size_t>(h->Instance - kb_fake_tim)] = h->Init;
  h->Instance->PSC = h->Init.Prescaler;
  h->Instance->ARR = h->Init.Period;
  return HAL_OK;
}
HAL_StatusTypeDef HAL_TIM_PWM_DeInit(TIM_HandleTypeDef*) {
  HAL_StatusTypeDef st = HAL_OK;
  uint32_t ec = 0U;
  return Injected("HAL_TIM_PWM_DeInit", st, ec) ? st : HAL_OK;
}
HAL_StatusTypeDef HAL_TIM_PWM_ConfigChannel(TIM_HandleTypeDef*, const TIM_OC_InitTypeDef*,
                                            uint32_t) {
  HAL_StatusTypeDef st = HAL_OK;
  uint32_t ec = 0U;
  return Injected("HAL_TIM_PWM_ConfigChannel", st, ec) ? st : HAL_OK;
}
HAL_StatusTypeDef HAL_TIM_PWM_Start(TIM_HandleTypeDef*, uint32_t) {
  HAL_StatusTypeDef st = HAL_OK;
  uint32_t ec = 0U;
  return Injected("HAL_TIM_PWM_Start", st, ec) ? st : HAL_OK;
}
HAL_StatusTypeDef HAL_TIM_PWM_Stop(TIM_HandleTypeDef*, uint32_t) {
  HAL_StatusTypeDef st = HAL_OK;
  uint32_t ec = 0U;
  return Injected("HAL_TIM_PWM_Stop", st, ec) ? st : HAL_OK;
}
HAL_StatusTypeDef HAL_TIM_Base_Init(TIM_HandleTypeDef* h) {
  HAL_StatusTypeDef st = HAL_OK;
  uint32_t ec = 0U;
  if (Injected("HAL_TIM_Base_Init", st, ec)) {
    return st;
  }
  S().tim_init[static_cast<std::size_t>(h->Instance - kb_fake_tim)] = h->Init;
  return HAL_OK;
}
HAL_StatusTypeDef HAL_TIM_Base_Start(TIM_HandleTypeDef*) {
  HAL_StatusTypeDef st = HAL_OK;
  uint32_t ec = 0U;
  return Injected("HAL_TIM_Base_Start", st, ec) ? st : HAL_OK;
}
HAL_StatusTypeDef HAL_TIM_GenerateEvent(TIM_HandleTypeDef*, uint32_t) {
  HAL_StatusTypeDef st = HAL_OK;
  uint32_t ec = 0U;
  return Injected("HAL_TIM_GenerateEvent", st, ec) ? st : HAL_OK;
}

// --- ADC -----------------------------------------------------------------------------------------
HAL_StatusTypeDef HAL_ADC_Init(ADC_HandleTypeDef* h) {
  FAKE_INJECT("HAL_ADC_Init", h);
  return HAL_OK;
}
HAL_StatusTypeDef HAL_ADC_DeInit(ADC_HandleTypeDef* h) {
  FAKE_INJECT("HAL_ADC_DeInit", h);
  return HAL_OK;
}
HAL_StatusTypeDef HAL_ADC_ConfigChannel(ADC_HandleTypeDef* h, const ADC_ChannelConfTypeDef* c) {
  FAKE_INJECT("HAL_ADC_ConfigChannel", h);
  S().adc_channel = c->Channel;
  S().adc_sampling = c->SamplingTime;
  return HAL_OK;
}
HAL_StatusTypeDef HAL_ADC_Start_IT(ADC_HandleTypeDef* h) {
  FAKE_INJECT("HAL_ADC_Start_IT", h);
  if (!fake::Stalled("ADC")) {
    HAL_StatusTypeDef st = HAL_OK;
    uint32_t ec = 0U;
    if (Injected("conversion", st, ec)) { // an error ends the conversion instead
      h->ErrorCode = ec;
      HAL_ADC_ErrorCallback(h);
    } else {
      HAL_ADC_ConvCpltCallback(h);
    }
  }
  return HAL_OK;
}
HAL_StatusTypeDef HAL_ADC_Stop_IT(ADC_HandleTypeDef* h) {
  FAKE_INJECT("HAL_ADC_Stop_IT", h);
  return HAL_OK;
}
void HAL_ADC_IRQHandler(ADC_HandleTypeDef*) {
  static_cast<void>(Injected("HAL_ADC_IRQHandler"));
}
uint32_t HAL_ADC_GetValue(const ADC_HandleTypeDef*) {
  return S().adc_raw;
}
HAL_StatusTypeDef HAL_ADCEx_Calibration_Start(ADC_HandleTypeDef* h, uint32_t) {
  FAKE_INJECT("HAL_ADCEx_Calibration_Start", h);
  return HAL_OK;
}

// --- I2C -----------------------------------------------------------------------------------------
HAL_StatusTypeDef HAL_I2C_Init(I2C_HandleTypeDef* h) {
  FAKE_INJECT("HAL_I2C_Init", h);
  S().i2c_init = h->Init;
  return HAL_OK;
}
HAL_StatusTypeDef HAL_I2C_DeInit(I2C_HandleTypeDef* h) {
  FAKE_INJECT("HAL_I2C_DeInit", h);
  if ((h->Instance != nullptr) && !S().i2c_busy_sticky) {
    h->Instance->ISR &= ~I2C_FLAG_BUSY; // PE low resets the peripheral's state machine
  }
  return HAL_OK;
}
HAL_StatusTypeDef HAL_I2C_Master_Transmit_IT(I2C_HandleTypeDef* h, uint16_t address, uint8_t*,
                                             uint16_t size) {
  FAKE_INJECT("HAL_I2C_Master_Transmit_IT", h);
  if ((h->Instance->ISR & I2C_FLAG_BUSY) != 0U) {
    return HAL_BUSY;
  }
  S().i2c_written.clear();
  HAL_StatusTypeDef st = HAL_OK;
  if (!S().present[(address >> 1U) & 0x7FU] || ((size > 0U) && S().data_nack)) {
    h->ErrorCode = HAL_I2C_ERROR_AF;
    st = HAL_ERROR;
  }
  fake::I2cDone(h, st);
  return HAL_OK;
}
HAL_StatusTypeDef HAL_I2C_Master_Seq_Transmit_IT(I2C_HandleTypeDef* h, uint16_t address,
                                                 uint8_t* data, uint16_t size, uint32_t options) {
  FAKE_INJECT("HAL_I2C_Master_Seq_Transmit_IT", h);
  if ((h->Instance->ISR & I2C_FLAG_BUSY) != 0U) {
    return HAL_BUSY;
  }
  S().i2c_options = options;
  S().i2c_written.assign(data, data + size);
  fake::I2cDone(h, fake::I2cAck(address, h));
  return HAL_OK;
}
HAL_StatusTypeDef HAL_I2C_Master_Seq_Receive_IT(I2C_HandleTypeDef* h, uint16_t address,
                                                uint8_t* data, uint16_t size, uint32_t options) {
  FAKE_INJECT("HAL_I2C_Master_Seq_Receive_IT", h);
  if (((h->Instance->ISR & I2C_FLAG_BUSY) != 0U) && (options == I2C_FIRST_AND_LAST_FRAME)) {
    return HAL_BUSY;
  }
  S().i2c_options = options;
  HAL_StatusTypeDef st = HAL_OK;
  uint32_t ec = 0U;
  if (!S().present[(address >> 1U) & 0x7FU]) {
    h->ErrorCode = HAL_I2C_ERROR_AF;
    st = HAL_ERROR;
  } else if (Injected("i2c_error", st, ec)) {
    h->ErrorCode = ec;
    st = HAL_ERROR;
  } else {
    fake::I2cCopy(data, size);
  }
  ++S().i2c_frames;
  if (!fake::Stalled("I2C")) {
    if (st == HAL_OK) {
      HAL_I2C_MasterRxCpltCallback(h);
    } else {
      HAL_I2C_ErrorCallback(h);
    }
  }
  return HAL_OK;
}
void HAL_I2C_EV_IRQHandler(I2C_HandleTypeDef*) {
  static_cast<void>(Injected("HAL_I2C_EV_IRQHandler"));
}
void HAL_I2C_ER_IRQHandler(I2C_HandleTypeDef*) {
  static_cast<void>(Injected("HAL_I2C_ER_IRQHandler"));
}
HAL_StatusTypeDef HAL_I2CEx_ConfigAnalogFilter(I2C_HandleTypeDef* h, uint32_t) {
  FAKE_INJECT("HAL_I2CEx_ConfigAnalogFilter", h);
  return HAL_OK;
}

// --- SPI -----------------------------------------------------------------------------------------
HAL_StatusTypeDef HAL_SPI_Init(SPI_HandleTypeDef* h) {
  FAKE_INJECT("HAL_SPI_Init", h);
  S().spi_init = h->Init;
  return HAL_OK;
}
HAL_StatusTypeDef HAL_SPI_DeInit(SPI_HandleTypeDef* h) {
  FAKE_INJECT("HAL_SPI_DeInit", h);
  return HAL_OK;
}
HAL_StatusTypeDef HAL_SPI_Transmit_IT(SPI_HandleTypeDef* h, const uint8_t* data, uint16_t size) {
  FAKE_INJECT("HAL_SPI_Transmit_IT", h);
  fake::SpiClock(data, nullptr, size);
  if (!fake::Stalled("SPI")) {
    HAL_StatusTypeDef st = HAL_OK;
    uint32_t ec = 0U;
    if (Injected("spi_error", st, ec)) {
      h->ErrorCode = ec;
      HAL_SPI_ErrorCallback(h);
    } else {
      HAL_SPI_TxCpltCallback(h);
    }
  }
  return HAL_OK;
}
HAL_StatusTypeDef HAL_SPI_TransmitReceive_IT(SPI_HandleTypeDef* h, const uint8_t* tx, uint8_t* rx,
                                             uint16_t size) {
  FAKE_INJECT("HAL_SPI_TransmitReceive_IT", h);
  fake::SpiClock(tx, rx, size);
  if (!fake::Stalled("SPI")) {
    HAL_StatusTypeDef st = HAL_OK;
    uint32_t ec = 0U;
    if (Injected("spi_error", st, ec)) { // a mode fault or overrun ends the transfer instead
      h->ErrorCode = ec;
      HAL_SPI_ErrorCallback(h);
    } else {
      HAL_SPI_TxRxCpltCallback(h);
    }
  }
  return HAL_OK;
}
void HAL_SPI_IRQHandler(SPI_HandleTypeDef*) {
  static_cast<void>(Injected("HAL_SPI_IRQHandler"));
}
HAL_StatusTypeDef HAL_SPI_Abort(SPI_HandleTypeDef* h) {
  FAKE_INJECT("HAL_SPI_Abort", h);
  return HAL_OK;
}

// --- UART ----------------------------------------------------------------------------------------
HAL_StatusTypeDef HAL_UART_Init(UART_HandleTypeDef* h) {
  FAKE_INJECT("HAL_UART_Init", h);
  S().uart_init = h->Init;
  return HAL_OK;
}
HAL_StatusTypeDef HAL_UART_DeInit(UART_HandleTypeDef* h) {
  FAKE_INJECT("HAL_UART_DeInit", h);
  return HAL_OK;
}
namespace {
// The port's interrupt entry, reached through a pointer: the fake stands in for the hardware, which
// raises the interrupt from outside the call chain, so the port's functions do not call themselves
// through the fake (Rule 8.2.10).
void (*const g_uart_irq)(uint8_t) noexcept = &kb::port::stm32::HandleUartIrq;
} // namespace
void kb_fake_uart_it(UART_HandleTypeDef* h, uint32_t it, int on) {
  h->Interrupts = (on != 0) ? (h->Interrupts | it) : (h->Interrupts & ~it);
  if ((on != 0) && ((it & (UART_IT_TXE | UART_IT_TC)) != 0U) && !S().uart_running &&
      !fake::Stalled("UART")) {
    // The hardware sends at once: transmit-empty and transmit-complete stay set, and the
    // interrupt runs while one of them is enabled.
    S().uart_running = true;
    USART_TypeDef* const u = h->Instance;
    const auto index = static_cast<std::size_t>(u - kb_fake_usart);
    const uint8_t instance =
        (index == 11U) ? kb::port::stm32::kLpuart1 : static_cast<uint8_t>(index);
    for (int i = 0; (i < 1000000) && ((h->Interrupts & (UART_IT_TXE | UART_IT_TC)) != 0U); ++i) {
#if defined(KB_FAKE_STM32F4)
      const uint32_t before = u->SR;
      u->SR = before | USART_SR_TXE | USART_SR_TC;
      g_uart_irq(instance);
      u->SR = before;
#else
      const uint32_t before = u->ISR;
      u->ISR = before | USART_ISR_TXE_TXFNF | USART_ISR_TC;
      g_uart_irq(instance);
      u->ISR = before;
#endif
    }
    S().uart_running = false;
  }
}

// --- CAN -----------------------------------------------------------------------------------------
namespace {
// One status read: a recovering controller leaves bus-off after the configured number of reads.
bool BusOffNow(bool recovering) {
  if (S().bus_off && recovering) {
    if (S().bus_off_reads > 0U) {
      --S().bus_off_reads;
    }
    if (S().bus_off_reads == 0U) {
      S().bus_off = false;
    }
  }
  return S().bus_off;
}
} // namespace
#if !defined(KB_FAKE_STM32F4)
HAL_StatusTypeDef HAL_FDCAN_ActivateNotification(FDCAN_HandleTypeDef* h, uint32_t, uint32_t) {
  FAKE_INJECT("HAL_FDCAN_ActivateNotification", h);
  S().can_notify = true;
  S().can_handle = h;
  return HAL_OK;
}
void HAL_FDCAN_IRQHandler(FDCAN_HandleTypeDef*) {
  static_cast<void>(Injected("HAL_FDCAN_IRQHandler"));
}
HAL_StatusTypeDef HAL_FDCAN_Init(FDCAN_HandleTypeDef* h) {
  FAKE_INJECT("HAL_FDCAN_Init", h);
  S().can_prescaler = h->Init.NominalPrescaler;
  S().can_mode = h->Init.Mode;
  return HAL_OK;
}
HAL_StatusTypeDef HAL_FDCAN_DeInit(FDCAN_HandleTypeDef* h) {
  S().can_notify = false;
  FAKE_INJECT("HAL_FDCAN_DeInit", h);
  return HAL_OK;
}
HAL_StatusTypeDef HAL_FDCAN_Start(FDCAN_HandleTypeDef* h) {
  FAKE_INJECT("HAL_FDCAN_Start", h);
  S().recovering = S().bus_off; // leaving INIT starts the recovery
  return HAL_OK;
}
HAL_StatusTypeDef HAL_FDCAN_Stop(FDCAN_HandleTypeDef* h) {
  FAKE_INJECT("HAL_FDCAN_Stop", h);
  return HAL_OK;
}
HAL_StatusTypeDef HAL_FDCAN_ConfigGlobalFilter(FDCAN_HandleTypeDef* h, uint32_t, uint32_t, uint32_t,
                                               uint32_t) {
  FAKE_INJECT("HAL_FDCAN_ConfigGlobalFilter", h);
  return HAL_OK;
}
uint32_t HAL_FDCAN_GetTxFifoFreeLevel(const FDCAN_HandleTypeDef*) {
  return S().can_tx_free;
}
uint32_t HAL_FDCAN_GetRxFifoFillLevel(const FDCAN_HandleTypeDef*, uint32_t) {
  return static_cast<uint32_t>(S().can_rx.size());
}
HAL_StatusTypeDef HAL_FDCAN_AddMessageToTxFifoQ(FDCAN_HandleTypeDef* h,
                                                const FDCAN_TxHeaderTypeDef* header,
                                                const uint8_t* data) {
  FAKE_INJECT("HAL_FDCAN_AddMessageToTxFifoQ", h);
  fake::Frame f{};
  f.id = header->Identifier;
  f.extended = header->IdType == FDCAN_EXTENDED_ID;
  f.remote = header->TxFrameType == FDCAN_REMOTE_FRAME;
  f.dlc = header->DataLength;
  std::memcpy(f.data, data, 8U);
  S().can_tx.push_back(f);
  if (S().can_notify) {
    HAL_FDCAN_TxBufferCompleteCallback(h, FDCAN_TX_BUFFER0);
  }
  return HAL_OK;
}
HAL_StatusTypeDef HAL_FDCAN_GetRxMessage(FDCAN_HandleTypeDef* h, uint32_t,
                                         FDCAN_RxHeaderTypeDef* header, uint8_t* data) {
  FAKE_INJECT("HAL_FDCAN_GetRxMessage", h);
  const fake::Frame f = S().can_rx.front();
  S().can_rx.pop_front();
  header->Identifier = f.id;
  header->IdType = f.extended ? FDCAN_EXTENDED_ID : FDCAN_STANDARD_ID;
  header->RxFrameType = f.remote ? FDCAN_REMOTE_FRAME : FDCAN_DATA_FRAME;
  header->DataLength = f.dlc;
  std::memcpy(data, f.data, 8U);
  return HAL_OK;
}
HAL_StatusTypeDef HAL_FDCAN_GetProtocolStatus(const FDCAN_HandleTypeDef*,
                                              FDCAN_ProtocolStatusTypeDef* ps) {
  HAL_StatusTypeDef st = HAL_OK;
  uint32_t ec = 0U;
  if (Injected("HAL_FDCAN_GetProtocolStatus", st, ec)) {
    return st;
  }
  ps->BusOff = BusOffNow(S().recovering) ? 1U : 0U;
  ps->ErrorPassive = ((S().tec >= 128U) || (S().rec >= 128U)) ? 1U : 0U;
  ps->LastErrorCode = 3U;
  return HAL_OK;
}
HAL_StatusTypeDef HAL_FDCAN_GetErrorCounters(const FDCAN_HandleTypeDef*,
                                             FDCAN_ErrorCountersTypeDef* ec) {
  HAL_StatusTypeDef st = HAL_OK;
  uint32_t code = 0U;
  if (Injected("HAL_FDCAN_GetErrorCounters", st, code)) {
    return st;
  }
  ec->TxErrorCnt = S().tec;
  ec->RxErrorCnt = S().rec;
  return HAL_OK;
}
#else
HAL_StatusTypeDef HAL_CAN_ActivateNotification(CAN_HandleTypeDef* h, uint32_t) {
  FAKE_INJECT("HAL_CAN_ActivateNotification", h);
  S().can_notify = true;
  S().can_handle = h;
  return HAL_OK;
}
HAL_StatusTypeDef HAL_CAN_ResetError(CAN_HandleTypeDef* h) {
  h->ErrorCode = 0U;
  return HAL_OK;
}
void HAL_CAN_IRQHandler(CAN_HandleTypeDef*) {
  static_cast<void>(Injected("HAL_CAN_IRQHandler"));
}
HAL_StatusTypeDef HAL_CAN_Init(CAN_HandleTypeDef* h) {
  FAKE_INJECT("HAL_CAN_Init", h);
  S().can_prescaler = h->Init.Prescaler;
  S().can_mode = h->Init.Mode;
  return HAL_OK;
}
HAL_StatusTypeDef HAL_CAN_DeInit(CAN_HandleTypeDef* h) {
  S().can_notify = false;
  FAKE_INJECT("HAL_CAN_DeInit", h);
  return HAL_OK;
}
HAL_StatusTypeDef HAL_CAN_ConfigFilter(CAN_HandleTypeDef* h, const CAN_FilterTypeDef*) {
  FAKE_INJECT("HAL_CAN_ConfigFilter", h);
  return HAL_OK;
}
HAL_StatusTypeDef HAL_CAN_Start(CAN_HandleTypeDef* h) {
  FAKE_INJECT("HAL_CAN_Start", h);
  return HAL_OK;
}
HAL_StatusTypeDef HAL_CAN_Stop(CAN_HandleTypeDef* h) {
  FAKE_INJECT("HAL_CAN_Stop", h);
  return HAL_OK;
}
uint32_t HAL_CAN_GetTxMailboxesFreeLevel(const CAN_HandleTypeDef*) {
  return S().can_tx_free;
}
uint32_t HAL_CAN_GetRxFifoFillLevel(const CAN_HandleTypeDef*, uint32_t) {
  return static_cast<uint32_t>(S().can_rx.size());
}
HAL_StatusTypeDef HAL_CAN_AddTxMessage(CAN_HandleTypeDef* h, const CAN_TxHeaderTypeDef* header,
                                       const uint8_t* data, uint32_t* mailbox) {
  FAKE_INJECT("HAL_CAN_AddTxMessage", h);
  fake::Frame f{};
  f.extended = header->IDE == CAN_ID_EXT;
  f.id = f.extended ? header->ExtId : header->StdId;
  f.remote = header->RTR == CAN_RTR_REMOTE;
  f.dlc = header->DLC;
  std::memcpy(f.data, data, 8U);
  S().can_tx.push_back(f);
  *mailbox = 1U;
  if (S().can_notify) {
    HAL_CAN_TxMailbox0CompleteCallback(h);
  }
  return HAL_OK;
}
HAL_StatusTypeDef HAL_CAN_GetRxMessage(CAN_HandleTypeDef* h, uint32_t, CAN_RxHeaderTypeDef* header,
                                       uint8_t* data) {
  FAKE_INJECT("HAL_CAN_GetRxMessage", h);
  const fake::Frame f = S().can_rx.front();
  S().can_rx.pop_front();
  header->IDE = f.extended ? CAN_ID_EXT : CAN_ID_STD;
  header->StdId = f.extended ? 0U : f.id;
  header->ExtId = f.extended ? f.id : 0U;
  header->RTR = f.remote ? CAN_RTR_REMOTE : CAN_RTR_DATA;
  header->DLC = f.dlc;
  std::memcpy(data, f.data, 8U);
  return HAL_OK;
}
#endif

} // extern "C"

#if defined(KB_FAKE_STM32F4)
KbFakeEsr::operator uint32_t() const {
  // bxCAN recovers from bus-off by itself (automatic bus-off management).
  const bool off = BusOffNow(true);
  return (off ? CAN_ESR_BOFF : 0U) | (3U << CAN_ESR_LEC_Pos) | (S().tec << CAN_ESR_TEC_Pos) |
         (S().rec << CAN_ESR_REC_Pos);
}
#endif

// The STM32 HAL fake: definitions of every HAL and CMSIS function the boundary files call (the link
// seam), with error injection by function name and a small model of each peripheral. Test code.
// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The port-stm32 authors
#include "fake_stm32_hal.hpp"

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
};

struct State {
  std::vector<Injection> injections;
  std::vector<PendingHook> hooks;
  std::map<std::string, uint32_t> calls;
  std::set<std::string> clocks;
  uint32_t tick{0U};
  uint32_t tick_step{1U};
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
  uint16_t mem_address{0U};
  uint16_t mem_size{0U};
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
  S().pins[static_cast<std::size_t>(port)][static_cast<std::size_t>(pin)].forced = level;
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
uint16_t I2cMemAddress() {
  return S().mem_address;
}
uint16_t I2cMemSize() {
  return S().mem_size;
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
  S().can_rx.push_back(frame);
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

KbFakeCycles::operator uint32_t() const {
  S().cycles += S().cycle_step;
  return static_cast<uint32_t>(S().cycles);
}

extern "C" {

// --- basics, CMSIS -------------------------------------------------------------------------------
uint32_t HAL_GetTick(void) {
  static_cast<void>(Injected("HAL_GetTick"));
  const uint32_t now = S().tick;
  S().tick += S().tick_step;
  return now;
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
  const fake::Pin& f =
      S().pins[static_cast<std::size_t>(p)][static_cast<std::size_t>(fake::PinIndex(pin))];
  bool high = false;
  if (f.forced >= 0) {
    high = f.forced != 0;
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
HAL_StatusTypeDef HAL_ADC_Start(ADC_HandleTypeDef* h) {
  FAKE_INJECT("HAL_ADC_Start", h);
  return HAL_OK;
}
HAL_StatusTypeDef HAL_ADC_PollForConversion(ADC_HandleTypeDef* h, uint32_t) {
  FAKE_INJECT("HAL_ADC_PollForConversion", h);
  return HAL_OK;
}
uint32_t HAL_ADC_GetValue(const ADC_HandleTypeDef*) {
  return S().adc_raw;
}
HAL_StatusTypeDef HAL_ADC_Stop(ADC_HandleTypeDef* h) {
  FAKE_INJECT("HAL_ADC_Stop", h);
  return HAL_OK;
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
  return HAL_OK;
}
HAL_StatusTypeDef HAL_I2C_Master_Transmit(I2C_HandleTypeDef* h, uint16_t address, uint8_t* data,
                                          uint16_t size, uint32_t) {
  FAKE_INJECT("HAL_I2C_Master_Transmit", h);
  S().i2c_written.assign(data, data + size);
  return fake::I2cAck(address, h);
}
HAL_StatusTypeDef HAL_I2C_Master_Receive(I2C_HandleTypeDef* h, uint16_t address, uint8_t* data,
                                         uint16_t size, uint32_t) {
  FAKE_INJECT("HAL_I2C_Master_Receive", h);
  const HAL_StatusTypeDef st = fake::I2cAck(address, h);
  if (st == HAL_OK) {
    fake::I2cCopy(data, size);
  }
  return st;
}
HAL_StatusTypeDef HAL_I2C_Mem_Read(I2C_HandleTypeDef* h, uint16_t address, uint16_t mem,
                                   uint16_t mem_size, uint8_t* data, uint16_t size, uint32_t) {
  FAKE_INJECT("HAL_I2C_Mem_Read", h);
  S().mem_address = mem;
  S().mem_size = mem_size;
  const HAL_StatusTypeDef st = fake::I2cAck(address, h);
  if (st == HAL_OK) {
    fake::I2cCopy(data, size);
  }
  return st;
}
HAL_StatusTypeDef HAL_I2C_IsDeviceReady(I2C_HandleTypeDef* h, uint16_t address, uint32_t,
                                        uint32_t) {
  FAKE_INJECT("HAL_I2C_IsDeviceReady", h);
  if (!S().present[(address >> 1U) & 0x7FU]) {
    h->ErrorCode = HAL_I2C_ERROR_AF;
    return HAL_ERROR;
  }
  return HAL_OK;
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
HAL_StatusTypeDef HAL_SPI_Transmit(SPI_HandleTypeDef* h, const uint8_t* data, uint16_t size,
                                   uint32_t) {
  FAKE_INJECT("HAL_SPI_Transmit", h);
  fake::SpiClock(data, nullptr, size);
  return HAL_OK;
}
HAL_StatusTypeDef HAL_SPI_Receive(SPI_HandleTypeDef* h, uint8_t* data, uint16_t size, uint32_t) {
  FAKE_INJECT("HAL_SPI_Receive", h);
  std::vector<uint8_t> out(data, data + size); // the HAL clocks out the receive buffer
  fake::SpiClock(out.data(), data, size);
  return HAL_OK;
}
HAL_StatusTypeDef HAL_SPI_TransmitReceive(SPI_HandleTypeDef* h, const uint8_t* tx, uint8_t* rx,
                                          uint16_t size, uint32_t) {
  FAKE_INJECT("HAL_SPI_TransmitReceive", h);
  fake::SpiClock(tx, rx, size);
  return HAL_OK;
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
HAL_StatusTypeDef HAL_UART_Transmit(UART_HandleTypeDef* h, const uint8_t* data, uint16_t size,
                                    uint32_t) {
  FAKE_INJECT("HAL_UART_Transmit", h);
  S().uart_sent.insert(S().uart_sent.end(), data, data + size);
  return HAL_OK;
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
HAL_StatusTypeDef HAL_FDCAN_Init(FDCAN_HandleTypeDef* h) {
  FAKE_INJECT("HAL_FDCAN_Init", h);
  S().can_prescaler = h->Init.NominalPrescaler;
  S().can_mode = h->Init.Mode;
  return HAL_OK;
}
HAL_StatusTypeDef HAL_FDCAN_DeInit(FDCAN_HandleTypeDef* h) {
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
HAL_StatusTypeDef HAL_CAN_Init(CAN_HandleTypeDef* h) {
  FAKE_INJECT("HAL_CAN_Init", h);
  S().can_prescaler = h->Init.Prescaler;
  S().can_mode = h->Init.Mode;
  return HAL_OK;
}
HAL_StatusTypeDef HAL_CAN_DeInit(CAN_HandleTypeDef* h) {
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

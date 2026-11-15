/*
 * Host fake of the STM32 HAL and CMSIS declarations the port's boundary files use: the same names,
 * types, macros and signatures (a subset), so the boundary files compile on the host unchanged and
 * link against tests/fakes/stm32/fake_stm32_hal.cpp (a link seam per HAL call). Two families: the
 * default is G4-like (I2C v2 with TIMINGR, USART ISR/ICR/RDR/TDR, FDCAN, ADC calibration, a 7-bit
 * UART word); KB_FAKE_STM32F4 is F4-like (I2C v1, USART SR/DR, bxCAN, no ADC calibration, no
 * 7-bit UART word): each variant has only the macros its family's HAL has. The interrupt APIs
 * (`*_IT`) call the HAL callbacks the port defines at once, as the interrupt would, unless the test
 * stalls them (fake::Stall). Every HAL header name the boundary files include is a one-line file
 * next to this one.
 *
 * Test code. SPDX-License-Identifier: MIT
 * Copyright (c) 2026 The port-stm32 authors
 */
#ifndef KB_FAKE_STM32_HAL_H
#define KB_FAKE_STM32_HAL_H

#include <stddef.h>
#include <stdint.h>

#if defined(KB_FAKE_STM32F4)
#define STM32F4
#else
#define STM32G4
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* --- basics -------------------------------------------------------------------------------------
 */
typedef enum {
  HAL_OK = 0x00U,
  HAL_ERROR = 0x01U,
  HAL_BUSY = 0x02U,
  HAL_TIMEOUT = 0x03U
} HAL_StatusTypeDef;
typedef enum { DISABLE = 0U, ENABLE = 1U } FunctionalState;
typedef enum { RESET = 0U, SET = 1U } FlagStatus;
typedef enum { HAL_UNLOCKED = 0x00U, HAL_LOCKED = 0x01U } HAL_LockTypeDef;
#define __HAL_UNLOCK(h) ((h)->Lock = HAL_UNLOCKED)
typedef enum {
  EXTI0_IRQn = 6,
  EXTI1_IRQn = 7,
  EXTI2_IRQn = 8,
  EXTI3_IRQn = 9,
  EXTI4_IRQn = 10,
  EXTI9_5_IRQn = 23,
  TIM2_IRQn = 28,
  USART1_IRQn = 37,
  USART2_IRQn = 38,
  USART3_IRQn = 39,
  EXTI15_10_IRQn = 40,
  TIM5_IRQn = 50,
  UART4_IRQn = 52,
  UART5_IRQn = 53,
  USART6_IRQn = 71,
  LPUART1_IRQn = 91,
  I2C1_EV_IRQn = 31,
  I2C1_ER_IRQn = 32,
  I2C2_EV_IRQn = 33,
  I2C2_ER_IRQn = 34,
  I2C3_EV_IRQn = 72,
  I2C3_ER_IRQn = 73,
  I2C4_EV_IRQn = 82,
  I2C4_ER_IRQn = 83,
  SPI1_IRQn = 35,
  SPI2_IRQn = 36,
  SPI3_IRQn = 51,
  SPI4_IRQn = 84,
  ADC1_2_IRQn = 18,
  ADC_IRQn = 118,
  ADC3_IRQn = 47,
  ADC4_IRQn = 61,
  ADC5_IRQn = 62,
  FDCAN1_IT0_IRQn = 21,
  FDCAN2_IT0_IRQn = 86,
  CAN1_TX_IRQn = 19,
  CAN1_RX0_IRQn = 20,
  CAN1_SCE_IRQn = 22,
  CAN2_TX_IRQn = 63,
  CAN2_RX0_IRQn = 64,
  CAN2_SCE_IRQn = 66
} IRQn_Type;
extern uint32_t SystemCoreClock;
uint32_t HAL_GetTick(void);
uint32_t HAL_RCC_GetHCLKFreq(void);
uint32_t HAL_RCC_GetPCLK1Freq(void);
uint32_t HAL_RCC_GetPCLK2Freq(void);
uint32_t HAL_RCCEx_GetPeriphCLKFreq(uint32_t clock);
#define RCC_PERIPHCLK_FDCAN 0x00001000U
void HAL_NVIC_SetPriority(IRQn_Type irq, uint32_t preempt, uint32_t sub);
void HAL_NVIC_EnableIRQ(IRQn_Type irq);
void HAL_NVIC_DisableIRQ(IRQn_Type irq);
void kb_fake_clock_enable(const char* name);
#define KB_FAKE_CLK(name) kb_fake_clock_enable(name)

/* --- CMSIS core ---------------------------------------------------------------------------------
 */
uint32_t __get_PRIMASK(void);
void __set_PRIMASK(uint32_t primask);
void __disable_irq(void);
#ifdef __cplusplus
}
/* The cycle counter advances on every read (fake::SetCycleStep), so a busy wait ends on the host.
 */
struct KbFakeCycles {
  operator uint32_t() const;
};
extern "C" {
#endif
typedef struct {
  volatile uint32_t CTRL;
  struct KbFakeCycles CYCCNT;
  volatile uint32_t LAR;
} DWT_Type;
typedef struct {
  volatile uint32_t DEMCR;
} CoreDebug_Type;
typedef union {
  volatile uint8_t u8;
  volatile uint16_t u16;
  volatile uint32_t u32;
} ITM_Port_t;
typedef struct {
  ITM_Port_t PORT[32];
  volatile uint32_t TER;
  volatile uint32_t TCR;
} ITM_Type;
extern DWT_Type kb_fake_dwt;
extern CoreDebug_Type kb_fake_coredebug;
extern ITM_Type kb_fake_itm;
#define DWT (&kb_fake_dwt)
#define CoreDebug (&kb_fake_coredebug)
#define ITM (&kb_fake_itm)
#define DWT_CTRL_CYCCNTENA_Msk (1UL)
#define CoreDebug_DEMCR_TRCENA_Msk (1UL << 24U)
#define ITM_TCR_ITMENA_Msk (1UL)

/* --- GPIO and EXTI ------------------------------------------------------------------------------
 */
typedef struct {
  volatile uint32_t MODER;
  volatile uint32_t PUPDR;
  volatile uint32_t IDR;
  volatile uint32_t ODR;
  volatile uint32_t BSRR;
} GPIO_TypeDef;
extern GPIO_TypeDef kb_fake_gpio[11];
#define GPIOA (&kb_fake_gpio[0])
#define GPIOB (&kb_fake_gpio[1])
#define GPIOC (&kb_fake_gpio[2])
#define GPIOD (&kb_fake_gpio[3])
#define GPIOE (&kb_fake_gpio[4])
#define GPIOF (&kb_fake_gpio[5])
#define GPIOG (&kb_fake_gpio[6])
#if defined(KB_FAKE_STM32F4)
#define GPIOH (&kb_fake_gpio[7])
#endif
#define __HAL_RCC_GPIOA_CLK_ENABLE() KB_FAKE_CLK("GPIOA")
#define __HAL_RCC_GPIOB_CLK_ENABLE() KB_FAKE_CLK("GPIOB")
#define __HAL_RCC_GPIOC_CLK_ENABLE() KB_FAKE_CLK("GPIOC")
#define __HAL_RCC_GPIOD_CLK_ENABLE() KB_FAKE_CLK("GPIOD")
#define __HAL_RCC_GPIOE_CLK_ENABLE() KB_FAKE_CLK("GPIOE")
#define __HAL_RCC_GPIOF_CLK_ENABLE() KB_FAKE_CLK("GPIOF")
#define __HAL_RCC_GPIOG_CLK_ENABLE() KB_FAKE_CLK("GPIOG")
#define __HAL_RCC_GPIOH_CLK_ENABLE() KB_FAKE_CLK("GPIOH")
#define __HAL_RCC_SYSCFG_CLK_ENABLE() KB_FAKE_CLK("SYSCFG")
typedef enum { GPIO_PIN_RESET = 0U, GPIO_PIN_SET = 1U } GPIO_PinState;
typedef struct {
  uint32_t Pin;
  uint32_t Mode;
  uint32_t Pull;
  uint32_t Speed;
  uint32_t Alternate;
} GPIO_InitTypeDef;
#define GPIO_MODE_INPUT 0x00000000U
#define GPIO_MODE_OUTPUT_PP 0x00000001U
#define GPIO_MODE_OUTPUT_OD 0x00000011U
#define GPIO_MODE_AF_PP 0x00000002U
#define GPIO_MODE_AF_OD 0x00000012U
#define GPIO_MODE_ANALOG 0x00000003U
#define GPIO_MODE_IT_RISING 0x10110000U
#define GPIO_MODE_IT_FALLING 0x10210000U
#define GPIO_MODE_IT_RISING_FALLING 0x10310000U
#define GPIO_NOPULL 0x00000000U
#define GPIO_PULLUP 0x00000001U
#define GPIO_PULLDOWN 0x00000002U
#define GPIO_SPEED_FREQ_LOW 0x00000000U
#define GPIO_SPEED_FREQ_MEDIUM 0x00000001U
#define GPIO_SPEED_FREQ_HIGH 0x00000002U
#define GPIO_SPEED_FREQ_VERY_HIGH 0x00000003U
void HAL_GPIO_Init(GPIO_TypeDef* port, const GPIO_InitTypeDef* init);
void HAL_GPIO_DeInit(GPIO_TypeDef* port, uint32_t pin);
void HAL_GPIO_WritePin(GPIO_TypeDef* port, uint16_t pin, GPIO_PinState state);
GPIO_PinState HAL_GPIO_ReadPin(const GPIO_TypeDef* port, uint16_t pin);
void HAL_GPIO_TogglePin(GPIO_TypeDef* port, uint16_t pin);
extern volatile uint32_t kb_fake_exti_pr;
#define __HAL_GPIO_EXTI_GET_IT(line) (kb_fake_exti_pr & (line))
#define __HAL_GPIO_EXTI_CLEAR_IT(line) (kb_fake_exti_pr &= ~(line))

/* --- timers -------------------------------------------------------------------------------------
 */
#ifdef __cplusplus
}
/* The counter runs: each read returns the count, then adds the step (fake::SetCounterStep). */
struct KbFakeCounter {
  uint32_t value;
  operator uint32_t();
  KbFakeCounter& operator=(uint32_t v) {
    value = v;
    return *this;
  }
};
extern "C" {
#endif
typedef struct {
  volatile uint32_t SR;
  volatile uint32_t DIER;
  struct KbFakeCounter CNT;
  volatile uint32_t PSC;
  volatile uint32_t ARR;
  volatile uint32_t CCR[4];
} TIM_TypeDef;
extern TIM_TypeDef kb_fake_tim[21];
#define TIM1 (&kb_fake_tim[1])
#define TIM2 (&kb_fake_tim[2])
#define TIM3 (&kb_fake_tim[3])
#define TIM4 (&kb_fake_tim[4])
#define TIM5 (&kb_fake_tim[5])
#define TIM8 (&kb_fake_tim[8])
#define TIM15 (&kb_fake_tim[15])
#define TIM16 (&kb_fake_tim[16])
#define TIM17 (&kb_fake_tim[17])
#define __HAL_RCC_TIM1_CLK_ENABLE() KB_FAKE_CLK("TIM1")
#define __HAL_RCC_TIM2_CLK_ENABLE() KB_FAKE_CLK("TIM2")
#define __HAL_RCC_TIM3_CLK_ENABLE() KB_FAKE_CLK("TIM3")
#define __HAL_RCC_TIM4_CLK_ENABLE() KB_FAKE_CLK("TIM4")
#define __HAL_RCC_TIM5_CLK_ENABLE() KB_FAKE_CLK("TIM5")
#define __HAL_RCC_TIM8_CLK_ENABLE() KB_FAKE_CLK("TIM8")
#define __HAL_RCC_TIM15_CLK_ENABLE() KB_FAKE_CLK("TIM15")
#define __HAL_RCC_TIM16_CLK_ENABLE() KB_FAKE_CLK("TIM16")
#define __HAL_RCC_TIM17_CLK_ENABLE() KB_FAKE_CLK("TIM17")
typedef struct {
  uint32_t Prescaler;
  uint32_t CounterMode;
  uint32_t Period;
  uint32_t ClockDivision;
  uint32_t RepetitionCounter;
  uint32_t AutoReloadPreload;
} TIM_Base_InitTypeDef;
typedef struct {
  TIM_TypeDef* Instance;
  TIM_Base_InitTypeDef Init;
} TIM_HandleTypeDef;
typedef struct {
  uint32_t OCMode;
  uint32_t Pulse;
  uint32_t OCPolarity;
  uint32_t OCNPolarity;
  uint32_t OCFastMode;
  uint32_t OCIdleState;
  uint32_t OCNIdleState;
} TIM_OC_InitTypeDef;
#define TIM_COUNTERMODE_UP 0x00000000U
#define TIM_CLOCKDIVISION_DIV1 0x00000000U
#define TIM_AUTORELOAD_PRELOAD_DISABLE 0x00000000U
#define TIM_AUTORELOAD_PRELOAD_ENABLE 0x00000080U
#define TIM_OCMODE_PWM1 0x00000060U
#define TIM_OCPOLARITY_HIGH 0x00000000U
#define TIM_OCNPOLARITY_HIGH 0x00000000U
#define TIM_OCFAST_DISABLE 0x00000000U
#define TIM_OCIDLESTATE_RESET 0x00000000U
#define TIM_OCNIDLESTATE_RESET 0x00000000U
#define TIM_CHANNEL_1 0x00000000U
#define TIM_CHANNEL_2 0x00000004U
#define TIM_CHANNEL_3 0x00000008U
#define TIM_CHANNEL_4 0x0000000CU
#define TIM_EVENTSOURCE_UPDATE 0x00000001U
#define TIM_FLAG_UPDATE 0x00000001U
#define TIM_IT_UPDATE 0x00000001U
HAL_StatusTypeDef HAL_TIM_PWM_Init(TIM_HandleTypeDef* h);
HAL_StatusTypeDef HAL_TIM_PWM_DeInit(TIM_HandleTypeDef* h);
HAL_StatusTypeDef HAL_TIM_PWM_ConfigChannel(TIM_HandleTypeDef* h, const TIM_OC_InitTypeDef* oc,
                                            uint32_t channel);
HAL_StatusTypeDef HAL_TIM_PWM_Start(TIM_HandleTypeDef* h, uint32_t channel);
HAL_StatusTypeDef HAL_TIM_PWM_Stop(TIM_HandleTypeDef* h, uint32_t channel);
HAL_StatusTypeDef HAL_TIM_Base_Init(TIM_HandleTypeDef* h);
HAL_StatusTypeDef HAL_TIM_Base_Start(TIM_HandleTypeDef* h);
HAL_StatusTypeDef HAL_TIM_GenerateEvent(TIM_HandleTypeDef* h, uint32_t source);
#define __HAL_TIM_SET_PRESCALER(h, v) ((h)->Instance->PSC = (v))
#define __HAL_TIM_SET_AUTORELOAD(h, v)                                                             \
  do {                                                                                             \
    (h)->Instance->ARR = (v);                                                                      \
    (h)->Init.Period = (v);                                                                        \
  } while (0)
#define __HAL_TIM_SET_COMPARE(h, ch, v) ((h)->Instance->CCR[(ch) >> 2U] = (v))
#define __HAL_TIM_SET_COUNTER(h, v) ((h)->Instance->CNT = (v))
#define __HAL_TIM_GET_FLAG(h, f) ((((h)->Instance->SR) & (f)) == (f))
#define __HAL_TIM_CLEAR_FLAG(h, f) ((h)->Instance->SR = ~(f) & (h)->Instance->SR)
#define __HAL_TIM_ENABLE_IT(h, it) ((h)->Instance->DIER |= (it))

/* --- ADC ----------------------------------------------------------------------------------------
 */
typedef struct {
  volatile uint32_t DR;
} ADC_TypeDef;
extern ADC_TypeDef kb_fake_adc[6];
#define ADC1 (&kb_fake_adc[1])
#define ADC2 (&kb_fake_adc[2])
#define ADC3 (&kb_fake_adc[3])
#if !defined(KB_FAKE_STM32F4)
#define ADC4 (&kb_fake_adc[4])
#define ADC5 (&kb_fake_adc[5])
#define ADC345_COMMON (&kb_fake_adc[0])
#define __HAL_RCC_ADC12_CLK_ENABLE() KB_FAKE_CLK("ADC12")
#define __HAL_RCC_ADC345_CLK_ENABLE() KB_FAKE_CLK("ADC345")
#define __LL_ADC_DECIMAL_NB_TO_CHANNEL(n) (((uint32_t)(n) << 26U) | 0x1000U)
#else
#define __HAL_RCC_ADC1_CLK_ENABLE() KB_FAKE_CLK("ADC1")
#define __HAL_RCC_ADC2_CLK_ENABLE() KB_FAKE_CLK("ADC2")
#define __HAL_RCC_ADC3_CLK_ENABLE() KB_FAKE_CLK("ADC3")
#endif
typedef struct {
  uint32_t ClockPrescaler;
  uint32_t Resolution;
  uint32_t DataAlign;
  uint32_t ScanConvMode;
  uint32_t EOCSelection;
  FunctionalState LowPowerAutoWait;
  FunctionalState ContinuousConvMode;
  uint32_t NbrOfConversion;
  FunctionalState DiscontinuousConvMode;
  uint32_t ExternalTrigConv;
  uint32_t ExternalTrigConvEdge;
  FunctionalState DMAContinuousRequests;
  uint32_t Overrun;
  FunctionalState OversamplingMode;
} ADC_InitTypeDef;
typedef struct {
  ADC_TypeDef* Instance;
  ADC_InitTypeDef Init;
  volatile uint32_t ErrorCode;
} ADC_HandleTypeDef;
typedef struct {
  uint32_t Channel;
  uint32_t Rank;
  uint32_t SamplingTime;
  uint32_t SingleDiff;
  uint32_t OffsetNumber;
} ADC_ChannelConfTypeDef;
#define ADC_CLOCK_SYNC_PCLK_DIV4 0x00030000U
#define ADC_RESOLUTION_12B 0x00000000U
#define ADC_DATAALIGN_RIGHT 0x00000000U
#define ADC_SCAN_DISABLE 0x00000000U
#define ADC_EOC_SINGLE_CONV 0x00000004U
#define ADC_SOFTWARE_START 0x00000800U
#define ADC_EXTERNALTRIGCONVEDGE_NONE 0x00000000U
#define ADC_OVR_DATA_OVERWRITTEN 0x00001000U
#define ADC_REGULAR_RANK_1 0x00000006U
#define ADC_SINGLE_ENDED 0x7FU
#define ADC_OFFSET_NONE 0x05U
#define ADC_SAMPLETIME_6CYCLES_5 0x1U
#define ADC_SAMPLETIME_47CYCLES_5 0x4U
#define ADC_SAMPLETIME_640CYCLES_5 0x7U
#define ADC_SAMPLETIME_15CYCLES 0x1U
#define ADC_SAMPLETIME_84CYCLES 0x4U
#define ADC_SAMPLETIME_480CYCLES 0x7U
HAL_StatusTypeDef HAL_ADC_Init(ADC_HandleTypeDef* h);
HAL_StatusTypeDef HAL_ADC_DeInit(ADC_HandleTypeDef* h);
HAL_StatusTypeDef HAL_ADC_ConfigChannel(ADC_HandleTypeDef* h, const ADC_ChannelConfTypeDef* c);
HAL_StatusTypeDef HAL_ADC_Start(ADC_HandleTypeDef* h);
HAL_StatusTypeDef HAL_ADC_Start_IT(ADC_HandleTypeDef* h);
HAL_StatusTypeDef HAL_ADC_Stop_IT(ADC_HandleTypeDef* h);
void HAL_ADC_IRQHandler(ADC_HandleTypeDef* h);
uint32_t HAL_ADC_GetValue(const ADC_HandleTypeDef* h);
void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef* hadc);
void HAL_ADC_ErrorCallback(ADC_HandleTypeDef* hadc);
HAL_StatusTypeDef HAL_ADCEx_Calibration_Start(ADC_HandleTypeDef* h, uint32_t single_diff);

/* --- I2C ----------------------------------------------------------------------------------------
 */
typedef struct {
  volatile uint32_t CR1;
  volatile uint32_t CR2;
  volatile uint32_t ISR;
  volatile uint32_t ICR;
  volatile uint32_t TXDR;
  volatile uint32_t RXDR;
} I2C_TypeDef;
extern I2C_TypeDef kb_fake_i2c[5];
#define I2C1 (&kb_fake_i2c[1])
#define I2C2 (&kb_fake_i2c[2])
#define I2C3 (&kb_fake_i2c[3])
#if !defined(KB_FAKE_STM32F4)
#define I2C4 (&kb_fake_i2c[4])
#define __HAL_RCC_I2C4_CLK_ENABLE() KB_FAKE_CLK("I2C4")
#endif
#define __HAL_RCC_I2C1_CLK_ENABLE() KB_FAKE_CLK("I2C1")
#define __HAL_RCC_I2C2_CLK_ENABLE() KB_FAKE_CLK("I2C2")
#define __HAL_RCC_I2C3_CLK_ENABLE() KB_FAKE_CLK("I2C3")
typedef struct {
  uint32_t Timing;
  uint32_t ClockSpeed;
  uint32_t DutyCycle;
  uint32_t OwnAddress1;
  uint32_t AddressingMode;
  uint32_t DualAddressMode;
  uint32_t OwnAddress2;
  uint32_t OwnAddress2Masks;
  uint32_t GeneralCallMode;
  uint32_t NoStretchMode;
} I2C_InitTypeDef;
typedef enum { HAL_I2C_STATE_RESET = 0x00U, HAL_I2C_STATE_READY = 0x20U } HAL_I2C_StateTypeDef;
typedef enum { HAL_I2C_MODE_NONE = 0x00U, HAL_I2C_MODE_MASTER = 0x10U } HAL_I2C_ModeTypeDef;
typedef struct {
  I2C_TypeDef* Instance;
  I2C_InitTypeDef Init;
  HAL_LockTypeDef Lock;
  volatile HAL_I2C_StateTypeDef State;
  volatile HAL_I2C_ModeTypeDef Mode;
  volatile uint32_t ErrorCode;
  volatile uint32_t PreviousState;
} I2C_HandleTypeDef;
#define I2C_FLAG_BUSY (1UL << 15U)
#define I2C_FLAG_AF (1UL << 4U)
#define I2C_FLAG_BERR (1UL << 8U)
#define I2C_FLAG_ARLO (1UL << 9U)
#define I2C_FLAG_OVR (1UL << 10U)
#define __HAL_I2C_GET_FLAG(h, f) ((((h)->Instance->ISR) & (f)) == (f) ? SET : RESET)
#define __HAL_I2C_CLEAR_FLAG(h, f) ((h)->Instance->ISR &= ~(uint32_t)(f))
#define __HAL_I2C_DISABLE_IT(h, it) ((h)->Instance->CR1 &= ~(uint32_t)(it))
#if defined(KB_FAKE_STM32F4)
#define I2C_IT_EVT (1UL << 9U)
#define I2C_IT_BUF (1UL << 10U)
#define I2C_IT_ERR (1UL << 8U)
#define I2C_FIRST_FRAME 0x00000001U
#define I2C_FIRST_AND_LAST_FRAME 0x00000008U
#define I2C_LAST_FRAME 0x00000020U
#else
#define I2C_IT_ERRI (1UL << 7U)
#define I2C_IT_TCI (1UL << 6U)
#define I2C_IT_STOPI (1UL << 5U)
#define I2C_IT_NACKI (1UL << 4U)
#define I2C_IT_ADDRI (1UL << 3U)
#define I2C_IT_RXI (1UL << 2U)
#define I2C_IT_TXI (1UL << 1U)
#define I2C_FLAG_STOPF (1UL << 5U)
#define I2C_ISR_TXE (1UL << 0U)
#define I2C_ISR_TXIS (1UL << 1U)
#define I2C_ISR_RXNE (1UL << 2U)
#define I2C_CR2_NBYTES (0xFFUL << 16U)
#define I2C_CR2_RELOAD (1UL << 24U)
#define I2C_CR2_AUTOEND (1UL << 25U)
#define I2C_CR2_START (1UL << 13U)
#define I2C_CR2_STOP (1UL << 14U)
#define I2C_CR2_RD_WRN (1UL << 10U)
#define I2C_FIRST_FRAME 0x00000000U
#define I2C_FIRST_AND_LAST_FRAME 0x02000000U
#define I2C_LAST_FRAME 0x02000000U
#endif
#define I2C_DUTYCYCLE_2 0x00000000U
#define I2C_OA2_NOMASK 0x00U
#define I2C_ADDRESSINGMODE_7BIT 0x00000001U
#define I2C_DUALADDRESS_DISABLE 0x00000000U
#define I2C_GENERALCALL_DISABLE 0x00000000U
#define I2C_NOSTRETCH_DISABLE 0x00000000U
#define I2C_ANALOGFILTER_ENABLE 0x00000000U
#define I2C_MEMADD_SIZE_8BIT 0x00000001U
#define I2C_MEMADD_SIZE_16BIT 0x00000002U
#define HAL_I2C_ERROR_BERR 0x00000001U
#define HAL_I2C_ERROR_ARLO 0x00000002U
#define HAL_I2C_ERROR_AF 0x00000004U
#define HAL_I2C_ERROR_TIMEOUT 0x00000020U
HAL_StatusTypeDef HAL_I2C_Init(I2C_HandleTypeDef* h);
HAL_StatusTypeDef HAL_I2C_DeInit(I2C_HandleTypeDef* h);
HAL_StatusTypeDef HAL_I2C_Master_Transmit_IT(I2C_HandleTypeDef* h, uint16_t address, uint8_t* data,
                                             uint16_t size);
HAL_StatusTypeDef HAL_I2C_Master_Seq_Transmit_IT(I2C_HandleTypeDef* h, uint16_t address,
                                                 uint8_t* data, uint16_t size, uint32_t options);
HAL_StatusTypeDef HAL_I2C_Master_Seq_Receive_IT(I2C_HandleTypeDef* h, uint16_t address,
                                                uint8_t* data, uint16_t size, uint32_t options);
void HAL_I2C_EV_IRQHandler(I2C_HandleTypeDef* h);
void HAL_I2C_ER_IRQHandler(I2C_HandleTypeDef* h);
void HAL_I2C_MasterTxCpltCallback(I2C_HandleTypeDef* hi2c);
void HAL_I2C_MasterRxCpltCallback(I2C_HandleTypeDef* hi2c);
void HAL_I2C_ErrorCallback(I2C_HandleTypeDef* hi2c);
void HAL_I2C_AbortCpltCallback(I2C_HandleTypeDef* hi2c);
HAL_StatusTypeDef HAL_I2CEx_ConfigAnalogFilter(I2C_HandleTypeDef* h, uint32_t filter);

/* --- SPI ----------------------------------------------------------------------------------------
 */
typedef struct {
  volatile uint32_t CR1;
} SPI_TypeDef;
extern SPI_TypeDef kb_fake_spi[7];
#define SPI1 (&kb_fake_spi[1])
#define SPI2 (&kb_fake_spi[2])
#define SPI3 (&kb_fake_spi[3])
#define SPI4 (&kb_fake_spi[4])
#define __HAL_RCC_SPI1_CLK_ENABLE() KB_FAKE_CLK("SPI1")
#define __HAL_RCC_SPI2_CLK_ENABLE() KB_FAKE_CLK("SPI2")
#define __HAL_RCC_SPI3_CLK_ENABLE() KB_FAKE_CLK("SPI3")
#define __HAL_RCC_SPI4_CLK_ENABLE() KB_FAKE_CLK("SPI4")
typedef struct {
  uint32_t Mode;
  uint32_t Direction;
  uint32_t DataSize;
  uint32_t CLKPolarity;
  uint32_t CLKPhase;
  uint32_t NSS;
  uint32_t BaudRatePrescaler;
  uint32_t FirstBit;
  uint32_t TIMode;
  uint32_t CRCCalculation;
  uint32_t CRCPolynomial;
  uint32_t CRCLength;
  uint32_t NSSPMode;
} SPI_InitTypeDef;
typedef struct {
  SPI_TypeDef* Instance;
  SPI_InitTypeDef Init;
  volatile uint32_t ErrorCode;
} SPI_HandleTypeDef;
#define SPI_MODE_MASTER 0x00000104U
#define SPI_DIRECTION_2LINES 0x00000000U
#define SPI_DATASIZE_8BIT 0x00000700U
#define SPI_POLARITY_LOW 0x00000000U
#define SPI_POLARITY_HIGH 0x00000002U
#define SPI_PHASE_1EDGE 0x00000000U
#define SPI_PHASE_2EDGE 0x00000001U
#define SPI_NSS_SOFT 0x00000200U
#define SPI_FIRSTBIT_MSB 0x00000000U
#define SPI_TIMODE_DISABLE 0x00000000U
#define SPI_CRCCALCULATION_DISABLE 0x00000000U
#define SPI_CRC_LENGTH_DATASIZE 0x00000000U
#define SPI_NSS_PULSE_DISABLE 0x00000000U
#define SPI_BAUDRATEPRESCALER_2 0x00000000U
#define SPI_BAUDRATEPRESCALER_4 0x00000008U
#define SPI_BAUDRATEPRESCALER_8 0x00000010U
#define SPI_BAUDRATEPRESCALER_16 0x00000018U
#define SPI_BAUDRATEPRESCALER_32 0x00000020U
#define SPI_BAUDRATEPRESCALER_64 0x00000028U
#define SPI_BAUDRATEPRESCALER_128 0x00000030U
#define SPI_BAUDRATEPRESCALER_256 0x00000038U
#define HAL_SPI_ERROR_MODF 0x00000001U
#define HAL_SPI_ERROR_OVR 0x00000004U
#define HAL_SPI_ERROR_ABORT 0x00000040U
HAL_StatusTypeDef HAL_SPI_Init(SPI_HandleTypeDef* h);
HAL_StatusTypeDef HAL_SPI_DeInit(SPI_HandleTypeDef* h);
HAL_StatusTypeDef HAL_SPI_Transmit_IT(SPI_HandleTypeDef* h, const uint8_t* data, uint16_t size);
HAL_StatusTypeDef HAL_SPI_TransmitReceive_IT(SPI_HandleTypeDef* h, const uint8_t* tx, uint8_t* rx,
                                             uint16_t size);
void HAL_SPI_IRQHandler(SPI_HandleTypeDef* h);
void HAL_SPI_TxCpltCallback(SPI_HandleTypeDef* hspi);
void HAL_SPI_TxRxCpltCallback(SPI_HandleTypeDef* hspi);
void HAL_SPI_ErrorCallback(SPI_HandleTypeDef* hspi);
HAL_StatusTypeDef HAL_SPI_Abort(SPI_HandleTypeDef* h);

/* --- UART ---------------------------------------------------------------------------------------
 */
#ifdef __cplusplus
}
/* A transmit data register: each write is a byte sent (fake::UartSent()). */
struct KbFakeTdr {
  KbFakeTdr& operator=(uint32_t v);
};
/* F4's DR: a write is a byte sent; a read returns `rx` (what the test put there). */
struct KbFakeDr {
  uint32_t rx;
  KbFakeDr& operator=(uint32_t v);
  operator uint32_t() const {
    return rx;
  }
};
extern "C" {
#endif
typedef struct {
  volatile uint32_t ISR;
  volatile uint32_t ICR;
  volatile uint32_t RDR;
  struct KbFakeTdr TDR;
  volatile uint32_t SR;
  struct KbFakeDr DR;
} USART_TypeDef;
extern USART_TypeDef kb_fake_usart[12];
#define USART1 (&kb_fake_usart[1])
#define USART2 (&kb_fake_usart[2])
#define USART3 (&kb_fake_usart[3])
#define UART4 (&kb_fake_usart[4])
#define UART5 (&kb_fake_usart[5])
#define __HAL_RCC_USART1_CLK_ENABLE() KB_FAKE_CLK("USART1")
#define __HAL_RCC_USART2_CLK_ENABLE() KB_FAKE_CLK("USART2")
#define __HAL_RCC_USART3_CLK_ENABLE() KB_FAKE_CLK("USART3")
#define __HAL_RCC_UART4_CLK_ENABLE() KB_FAKE_CLK("UART4")
#define __HAL_RCC_UART5_CLK_ENABLE() KB_FAKE_CLK("UART5")
#if defined(KB_FAKE_STM32F4)
#define USART6 (&kb_fake_usart[6])
#define __HAL_RCC_USART6_CLK_ENABLE() KB_FAKE_CLK("USART6")
#define USART_SR_PE (1UL << 0U)
#define USART_SR_FE (1UL << 1U)
#define USART_SR_NE (1UL << 2U)
#define USART_SR_ORE (1UL << 3U)
#define USART_SR_RXNE (1UL << 5U)
#define USART_SR_TC (1UL << 6U)
#define USART_SR_TXE (1UL << 7U)
#define UART_FLAG_TC USART_SR_TC
#define UART_FLAG_TXE USART_SR_TXE
#define UART_FLAG_RXNE USART_SR_RXNE
#define __HAL_UART_GET_FLAG(h, f) ((((h)->Instance->SR) & (f)) == (f))
#else
#define LPUART1 (&kb_fake_usart[11])
#define __HAL_RCC_LPUART1_CLK_ENABLE() KB_FAKE_CLK("LPUART1")
#define USART_ISR_PE (1UL << 0U)
#define USART_ISR_FE (1UL << 1U)
#define USART_ISR_NE (1UL << 2U)
#define USART_ISR_ORE (1UL << 3U)
#define USART_ISR_RXNE_RXFNE (1UL << 5U)
#define USART_ISR_TC (1UL << 6U)
#define USART_ISR_TXE_TXFNF (1UL << 7U)
#define USART_ICR_PECF (1UL << 0U)
#define USART_ICR_FECF (1UL << 1U)
#define USART_ICR_NECF (1UL << 2U)
#define USART_ICR_ORECF (1UL << 3U)
#define USART_PRESC_PRESCALER 0xFU
#define UART_FLAG_TC USART_ISR_TC
#define UART_FLAG_TXE USART_ISR_TXE_TXFNF
#define UART_FLAG_RXNE USART_ISR_RXNE_RXFNE
#define UART_WORDLENGTH_7B 0x10000000U
#define UART_ONE_BIT_SAMPLE_DISABLE 0x00000000U
#define UART_PRESCALER_DIV1 0x00000000U
#define UART_ADVFEATURE_NO_INIT 0x00000000U
#define __HAL_UART_GET_FLAG(h, f) ((((h)->Instance->ISR) & (f)) == (f))
#endif
typedef struct {
  uint32_t BaudRate;
  uint32_t WordLength;
  uint32_t StopBits;
  uint32_t Parity;
  uint32_t Mode;
  uint32_t HwFlowCtl;
  uint32_t OverSampling;
  uint32_t OneBitSampling;
  uint32_t ClockPrescaler;
} UART_InitTypeDef;
typedef struct {
  uint32_t AdvFeatureInit;
} UART_AdvFeatureInitTypeDef;
typedef struct {
  USART_TypeDef* Instance;
  UART_InitTypeDef Init;
  UART_AdvFeatureInitTypeDef AdvancedInit;
  volatile uint32_t ErrorCode;
  uint32_t Interrupts;
} UART_HandleTypeDef;
#define UART_WORDLENGTH_8B 0x00000000U
#define UART_WORDLENGTH_9B 0x00001000U
#define UART_STOPBITS_1 0x00000000U
#define UART_STOPBITS_2 0x00002000U
#define UART_PARITY_NONE 0x00000000U
#define UART_PARITY_EVEN 0x00000400U
#define UART_PARITY_ODD 0x00000600U
#define UART_MODE_TX_RX 0x0000000CU
#define UART_HWCONTROL_NONE 0x00000000U
#define UART_OVERSAMPLING_16 0x00000000U
#define UART_IT_RXNE (1UL << 0U)
#define UART_IT_PE (1UL << 1U)
#define UART_IT_ERR (1UL << 2U)
#define UART_IT_TXE (1UL << 3U)
#define UART_IT_TC (1UL << 4U)
/* Enabling a transmit interrupt runs the port's handler while its flag is set (as the hardware
 * would), unless fake::Stall("UART") holds it. */
void kb_fake_uart_it(UART_HandleTypeDef* h, uint32_t it, int on);
#define __HAL_UART_ENABLE_IT(h, it) kb_fake_uart_it((h), (it), 1)
#define __HAL_UART_DISABLE_IT(h, it) kb_fake_uart_it((h), (it), 0)
#define __HAL_UART_GET_IT_SOURCE(h, it) (((h)->Interrupts & (it)) != 0U ? 1U : 0U)
HAL_StatusTypeDef HAL_UART_Init(UART_HandleTypeDef* h);
HAL_StatusTypeDef HAL_UART_DeInit(UART_HandleTypeDef* h);

/* --- CAN ----------------------------------------------------------------------------------------
 */
#if !defined(KB_FAKE_STM32F4)
typedef struct {
  volatile uint32_t CCCR;
} FDCAN_GlobalTypeDef;
extern FDCAN_GlobalTypeDef kb_fake_fdcan[4];
#define FDCAN1 (&kb_fake_fdcan[1])
#define FDCAN2 (&kb_fake_fdcan[2])
#define __HAL_RCC_FDCAN_CLK_ENABLE() KB_FAKE_CLK("FDCAN")
typedef struct {
  uint32_t ClockDivider;
  uint32_t FrameFormat;
  uint32_t Mode;
  FunctionalState AutoRetransmission;
  FunctionalState TransmitPause;
  FunctionalState ProtocolException;
  uint32_t NominalPrescaler;
  uint32_t NominalSyncJumpWidth;
  uint32_t NominalTimeSeg1;
  uint32_t NominalTimeSeg2;
  uint32_t DataPrescaler;
  uint32_t DataSyncJumpWidth;
  uint32_t DataTimeSeg1;
  uint32_t DataTimeSeg2;
  uint32_t StdFiltersNbr;
  uint32_t ExtFiltersNbr;
  uint32_t TxFifoQueueMode;
} FDCAN_InitTypeDef;
typedef struct {
  FDCAN_GlobalTypeDef* Instance;
  FDCAN_InitTypeDef Init;
  volatile uint32_t ErrorCode;
} FDCAN_HandleTypeDef;
typedef struct {
  uint32_t Identifier;
  uint32_t IdType;
  uint32_t TxFrameType;
  uint32_t DataLength;
  uint32_t ErrorStateIndicator;
  uint32_t BitRateSwitch;
  uint32_t FDFormat;
  uint32_t TxEventFifoControl;
  uint32_t MessageMarker;
} FDCAN_TxHeaderTypeDef;
typedef struct {
  uint32_t Identifier;
  uint32_t IdType;
  uint32_t RxFrameType;
  uint32_t DataLength;
} FDCAN_RxHeaderTypeDef;
typedef struct {
  uint32_t LastErrorCode;
  uint32_t ErrorPassive;
  uint32_t BusOff;
} FDCAN_ProtocolStatusTypeDef;
typedef struct {
  uint32_t TxErrorCnt;
  uint32_t RxErrorCnt;
} FDCAN_ErrorCountersTypeDef;
#define FDCAN_FRAME_CLASSIC 0x00000000U
#define FDCAN_MODE_NORMAL 0x00000000U
#define FDCAN_MODE_INTERNAL_LOOPBACK 0x00000003U
#define FDCAN_MODE_BUS_MONITORING 0x00000004U
#define FDCAN_TX_FIFO_OPERATION 0x00000000U
#define FDCAN_CLOCK_DIV1 0x00000000U
#define FDCAN_ACCEPT_IN_RX_FIFO0 0x00000000U
#define FDCAN_FILTER_REMOTE 0x00000000U
#define FDCAN_STANDARD_ID 0x00000000U
#define FDCAN_EXTENDED_ID 0x40000000U
#define FDCAN_DATA_FRAME 0x00000000U
#define FDCAN_REMOTE_FRAME 0x20000000U
#define FDCAN_ESI_ACTIVE 0x00000000U
#define FDCAN_BRS_OFF 0x00000000U
#define FDCAN_CLASSIC_CAN 0x00000000U
#define FDCAN_NO_TX_EVENTS 0x00000000U
#define FDCAN_RX_FIFO0 0x00000040U
#define FDCAN_IT_RX_FIFO0_NEW_MESSAGE (1UL << 0U)
#define FDCAN_IT_RX_FIFO0_MESSAGE_LOST (1UL << 3U)
#define FDCAN_IT_TX_COMPLETE (1UL << 9U)
#define FDCAN_TX_BUFFER0 0x00000001U
#define FDCAN_TX_BUFFER1 0x00000002U
#define FDCAN_TX_BUFFER2 0x00000004U
HAL_StatusTypeDef HAL_FDCAN_ActivateNotification(FDCAN_HandleTypeDef* h, uint32_t its,
                                                 uint32_t buffers);
void HAL_FDCAN_IRQHandler(FDCAN_HandleTypeDef* h);
void HAL_FDCAN_RxFifo0Callback(FDCAN_HandleTypeDef* hfdcan, uint32_t RxFifo0ITs);
void HAL_FDCAN_TxBufferCompleteCallback(FDCAN_HandleTypeDef* hfdcan, uint32_t BufferIndexes);
HAL_StatusTypeDef HAL_FDCAN_Init(FDCAN_HandleTypeDef* h);
HAL_StatusTypeDef HAL_FDCAN_DeInit(FDCAN_HandleTypeDef* h);
HAL_StatusTypeDef HAL_FDCAN_Start(FDCAN_HandleTypeDef* h);
HAL_StatusTypeDef HAL_FDCAN_Stop(FDCAN_HandleTypeDef* h);
HAL_StatusTypeDef HAL_FDCAN_ConfigGlobalFilter(FDCAN_HandleTypeDef* h, uint32_t std, uint32_t ext,
                                               uint32_t rstd, uint32_t rext);
uint32_t HAL_FDCAN_GetTxFifoFreeLevel(const FDCAN_HandleTypeDef* h);
uint32_t HAL_FDCAN_GetRxFifoFillLevel(const FDCAN_HandleTypeDef* h, uint32_t fifo);
HAL_StatusTypeDef HAL_FDCAN_AddMessageToTxFifoQ(FDCAN_HandleTypeDef* h,
                                                const FDCAN_TxHeaderTypeDef* header,
                                                const uint8_t* data);
HAL_StatusTypeDef HAL_FDCAN_GetRxMessage(FDCAN_HandleTypeDef* h, uint32_t fifo,
                                         FDCAN_RxHeaderTypeDef* header, uint8_t* data);
HAL_StatusTypeDef HAL_FDCAN_GetProtocolStatus(const FDCAN_HandleTypeDef* h,
                                              FDCAN_ProtocolStatusTypeDef* ps);
HAL_StatusTypeDef HAL_FDCAN_GetErrorCounters(const FDCAN_HandleTypeDef* h,
                                             FDCAN_ErrorCountersTypeDef* ec);
#else
#ifdef __cplusplus
}
/* ESR is computed on every read from the fake's bus state (bus-off and its automatic recovery). */
struct KbFakeEsr {
  operator uint32_t() const;
};
extern "C" {
#endif
typedef struct {
  struct KbFakeEsr ESR;
} CAN_TypeDef;
extern CAN_TypeDef kb_fake_can[4];
#define CAN1 (&kb_fake_can[1])
#define CAN2 (&kb_fake_can[2])
#define __HAL_RCC_CAN1_CLK_ENABLE() KB_FAKE_CLK("CAN1")
#define __HAL_RCC_CAN2_CLK_ENABLE() KB_FAKE_CLK("CAN2")
typedef struct {
  uint32_t Prescaler;
  uint32_t Mode;
  uint32_t SyncJumpWidth;
  uint32_t TimeSeg1;
  uint32_t TimeSeg2;
  FunctionalState TimeTriggeredMode;
  FunctionalState AutoBusOff;
  FunctionalState AutoWakeUp;
  FunctionalState AutoRetransmission;
  FunctionalState ReceiveFifoLocked;
  FunctionalState TransmitFifoPriority;
} CAN_InitTypeDef;
typedef struct {
  CAN_TypeDef* Instance;
  CAN_InitTypeDef Init;
  volatile uint32_t ErrorCode;
} CAN_HandleTypeDef;
typedef struct {
  uint32_t FilterIdHigh;
  uint32_t FilterIdLow;
  uint32_t FilterMaskIdHigh;
  uint32_t FilterMaskIdLow;
  uint32_t FilterFIFOAssignment;
  uint32_t FilterBank;
  uint32_t FilterMode;
  uint32_t FilterScale;
  uint32_t FilterActivation;
  uint32_t SlaveStartFilterBank;
} CAN_FilterTypeDef;
typedef struct {
  uint32_t StdId;
  uint32_t ExtId;
  uint32_t IDE;
  uint32_t RTR;
  uint32_t DLC;
  FunctionalState TransmitGlobalTime;
} CAN_TxHeaderTypeDef;
typedef struct {
  uint32_t StdId;
  uint32_t ExtId;
  uint32_t IDE;
  uint32_t RTR;
  uint32_t DLC;
} CAN_RxHeaderTypeDef;
#define CAN_MODE_NORMAL 0x00000000U
#define CAN_MODE_LOOPBACK 0x40000000U
#define CAN_MODE_SILENT 0x80000000U
#define CAN_FILTERMODE_IDMASK 0x00000000U
#define CAN_FILTERSCALE_32BIT 0x00000001U
#define CAN_RX_FIFO0 0x00000000U
#define CAN_ID_STD 0x00000000U
#define CAN_ID_EXT 0x00000004U
#define CAN_RTR_DATA 0x00000000U
#define CAN_RTR_REMOTE 0x00000002U
#define CAN_BTR_SJW_Pos 24U
#define CAN_BTR_TS1_Pos 16U
#define CAN_BTR_TS2_Pos 20U
#define CAN_ESR_BOFF (1UL << 2U)
#define CAN_ESR_LEC_Pos 4U
#define CAN_ESR_LEC_Msk (7UL << 4U)
#define CAN_ESR_TEC_Pos 16U
#define CAN_ESR_TEC_Msk (0xFFUL << 16U)
#define CAN_ESR_REC_Pos 24U
#define CAN_ESR_REC_Msk (0xFFUL << 24U)
#define CAN_IT_TX_MAILBOX_EMPTY (1UL << 0U)
#define CAN_IT_RX_FIFO0_MSG_PENDING (1UL << 1U)
#define CAN_IT_RX_FIFO0_OVERRUN (1UL << 3U)
#define HAL_CAN_ERROR_RX_FOV0 0x00000200U
HAL_StatusTypeDef HAL_CAN_ActivateNotification(CAN_HandleTypeDef* h, uint32_t its);
HAL_StatusTypeDef HAL_CAN_ResetError(CAN_HandleTypeDef* h);
void HAL_CAN_IRQHandler(CAN_HandleTypeDef* h);
void HAL_CAN_RxFifo0MsgPendingCallback(CAN_HandleTypeDef* hcan);
void HAL_CAN_ErrorCallback(CAN_HandleTypeDef* hcan);
void HAL_CAN_TxMailbox0CompleteCallback(CAN_HandleTypeDef* hcan);
void HAL_CAN_TxMailbox1CompleteCallback(CAN_HandleTypeDef* hcan);
void HAL_CAN_TxMailbox2CompleteCallback(CAN_HandleTypeDef* hcan);
void HAL_CAN_TxMailbox0AbortCallback(CAN_HandleTypeDef* hcan);
void HAL_CAN_TxMailbox1AbortCallback(CAN_HandleTypeDef* hcan);
void HAL_CAN_TxMailbox2AbortCallback(CAN_HandleTypeDef* hcan);
HAL_StatusTypeDef HAL_CAN_Init(CAN_HandleTypeDef* h);
HAL_StatusTypeDef HAL_CAN_DeInit(CAN_HandleTypeDef* h);
HAL_StatusTypeDef HAL_CAN_ConfigFilter(CAN_HandleTypeDef* h, const CAN_FilterTypeDef* f);
HAL_StatusTypeDef HAL_CAN_Start(CAN_HandleTypeDef* h);
HAL_StatusTypeDef HAL_CAN_Stop(CAN_HandleTypeDef* h);
uint32_t HAL_CAN_GetTxMailboxesFreeLevel(const CAN_HandleTypeDef* h);
uint32_t HAL_CAN_GetRxFifoFillLevel(const CAN_HandleTypeDef* h, uint32_t fifo);
HAL_StatusTypeDef HAL_CAN_AddTxMessage(CAN_HandleTypeDef* h, const CAN_TxHeaderTypeDef* header,
                                       const uint8_t* data, uint32_t* mailbox);
HAL_StatusTypeDef HAL_CAN_GetRxMessage(CAN_HandleTypeDef* h, uint32_t fifo,
                                       CAN_RxHeaderTypeDef* header, uint8_t* data);
#endif

#ifdef __cplusplus
}
#endif

#endif /* KB_FAKE_STM32_HAL_H */

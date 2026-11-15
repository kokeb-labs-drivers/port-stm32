/* FreeRTOS configuration of the STM32 example under EXAMPLE_RTOS=FREERTOS, for every family (4 NVIC priority bits on
 * G4, H7, F4 and L4). Static allocation only, a 1 kHz tick, the RTOS syscall limit at NVIC priority 5: the port's
 * interrupts that give a kb::rtos semaphore (UART, I2C, SPI, ADC, CAN, EXTI at their default priority 5) are at or
 * below it; the clock's wrap interrupt (priority 1) never calls the RTOS. The kernel's handlers take the CMSIS
 * vector names. Example code. SPDX-License-Identifier: MIT */
#ifndef KB_EXAMPLE_FREERTOS_CONFIG_H
#define KB_EXAMPLE_FREERTOS_CONFIG_H

#include <stdint.h>

extern uint32_t SystemCoreClock;

#define configUSE_PREEMPTION 1
#define configCPU_CLOCK_HZ (SystemCoreClock)
#define configTICK_RATE_HZ 1000
#define configMAX_PRIORITIES 8
#define configMINIMAL_STACK_SIZE 256
#define configMAX_TASK_NAME_LEN 16
#define configTICK_TYPE_WIDTH_IN_BITS TICK_TYPE_WIDTH_32_BITS
#define configIDLE_SHOULD_YIELD 1
#define configUSE_MUTEXES 1
#define configUSE_RECURSIVE_MUTEXES 0
#define configUSE_COUNTING_SEMAPHORES 1
#define configQUEUE_REGISTRY_SIZE 0
#define configUSE_TIMERS 1
#define configTIMER_TASK_PRIORITY 6
#define configTIMER_QUEUE_LENGTH 8
#define configTIMER_TASK_STACK_DEPTH 256
#define configSUPPORT_STATIC_ALLOCATION 1
#define configSUPPORT_DYNAMIC_ALLOCATION 0
#define configUSE_IDLE_HOOK 0
#define configUSE_TICK_HOOK 1
#define configCHECK_FOR_STACK_OVERFLOW 0
#define configUSE_MALLOC_FAILED_HOOK 0

#define configPRIO_BITS 4
#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY 15
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY 5
#define configKERNEL_INTERRUPT_PRIORITY (configLIBRARY_LOWEST_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))
#define configMAX_SYSCALL_INTERRUPT_PRIORITY (configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))

#define configASSERT(x)                                                                                              \
  do {                                                                                                               \
    if ((x) == 0) {                                                                                                  \
      for (;;) {                                                                                                     \
      }                                                                                                              \
    }                                                                                                                \
  } while (0)

#define INCLUDE_vTaskDelay 1
#define INCLUDE_vTaskDelete 1
#define INCLUDE_xTaskDelayUntil 1
#define INCLUDE_xTaskGetSchedulerState 1
#define INCLUDE_xTimerPendFunctionCall 1

/* The kernel's exception handlers under the names of the CMSIS startup file's vector table. */
#define vPortSVCHandler SVC_Handler
#define xPortPendSVHandler PendSV_Handler
#define xPortSysTickHandler SysTick_Handler

#endif

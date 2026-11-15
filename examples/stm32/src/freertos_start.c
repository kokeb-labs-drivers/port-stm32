/* The FreeRTOS side of the example under EXAMPLE_RTOS=FREERTOS: the application task (static), the kernel's
 * static-allocation hooks, and the HAL time base advanced from the tick hook (the kernel owns SysTick). The port's
 * waits do not use the HAL tick; only the HAL's own initialisation waits do, and the port checks that it runs before
 * it calls them (docs/getting-started.md). Example code. SPDX-License-Identifier: MIT */
#include "FreeRTOS.h"
#include "task.h"

void HAL_IncTick(void);
void kb_example_main(void);
void kb_example_start(void);

static StaticTask_t app_tcb;
static StackType_t app_stack[2048];
static StaticTask_t idle_tcb;
static StackType_t idle_stack[configMINIMAL_STACK_SIZE];
static StaticTask_t timer_tcb;
static StackType_t timer_stack[configTIMER_TASK_STACK_DEPTH];

static void app_task(void* context) {
  (void)context;
  kb_example_main(); /* never returns */
  vTaskDelete(NULL);
}

void kb_example_start(void) {
  (void)xTaskCreateStatic(app_task, "app", sizeof(app_stack) / sizeof(app_stack[0]), NULL, 3U, app_stack,
                          &app_tcb);
  vTaskStartScheduler(); /* does not return */
}

void vApplicationTickHook(void) {
  HAL_IncTick();
}

void vApplicationGetIdleTaskMemory(StaticTask_t** tcb, StackType_t** stack, configSTACK_DEPTH_TYPE* size) {
  *tcb = &idle_tcb;
  *stack = idle_stack;
  *size = configMINIMAL_STACK_SIZE;
}

void vApplicationGetTimerTaskMemory(StaticTask_t** tcb, StackType_t** stack, configSTACK_DEPTH_TYPE* size) {
  *tcb = &timer_tcb;
  *stack = timer_stack;
  *size = configTIMER_TASK_STACK_DEPTH;
}

/* Boundary file (deviations/boundary-files.yml), targets only: the two 64-bit atomic helpers the
 * GNU toolchain calls for std::atomic<uint64_t> on a 32-bit Cortex-M, which has no 64-bit exclusive
 * load/store and whose arm-none-eabi toolchain ships no libatomic. kb::dma::Stream keeps its frame
 * time stamp in one (adc_stream.hpp). Single-core: interrupts are masked (PRIMASK saved and
 * restored) for the two word accesses, which makes them atomic against every interrupt. Weak, so a
 * toolchain libatomic or the application's own definition wins. SPDX-License-Identifier: MIT
 * Copyright (c) 2026 The port-stm32 authors */
/* The fixed-width types of the Arm EABI, spelled out: this file includes no header. */
typedef unsigned int uint32_t;
typedef unsigned long long uint64_t;

static inline uint32_t KbMask(void) {
  uint32_t primask;
  __asm volatile("mrs %0, primask\n cpsid i" : "=r"(primask)::"memory");
  return primask;
}

static inline void KbRestore(uint32_t primask) {
  __asm volatile("msr primask, %0" ::"r"(primask) : "memory");
}

__attribute__((weak)) uint64_t __atomic_load_8(const volatile void* ptr, int order) {
  (void)order;
  const uint32_t saved = KbMask();
  const uint64_t value = *(const volatile uint64_t*)ptr;
  KbRestore(saved);
  return value;
}

__attribute__((weak)) void __atomic_store_8(volatile void* ptr, uint64_t value, int order) {
  (void)order;
  const uint32_t saved = KbMask();
  *(volatile uint64_t*)ptr = value;
  KbRestore(saved);
}

/* Boundary file (deviations/boundary-files.yml), targets only: the two 64-bit atomic helpers the
 * GNU toolchain calls for std::atomic<uint64_t> on a 32-bit Cortex-M, which has no 64-bit exclusive
 * load/store and whose arm-none-eabi toolchain ships no libatomic. kb::dma::Stream keeps its frame
 * time stamp in one (adc_stream.hpp). Single-core: interrupts are masked (PRIMASK saved and
 * restored) for the two word accesses, which makes them atomic against every interrupt. Weak, so a
 * toolchain libatomic or the application's own definition wins. SPDX-License-Identifier: MIT
 * Copyright (c) 2026 The port-stm32 authors */
#if defined(__arm__) || defined(__thumb__)
/* The fixed-width types of the Arm EABI, spelled out: this file includes no header. */
typedef unsigned int KbU32;
typedef unsigned long long KbU64;

static inline KbU32 KbMask(void) {
  KbU32 primask = 0U;
  __asm volatile("mrs %0, primask\n cpsid i" : "=r"(primask)::"memory");
  return primask;
}

static inline void KbRestore(KbU32 primask) {
  __asm volatile("msr primask, %0" ::"r"(primask) : "memory");
}

/* The names and signatures are the GNU toolchain's libatomic ABI (reserved names by definition). */
// NOLINTBEGIN(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp,readability-identifier-naming,cppcoreguidelines-pro-type-cstyle-cast)
__attribute__((weak)) KbU64 __atomic_load_8(const volatile void* ptr, int order) {
  (void)order;
  const KbU32 saved = KbMask();
  const KbU64 value = *(const volatile KbU64*)ptr;
  KbRestore(saved);
  return value;
}

__attribute__((weak)) void __atomic_store_8(volatile void* ptr, KbU64 value, int order) {
  (void)order;
  const KbU32 saved = KbMask();
  *(volatile KbU64*)ptr = value;
  KbRestore(saved);
}
// NOLINTEND(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp,readability-identifier-naming,cppcoreguidelines-pro-type-cstyle-cast)
#else
/* A host compile (the MISRA scan and clang-tidy over the HAL fakes): the host's compiler has these
 * built in. */
typedef int KbAtomic64HostUnit;
#endif

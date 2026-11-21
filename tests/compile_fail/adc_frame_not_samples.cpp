// Compile-fail test: an ADC stream frame that is not an array of 16-bit samples.
// EXPECT: AdcFrame|constraints not satisfied|template constraint failure
// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The port-stm32 authors
#include "kb/port/stm32/adc_stream.hpp"

struct Floats {
  float v[2];
};
using Bad = kb::port::stm32::AdcDmaStream<Floats, kb::port::stm32::h7::D2Sram>;
Bad* g_bad = nullptr;

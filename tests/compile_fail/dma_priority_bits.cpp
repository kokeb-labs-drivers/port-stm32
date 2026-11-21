// Compile-fail test: a stream interrupt priority outside the four implemented priority bits.
// EXPECT: outside the implemented bits
// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The port-stm32 authors
#include "kb/port/stm32/dma.hpp"

struct Adc1 {
  static constexpr kb::dma::StreamAssignment kAssignment{
      1U, 2U, 9U, kb::dma::ChannelPriority::High, 16U, kb::dma::Notify::None};
};
constexpr bool kBad = (kb::port::stm32::RequireStream<Adc1>(), true);

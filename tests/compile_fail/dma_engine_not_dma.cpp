// Compile-fail test: a stream on an engine the port does not drive (BDMA, MDMA, none).
// EXPECT: engine is not DMA1 or DMA2
// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The port-stm32 authors
#include "kb/port/stm32/dma.hpp"

struct Lpuart {
  static constexpr kb::dma::StreamAssignment kAssignment{3U, 0U, 9U};
};
constexpr bool kBad = (kb::port::stm32::RequireStream<Lpuart>(), true);

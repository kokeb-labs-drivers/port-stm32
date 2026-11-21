// Compile-fail test: a DMA1 stream with a buffer in the H7 DTCM, which DMA1 cannot reach.
// EXPECT: cannot reach this memory region
// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The port-stm32 authors
#include "kb/port/stm32/dma.hpp"

struct Spi1Rx {
  static constexpr kb::dma::StreamAssignment kAssignment{1U, 0U, 37U};
};
constexpr bool kBad =
    (kb::port::stm32::RequireStreamReaches<Spi1Rx, kb::port::stm32::h7::Dtcm>(), true);

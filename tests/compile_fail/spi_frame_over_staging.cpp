// Compile-fail test: a driver whose largest frame is larger than the bus's DMA staging buffers.
// EXPECT: exceeds what this bus carries
// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The port-stm32 authors
#include "kb/port/stm32/spi_dma.hpp"

using Link = kb::port::stm32::SpiDmaLink<kb::port::stm32::h7::D2Sram, 64U>;
constexpr bool kBad =
    (kb::dma::RequireBusCarries<kb::port::stm32::SpiLimitsOf<Link>, 128U>(), true);

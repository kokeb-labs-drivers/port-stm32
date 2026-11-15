// Boundary file (deviations/boundary-files.yml): the one instance of the port's shared state
// (state.hpp), in the DEV-001 form of the HAL handle tables it goes with. It includes no HAL
// header; every rule over the state is in the platform-free files. SPDX-License-Identifier: MIT
// Copyright (c) 2026 The port-stm32 authors
#include "state.hpp"
#include "sdk.hpp"

namespace kb::port::stm32::sdk {

PortState& State() noexcept {
  // codeql::misra_deviation_next_line(DEV-001)
  static PortState state{};
  return state;
}

} // namespace kb::port::stm32::sdk

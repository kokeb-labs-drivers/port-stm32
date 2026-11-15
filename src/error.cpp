// LastErrorRecord: the status and vendor code of an object's last call, written and read with
// interrupts masked so concurrent calls from several tasks never tear it. Platform-free.
// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The port-stm32 authors
#include <cstdint>

#include "kb/port/stm32/error.hpp"
#include "kb/utils/result.hpp"
#include "kb/utils/status.hpp"
#include "state.hpp"

namespace kb::port::stm32 {

kb::Status LastErrorRecord::Set(kb::Status status, uint32_t code) noexcept {
  const IrqGuard guard{};
  status_ = status;
  detail_ = code;
  return status;
}

kb::Result<void> LastErrorRecord::Get() const noexcept {
  kb::Status status = kb::Status::Ok;
  uint32_t detail = 0U;
  {
    const IrqGuard guard{};
    status = status_;
    detail = detail_;
  }
  return (status == kb::Status::Ok) ? kb::Result<void>::Success()
                                    : kb::Result<void>::Failure(status, detail);
}

} // namespace kb::port::stm32

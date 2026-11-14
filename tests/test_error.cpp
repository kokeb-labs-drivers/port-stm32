// The HAL status mapping (error.hpp), the vendor code, the last-error record and the pin naming.
// Test code. SPDX-License-Identifier: MIT
// Copyright (c) 2026 The port-stm32 authors
#include "support.hpp"

namespace h = kb::port::stm32::hal;
namespace pc = kb::port::stm32::port_code;

static_assert(stm::MapHalStatus(h::kOk) == Status::Ok);
static_assert(stm::MapHalStatus(h::kError) == Status::Fault);
static_assert(stm::MapHalStatus(h::kBusy) == Status::Busy);
static_assert(stm::MapHalStatus(h::kTimeout) == Status::Timeout);
static_assert(stm::MapHalStatus(pc::kInvalidArgument) == Status::InvalidArgument);
static_assert(stm::MapHalStatus(pc::kNotReady) == Status::NotReady);
static_assert(stm::MapHalStatus(pc::kLockTimeout) == Status::Busy);
static_assert(stm::MapHalStatus(pc::kUnsupported) == Status::Unsupported);
static_assert(stm::MapHalStatus(pc::kOwned) == Status::Busy);
static_assert(stm::MapHalStatus(0x77U) == Status::Fault);
static_assert(stm::VendorCode(h::kError, 0x04U) == 0x401U);
static_assert(PinId{}.Used() == false);
static_assert(PinId{GpioPort::B, 15U, 15U}.Valid());
static_assert(!PinId{GpioPort::B, 16U, 0U}.Valid());
static_assert(!PinId{GpioPort::B, 1U, 16U}.Valid());

KB_TEST("error: the mapping at run time and the record") {
  const std::array<uint32_t, 9> codes{
      h::kOk,        h::kError,        h::kBusy,         h::kTimeout, pc::kInvalidArgument,
      pc::kNotReady, pc::kLockTimeout, pc::kUnsupported, pc::kOwned};
  const std::array<Status, 9> want{Status::Ok,      Status::Fault,           Status::Busy,
                                   Status::Timeout, Status::InvalidArgument, Status::NotReady,
                                   Status::Busy,    Status::Unsupported,     Status::Busy};
  for (std::size_t i = 0U; i < codes.size(); ++i) {
    KB_CHECK(stm::MapHalStatus(codes[i]) == want[i]);
  }
  KB_CHECK(stm::MapHalStatus(0x99U) == Status::Fault);
  stm::LastErrorRecord r{};
  KB_CHECK(r.Get().IsOk());
  KB_CHECK(r.Set(Status::Timeout, 3U) == Status::Timeout);
  KB_CHECK(FailedWith(r.Get(), Status::Timeout, 3U));
  KB_CHECK(r.Clear() == Status::Ok);
  KB_CHECK(r.Get().IsOk());
}

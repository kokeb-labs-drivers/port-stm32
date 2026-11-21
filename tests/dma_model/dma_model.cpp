// The host implementation of the port's DMA boundary (sdk:: DMA, SPI-by-DMA and ADC-stream calls)
// over kb-core's host DMA-and-cache model; see dma_model.hpp. Test code, outside the MISRA claim.
// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The port-stm32 authors
#include "dma_model.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "kb/dma/host/engine.hpp"
#include "kb/dma/host/machine.hpp"
#include "kb/dma/host/stream_rig.hpp"
#include "kb/port/stm32/dma.hpp"
#include "kb/port/stm32/error.hpp"
#include "kb/port/stm32/irq.hpp"
#include "sdk.hpp"

namespace h = kb::dma::host;
namespace stm = kb::port::stm32;

namespace {

constexpr std::size_t kStreams = 16U;

struct Slot {
  h::DmaEngine* engine{nullptr};
  h::HwRing* ring{nullptr};
  h::Machine* ring_machine{nullptr};
  uint8_t seed{0U};
  bool configured{false};
  bool circular{false};
  uint16_t request{0U};
  std::size_t item{1U};
  kb::dma::Direction dir{kb::dma::Direction::FromDevice};
  std::size_t length{0U}; // bytes of the armed transfer
  bool ring_on{false};    // the ring's EN
  bool stopping{false};   // EN cleared, the engine finishing its burst
  bool foreign{false};    // EN set by someone else
  uint32_t armed{0U};     // interrupt sources enabled at the arm
  uint32_t extra{0U};     // flags raised by a test with nothing in flight
  bool irq{false};
  uint8_t priority{0U};
  std::array<uint32_t, 5> clears{};
};

struct Model {
  std::array<Slot, kStreams> slots{};
  h::Machine* machine{nullptr};
  dma_model::BoundaryFaults faults{};
  int priority_readback{-1};
  bool cache_on{false};
  bool mpu{true};
  bool spi_runs{true};
  uint32_t spi_errors{0U};
  uint32_t spi_prepare_fail{0U};
  int conversions{-1};
  uint32_t adc_opened{0U};
  uint32_t adc_open_fail{0U};
  uint32_t adc_start_fail{0U};
  uint32_t adc_starts{0U};
  uint32_t adc_opens{0U};
  std::vector<std::string> log{};
};

Model& M() {
  static Model model{};
  return model;
}

[[nodiscard]] Slot* SlotOf(uint8_t engine, uint8_t stream) {
  const uint8_t index = stm::sdk::DmaIndex(engine, stream);
  return (index < kStreams) ? &M().slots[index] : nullptr;
}

[[nodiscard]] std::size_t BitIndex(uint32_t flag) {
  std::size_t i = 0U;
  while ((i < 5U) && (flag != (1U << i))) {
    ++i;
  }
  return i;
}

// The engine may be shared between streams: only the stream armed last on it owns its flags.
uint32_t g_arm_counter = 0U;
std::array<uint32_t, kStreams> g_armed_at{};

[[nodiscard]] bool IsLastArmed(const Slot& s) {
  const auto self = static_cast<std::size_t>(&s - M().slots.data());
  bool last = true;
  for (std::size_t i = 0U; i < kStreams; ++i) {
    if ((i != self) && (M().slots[i].engine == s.engine) && (g_armed_at[i] > g_armed_at[self])) {
      last = false;
    }
  }
  return last;
}

[[nodiscard]] uint32_t EngineFlags(const Slot& s) {
  uint32_t flags = 0U;
  if ((s.engine != nullptr) && IsLastArmed(s)) {
    const uint32_t f = s.engine->Flags();
    flags |= ((f & h::kFlagComplete) != 0U) ? stm::kDmaFlagComplete : 0U;
    flags |= ((f & h::kFlagTransferError) != 0U) ? stm::kDmaFlagTransferError : 0U;
    flags |= ((f & h::kFlagFifoError) != 0U) ? stm::kDmaFlagFifoError : 0U;
  }
  if (s.ring != nullptr) {
    const uint32_t f = s.ring->Flags();
    flags |= ((f & h::kRingHalfTransfer) != 0U) ? stm::kDmaFlagHalf : 0U;
    flags |= ((f & h::kRingTransferComplete) != 0U) ? stm::kDmaFlagComplete : 0U;
  }
  return flags | s.extra;
}

[[nodiscard]] std::byte* Pointer(std::uintptr_t address) {
  return reinterpret_cast<std::byte*>(address); // NOLINT: test code, the model's memory
}

void Log(std::string entry) {
  M().log.push_back(std::move(entry));
}

} // namespace

namespace dma_model {

void Reset() {
  M() = Model{};
  g_arm_counter = 0U;
  g_armed_at.fill(0U);
}
void BindMachine(h::Machine* machine) {
  M().machine = machine;
}
void BindEngine(uint8_t engine, uint8_t stream, h::DmaEngine* dma, uint8_t seed) {
  Slot* s = SlotOf(engine, stream);
  if (s != nullptr) {
    s->engine = dma;
    s->seed = seed;
  }
}
void BindRing(uint8_t engine, uint8_t stream, h::HwRing* ring, h::Machine* machine) {
  Slot* s = SlotOf(engine, stream);
  if (s != nullptr) {
    s->ring = ring;
    s->ring_machine = machine;
  }
}
void SetSeed(uint8_t engine, uint8_t stream, uint8_t seed) {
  Slot* s = SlotOf(engine, stream);
  if (s != nullptr) {
    s->seed = seed;
  }
}
void SetFaults(const BoundaryFaults& faults) {
  M().faults = faults;
}
void ForcePriorityReadback(int value) {
  M().priority_readback = value;
}
void SetCache(bool on, bool mpu) {
  M().cache_on = on;
  M().mpu = mpu;
}
void SetForeignEnabled(uint8_t engine, uint8_t stream, bool on) {
  Slot* s = SlotOf(engine, stream);
  if (s != nullptr) {
    s->foreign = on;
  }
}
void SetSpiRuns(bool on) {
  M().spi_runs = on;
}
void SetSpiErrors(uint32_t errors) {
  M().spi_errors = errors;
}
void FailSpiPrepare(uint32_t code) {
  M().spi_prepare_fail = code;
}
void ForceConversions(int count) {
  M().conversions = count;
}
void FailAdcOpen(uint32_t code) {
  M().adc_open_fail = code;
}
void FailAdcStart(uint32_t code) {
  M().adc_start_fail = code;
}
uint32_t AdcStarts() {
  return M().adc_starts;
}
uint32_t AdcOpens() {
  return M().adc_opens;
}

uint32_t Nvic() {
  uint32_t runs = 0U;
  for (uint8_t e = 1U; e <= 2U; ++e) {
    for (uint8_t st = 0U; st <= 8U; ++st) {
      Slot* s = SlotOf(e, st);
      if ((s != nullptr) && s->irq &&
          ((s->engine != nullptr) || (s->ring != nullptr) || (s->extra != 0U))) {
        const uint32_t flags = EngineFlags(*s);
        const uint32_t errors = stm::kDmaFlagTransferError | stm::kDmaFlagDirectError;
        if (((flags & s->armed) != 0U) || ((flags & errors) != 0U) || (s->extra != 0U)) {
          stm::HandleDmaIrq(e, st);
          ++runs;
        }
      }
    }
  }
  return runs;
}

void Tick() {
  std::array<h::DmaEngine*, kStreams> ticked{};
  std::size_t n = 0U;
  for (Slot& s : M().slots) {
    bool seen = false;
    for (std::size_t i = 0U; i < n; ++i) {
      seen = seen || (ticked[i] == s.engine);
    }
    if ((s.engine != nullptr) && !seen) {
      s.engine->Tick();
      ticked[n] = s.engine;
      ++n;
    }
  }
  static_cast<void>(Nvic());
}

bool IrqEnabled(uint8_t engine, uint8_t stream) {
  const Slot* s = SlotOf(engine, stream);
  return (s != nullptr) && s->irq;
}
uint8_t PriorityWritten(uint8_t engine, uint8_t stream) {
  const Slot* s = SlotOf(engine, stream);
  return (s != nullptr) ? s->priority : 0xFFU;
}
uint32_t ArmedIrqs(uint8_t engine, uint8_t stream) {
  const Slot* s = SlotOf(engine, stream);
  return (s != nullptr) ? s->armed : 0U;
}
uint16_t Request(uint8_t engine, uint8_t stream) {
  const Slot* s = SlotOf(engine, stream);
  return (s != nullptr) ? s->request : 0U;
}
bool Circular(uint8_t engine, uint8_t stream) {
  const Slot* s = SlotOf(engine, stream);
  return (s != nullptr) && s->circular;
}
uint32_t Clears(uint8_t engine, uint8_t stream, uint32_t flag) {
  const Slot* s = SlotOf(engine, stream);
  const std::size_t i = BitIndex(flag);
  return ((s != nullptr) && (i < 5U)) ? s->clears[i] : 0U;
}
void RaiseFlags(uint8_t engine, uint8_t stream, uint32_t flags) {
  Slot* s = SlotOf(engine, stream);
  if (s != nullptr) {
    s->extra |= flags;
  }
}
const std::vector<std::string>& Log() {
  return M().log;
}
std::size_t Find(const std::string& entry) {
  std::size_t i = 0U;
  while ((i < M().log.size()) && (M().log[i] != entry)) {
    ++i;
  }
  return i;
}

} // namespace dma_model

// --- the boundary calls --------------------------------------------------------------------------
namespace kb::port::stm32::sdk {

uint8_t DmaIndex(uint8_t engine, uint8_t stream) noexcept {
  // As F4 and H7: streams 0 to 7 (the G4/L4 channel numbers 1 to 8 map to index - 1 on target).
  uint8_t index = kDmaNoIndex;
  if (((engine == 1U) || (engine == 2U)) && (stream < 8U)) {
    index = static_cast<uint8_t>(((engine - 1U) * 8U) + stream);
  }
  return index;
}

bool DmaRequestValid(uint16_t request) noexcept {
  return request <= 127U;
}

uint32_t DmaConfigure(const DmaProgram& program) noexcept {
  Slot* s = SlotOf(program.assignment.engine, program.assignment.stream);
  uint32_t code = port_code::kInvalidArgument;
  if (s != nullptr) {
    s->configured = true;
    s->circular = program.assignment.mode == kb::dma::ChannelMode::Circular;
    s->request = program.assignment.request;
    s->item = DmaItemBytes(program.peripheral.width);
    s->dir = program.peripheral.dir;
    code = hal::kOk;
  }
  return code;
}

void DmaArm(uint8_t engine, uint8_t stream, std::uintptr_t memory, uint32_t items,
            uint32_t irq) noexcept {
  Slot* s = SlotOf(engine, stream);
  if (s != nullptr) {
    Log("arm:" + std::to_string(engine) + ":" + std::to_string(stream));
    s->armed = irq;
    s->length = static_cast<std::size_t>(items) * s->item;
    s->stopping = false;
    g_armed_at[DmaIndex(engine, stream)] = ++g_arm_counter;
    if (s->ring != nullptr) {
      s->ring->Attach(*s->ring_machine, Pointer(memory));
      s->ring_on = true;
    } else if (s->engine != nullptr) {
      static_cast<void>(s->engine->Start(s->dir, Pointer(memory), s->length, s->seed));
    } else {
      // Nothing bound: the stream never moves.
    }
  }
}

bool DmaEnabled(uint8_t engine, uint8_t stream) noexcept {
  Slot* s = SlotOf(engine, stream);
  bool on = false;
  if (s != nullptr) {
    if (s->foreign) {
      on = true;
    } else if (s->ring != nullptr) {
      on = s->ring_on;
    } else if ((s->engine != nullptr) && IsLastArmed(*s)) {
      if (s->stopping && M().faults.en_reads_zero) {
        on = false;
      } else {
        if (s->stopping) {
          s->engine->Tick(); // the CPU polls EN while the stream finishes its burst
        }
        on = s->engine->Enabled();
      }
    } else {
      on = false;
    }
  }
  return on;
}

void DmaDisable(uint8_t engine, uint8_t stream) noexcept {
  Slot* s = SlotOf(engine, stream);
  if (s != nullptr) {
    s->ring_on = false;
    if ((s->engine != nullptr) && IsLastArmed(*s)) {
      s->engine->RequestStop();
      s->stopping = true;
    }
  }
}

void DmaReset(uint8_t engine, uint8_t stream) noexcept {
  Slot* s = SlotOf(engine, stream);
  if (s != nullptr) {
    s->ring_on = false;
    s->stopping = false;
    s->extra = 0U;
    if ((s->engine != nullptr) && IsLastArmed(*s)) {
      s->engine->HardReset();
    }
  }
}

uint32_t DmaRemaining(uint8_t engine, uint8_t stream) noexcept {
  const Slot* s = SlotOf(engine, stream);
  uint32_t items = 0U;
  if ((s != nullptr) && (s->ring != nullptr)) {
    items = static_cast<uint32_t>((2U * h::HwRing::kSamples) - s->ring->Pos());
  } else if ((s != nullptr) && (s->engine != nullptr) && (s->item != 0U)) {
    const std::size_t moved = s->engine->Moved();
    const std::size_t left = (moved < s->length) ? (s->length - moved) : 0U;
    items = static_cast<uint32_t>(left / s->item);
  } else {
    items = 0U;
  }
  return items;
}

uint32_t DmaFlags(uint8_t engine, uint8_t stream) noexcept {
  const Slot* s = SlotOf(engine, stream);
  return (s != nullptr) ? EngineFlags(*s) : 0U;
}

void DmaClearFlag(uint8_t engine, uint8_t stream, uint32_t flag) noexcept {
  Slot* s = SlotOf(engine, stream);
  const std::size_t i = BitIndex(flag);
  if ((s != nullptr) && (i < 5U)) {
    ++s->clears[i];
    s->extra &= ~flag;
    if ((s->engine != nullptr) && IsLastArmed(*s)) {
      uint32_t mask = 0U;
      mask |= (flag == kDmaFlagComplete) ? h::kFlagComplete : 0U;
      mask |= (flag == kDmaFlagTransferError) ? h::kFlagTransferError : 0U;
      mask |= (flag == kDmaFlagFifoError) ? h::kFlagFifoError : 0U;
      s->engine->ClearFlags(mask);
    }
    if (s->ring != nullptr) {
      uint32_t mask = 0U;
      mask |= (flag == kDmaFlagHalf) ? h::kRingHalfTransfer : 0U;
      mask |= (flag == kDmaFlagComplete) ? h::kRingTransferComplete : 0U;
      s->ring->ClearFlags(mask);
    }
  }
}

void DmaIrqPriority(uint8_t engine, uint8_t stream, uint8_t priority) noexcept {
  Slot* s = SlotOf(engine, stream);
  if (s != nullptr) {
    s->priority = priority;
  }
}

uint8_t DmaIrqPriorityRead(uint8_t engine, uint8_t stream) noexcept {
  const Slot* s = SlotOf(engine, stream);
  uint8_t value = (s != nullptr) ? s->priority : 0U;
  if (M().priority_readback >= 0) {
    value = static_cast<uint8_t>(M().priority_readback);
  }
  return value;
}

void DmaIrqEnable(uint8_t engine, uint8_t stream, bool on) noexcept {
  Slot* s = SlotOf(engine, stream);
  if (s != nullptr) {
    s->irq = on;
  }
}

uint32_t DmaStopBoundCycles() noexcept {
  return 1024U;
}

void CacheClean(std::uintptr_t address, std::size_t bytes) noexcept {
  if ((M().machine != nullptr) && !M().faults.clean_noop) {
    M().machine->CleanRange(std::span<const std::byte>(Pointer(address), bytes));
  }
}

void CacheInvalidate(std::uintptr_t address, std::size_t bytes) noexcept {
  if ((M().machine != nullptr) && !M().faults.invalidate_noop) {
    M().machine->InvalidateRange(std::span<const std::byte>(Pointer(address), bytes));
  }
}

void DataBarrier() noexcept {
  if (M().machine != nullptr) {
    M().machine->Barrier();
  }
}

bool DataCacheOn() noexcept {
  return M().cache_on;
}

bool MpuUncached(std::uintptr_t base, std::size_t size) noexcept {
  static_cast<void>(base);
  static_cast<void>(size);
  return M().mpu;
}

// --- SPI by DMA ----------------------------------------------------------------------------------

std::uintptr_t SpiDmaTxRegister(uint8_t instance) noexcept {
  return 0x40013000U + (static_cast<std::uintptr_t>(instance) * 0x400U) + 0x20U;
}

std::uintptr_t SpiDmaRxRegister(uint8_t instance) noexcept {
  return 0x40013000U + (static_cast<std::uintptr_t>(instance) * 0x400U) + 0x30U;
}

uint32_t SpiDmaPrepare(uint8_t instance, uint32_t bytes) noexcept {
  Log("prepare:" + std::to_string(instance) + ":" + std::to_string(bytes));
  return M().spi_prepare_fail;
}

void SpiDmaGo(uint8_t instance) noexcept {
  Log("go:" + std::to_string(instance));
  if (M().spi_runs) {
    for (uint32_t i = 0U; i < 8192U; ++i) {
      bool any = false;
      for (Slot& s : M().slots) {
        any = any || ((s.engine != nullptr) && s.engine->Enabled() && IsLastArmed(s));
      }
      if (!any) {
        break;
      }
      dma_model::Tick();
    }
    static_cast<void>(dma_model::Nvic());
  }
}

uint32_t SpiDmaErrors(uint8_t instance) noexcept {
  static_cast<void>(instance);
  return M().spi_errors;
}

bool SpiDmaIdle(uint8_t instance) noexcept {
  static_cast<void>(instance);
  return true;
}

void SpiDmaFinish(uint8_t instance) noexcept {
  Log("finish:" + std::to_string(instance));
}

// --- ADC stream ----------------------------------------------------------------------------------

uint32_t AdcStreamOpen(uint8_t instance, std::span<const uint8_t> channels, AdcSampling sampling,
                       uint32_t trigger) noexcept {
  static_cast<void>(sampling);
  Log("adc-open:" + std::to_string(instance) + ":" + std::to_string(trigger));
  ++M().adc_opens;
  M().adc_opened = static_cast<uint32_t>(channels.size());
  return M().adc_open_fail;
}

uint32_t AdcStreamConversions(uint8_t instance) noexcept {
  static_cast<void>(instance);
  return (M().conversions >= 0) ? static_cast<uint32_t>(M().conversions) : M().adc_opened;
}

std::uintptr_t AdcStreamDataRegister(uint8_t instance) noexcept {
  return 0x40022040U + static_cast<std::uintptr_t>(instance);
}

uint32_t AdcStreamStart(uint8_t instance) noexcept {
  Log("adc-start:" + std::to_string(instance));
  ++M().adc_starts;
  return M().adc_start_fail;
}

bool AdcStreamTakeOverrun(uint8_t instance) noexcept {
  static_cast<void>(instance);
  bool overrun = false;
  for (Slot& s : M().slots) {
    if ((s.ring != nullptr) && ((s.ring->Flags() & h::kRingOverrun) != 0U)) {
      s.ring->ClearFlags(h::kRingOverrun);
      overrun = true;
    }
  }
  return overrun;
}

uint32_t AdcStreamStop(uint8_t instance) noexcept {
  Log("adc-stop:" + std::to_string(instance));
  return hal::kOk;
}

uint32_t AdcStreamClose(uint8_t instance) noexcept {
  Log("adc-close:" + std::to_string(instance));
  return hal::kOk;
}

} // namespace kb::port::stm32::sdk

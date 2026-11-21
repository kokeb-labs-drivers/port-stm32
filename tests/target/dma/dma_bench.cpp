// DMA bench app of the port (docs/assurance/bench/procedure-stm32h7.md): the on-target items of the
// DMA design, run by hand on a board with SPI1 MOSI jumpered to MISO, ADC inputs on a divider and a
// UART console. Each item prints one machine-readable line:
//   KB-BENCH <item> PASS|FAIL|SKIP key=value ...
// The H7 build places the staging buffers in the D2 window (an MPU region made non-cacheable here)
// and in AXI SRAM (cached, D-cache on), and runs the ADC scan stream from TIM6 at 1 kHz. The other
// families build the SPI items over their cacheless SRAM and print SKIP for the H7-only items.
// Bench code: built in CI (ci-cmake-matrix.yml), outside the MISRA claim.
// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The port-stm32 authors
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <span>
#include <string_view>

#include "board.hpp"
#include "kb/port/stm32/port.hpp"
#include "sdk.hpp"
#if defined(KB_PORT_STM32_G4)
#include "stm32g4xx_hal.h"
#elif defined(KB_PORT_STM32_H7)
#include "stm32h7xx_hal.h"
#elif defined(KB_PORT_STM32_F4)
#include "stm32f4xx_hal.h"
#elif defined(KB_PORT_STM32_L4)
#include "stm32l4xx_hal.h"
#endif

namespace stm = kb::port::stm32;
namespace d = kb::dma;

extern "C" {
void SysTick_Handler(void);
void SysTick_Handler(void) {
  HAL_IncTick();
}
}

namespace {

// --- the board's DMA declaration (what kb board gen writes from board.yml)
// -------------------------
#if defined(STM32H7)
constexpr uint16_t kSpiRxRequest = DMA_REQUEST_SPI1_RX;
constexpr uint16_t kSpiTxRequest = DMA_REQUEST_SPI1_TX;
constexpr uint16_t kAdcRequest = DMA_REQUEST_ADC1;
constexpr uint8_t kRxStream = 0U;
constexpr uint8_t kTxStream = 1U;
constexpr uint8_t kAdcStreamNo = 2U;
using StageRegion = stm::h7::D2Sram;
#elif defined(STM32G4)
constexpr uint16_t kSpiRxRequest = DMA_REQUEST_SPI1_RX;
constexpr uint16_t kSpiTxRequest = DMA_REQUEST_SPI1_TX;
constexpr uint16_t kAdcRequest = DMA_REQUEST_ADC1;
constexpr uint8_t kRxStream = 1U; // channels count from 1
constexpr uint8_t kTxStream = 2U;
constexpr uint8_t kAdcStreamNo = 3U;
using StageRegion = stm::Sram;
#elif defined(STM32L4)
constexpr uint16_t kSpiRxRequest = 1U; // CSELR: SPI1_RX on DMA1 channel 2, SPI1_TX on channel 3
constexpr uint16_t kSpiTxRequest = 1U;
constexpr uint16_t kAdcRequest = 0U;
constexpr uint8_t kRxStream = 2U;
constexpr uint8_t kTxStream = 3U;
constexpr uint8_t kAdcStreamNo = 1U;
using StageRegion = stm::Sram;
#else
constexpr uint16_t kSpiRxRequest = 3U; // CHSEL: SPI1 on DMA2 streams 0 (RX) and 3 (TX), channel 3
constexpr uint16_t kSpiTxRequest = 3U;
constexpr uint16_t kAdcRequest = 0U;
constexpr uint8_t kRxStream = 0U;
constexpr uint8_t kTxStream = 3U;
constexpr uint8_t kAdcStreamNo = 4U;
using StageRegion = stm::Sram;
#endif
#if defined(STM32F4)
constexpr uint8_t kSpiEngine = stm::kEngineDma2;
#else
constexpr uint8_t kSpiEngine = stm::kEngineDma1;
#endif
constexpr uint8_t kSpiIrq = 5U;

struct SpiRx {
  static constexpr d::StreamAssignment kAssignment{
      kSpiEngine, kRxStream,       kSpiRxRequest,  d::ChannelPriority::High,
      kSpiIrq,    d::Notify::Task, d::IrqUse::All, d::ChannelMode::Normal};
};
struct SpiTx {
  static constexpr d::StreamAssignment kAssignment{
      kSpiEngine, kTxStream,       kSpiTxRequest,         d::ChannelPriority::High,
      kSpiIrq,    d::Notify::Task, d::IrqUse::ErrorsOnly, d::ChannelMode::Normal};
};
struct AdcStream1 {
  static constexpr d::StreamAssignment kAssignment{stm::kEngineDma1,
                                                   kAdcStreamNo,
                                                   kAdcRequest,
                                                   d::ChannelPriority::High,
                                                   4U,
                                                   d::Notify::None,
                                                   d::IrqUse::All,
                                                   d::ChannelMode::Circular};
};
constexpr bool kChecked =
    (stm::RequireStream<SpiRx>(), stm::RequireStream<SpiTx>(), stm::RequireStream<AdcStream1>(),
     d::RequireSamePriority<SpiRx, SpiTx>(), stm::RequireStreamReaches<SpiRx, StageRegion>(), true);
static_assert(kChecked);

constexpr std::size_t kStage = 1028U; // the largest frame: a 1 kB block plus a 4-byte header

#if defined(STM32H7)
// The board's region tag for the D2 window its MPU region covers (what kb board gen writes): SRAM1,
// 128 KB at 0x30000000 (tests/target/dma/h7_dma.ld).
struct D2Window : stm::h7::D2Sram {
  static constexpr std::size_t kSize = 0x20000U;
};
#endif

// The staging buffers in their region's section (the generated board file's one attribute).
// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables)
#if defined(STM32H7)
[[gnu::section(".kb_dma.d2")]] d::Buffer<D2Window, kStage> g_d2_tx{};
[[gnu::section(".kb_dma.d2")]] d::Buffer<D2Window, kStage> g_d2_rx{};
[[gnu::section(".kb_dma.axi")]] d::Buffer<stm::h7::AxiSram, kStage> g_axi_tx{};
[[gnu::section(".kb_dma.axi")]] d::Buffer<stm::h7::AxiSram, kStage> g_axi_rx{};
[[gnu::section(".kb_dma.d2")]] stm::AdcDmaStream<stm::AdcScan<6U>, D2Window>::Ring g_ring{};
// A DTCM buffer that DMA1 cannot reach: the forced transfer error (it bypasses the compile-time
// check by lending raw memory to a DmaStream directly).
alignas(32) std::array<std::byte, 64> g_dtcm{};
#else
d::Buffer<stm::Sram, kStage> g_d2_tx{};
d::Buffer<stm::Sram, kStage> g_d2_rx{};
#endif
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)

// --- console
// ----------------------------------------------------------------------------------------
stm::Uart& Console() {
  static stm::Uart uart{
      stm::UartConfig{board::kUart, board::kUartTx, board::kUartRx, stm::PinId{}, 115200U}};
  return uart;
}

void Line(std::string_view item, std::string_view verdict, const char* fmt, uint32_t a = 0U,
          uint32_t b = 0U, uint32_t c = 0U) {
  std::array<char, 160> buf{};
  const int head =
      std::snprintf(buf.data(), buf.size(), "KB-BENCH %.*s %.*s ", static_cast<int>(item.size()),
                    item.data(), static_cast<int>(verdict.size()), verdict.data());
  const int tail =
      std::snprintf(buf.data() + head, buf.size() - static_cast<std::size_t>(head), fmt,
                    static_cast<unsigned>(a), static_cast<unsigned>(b), static_cast<unsigned>(c));
  const std::size_t n = static_cast<std::size_t>(head + tail);
  std::array<uint8_t, 162> out{};
  std::memcpy(out.data(), buf.data(), n);
  out[n] = '\r';
  out[n + 1U] = '\n';
  static_cast<void>(Console().Write(std::span<const uint8_t>(out.data(), n + 2U)));
}

stm::Clock& Clock() {
  static stm::Clock clock{};
  return clock;
}
kb::Instant Now() noexcept {
  return Clock().Now();
}

// --- SPI loopback
// -----------------------------------------------------------------------------------
template <typename Link>
struct Loop {
  Link link;
  stm::SpiBus bus{stm::SpiBusConfig{board::kSpi, board::kSpiSck, board::kSpiMosi, board::kSpiMiso,
                                    0U, kSpiIrq}};
  stm::SpiDevice dev;
  Loop(typename Link::Stage& tx, typename Link::Stage& rx, uint32_t hz)
      : link(tx, rx, stm::SpiDmaConfig{SpiRx::kAssignment, SpiTx::kAssignment, 16U}),
        dev(bus, stm::SpiDeviceConfig{board::kSpiCs, 0U, hz, kb::Milliseconds{50U}}) {}
  kb::Status Start() {
    kb::Status s = bus.AttachDma(link);
    return (s == kb::Status::Ok) ? dev.EnsureReady() : s;
  }
};

std::array<uint8_t, kStage> g_out{};
std::array<uint8_t, kStage> g_in{};

// MOSI jumpered to MISO: every frame comes back as it went out.
template <typename Link>
void SpiLoopback(std::string_view item, Loop<Link>& loop, std::size_t bytes, uint32_t rounds) {
  uint32_t bad = 0U;
  uint32_t worst_us = 0U;
  for (uint32_t r = 0U; r < rounds; ++r) {
    for (std::size_t i = 0U; i < bytes; ++i) {
      g_out[i] = static_cast<uint8_t>((i * 7U) + r);
      g_in[i] = 0U;
    }
    const kb::Instant t0 = Now();
    const kb::Status s = loop.dev.Transfer(std::span<const uint8_t>(g_out.data(), bytes),
                                           std::span<uint8_t>(g_in.data(), bytes));
    const auto us = static_cast<uint32_t>(kb::utils::ElapsedUs(Now(), t0));
    worst_us = (us > worst_us) ? us : worst_us;
    bad +=
        ((s != kb::Status::Ok) || (std::memcmp(g_out.data(), g_in.data(), bytes) != 0)) ? 1U : 0U;
  }
  Line(item, (bad == 0U) ? "PASS" : "FAIL", "bytes=%u bad=%u worst_us=%u",
       static_cast<uint32_t>(bytes), bad, worst_us);
}

template <typename Link>
void SpiItems(std::string_view prefix, typename Link::Stage& tx, typename Link::Stage& rx) {
  Loop<Link> loop(tx, rx, 8000000U);
  const kb::Status s = loop.Start();
  if (s != kb::Status::Ok) {
    Line(prefix, "FAIL", "start=%u detail=%u", static_cast<uint32_t>(s),
         loop.bus.LastError().Detail());
    return;
  }
  // T1/T2/T3 on the real memory: small (interrupt, the CPU path), DMA, 1 kB at the full SCK.
  std::array<char, 32> name{};
  std::snprintf(name.data(), name.size(), "%.*s-it-8", static_cast<int>(prefix.size()),
                prefix.data());
  SpiLoopback(name.data(), loop, 8U, 100U);
  std::snprintf(name.data(), name.size(), "%.*s-dma-64", static_cast<int>(prefix.size()),
                prefix.data());
  SpiLoopback(name.data(), loop, 64U, 100U);
  std::snprintf(name.data(), name.size(), "%.*s-dma-1k", static_cast<int>(prefix.size()),
                prefix.data());
  SpiLoopback(name.data(), loop, 1024U, 4U);
  const stm::DmaStats rxs = loop.link.RxStream().Stats();
  Line("spi-isr-cost", "PASS", "max_isr_cycles=%u max_stop_cycles=%u frames=%u", rxs.max_isr_cycles,
       rxs.max_stop_cycles, loop.link.Stats().frames);
  // 1 kB frames at the interrupt ceiling, four times (the CPU stays in the interrupt handler).
  Loop<Link> slow(tx, rx, loop.bus.InterruptCeilingHz());
  static_cast<void>(loop.dev.Release());
  static_cast<void>(loop.bus.Release());
  if (slow.Start() == kb::Status::Ok) {
    SpiLoopback("spi-1k-at-it-ceiling-x4", slow, 1024U, 4U);
  }
}

#if defined(STM32H7)
// --- H7 memory set-up: MPU region 0 over the D2 window, non-cacheable; caches on
// ------------------
extern "C" uint32_t _sidata_kb_dma, _sdata_kb_dma_d2, _edata_kb_dma_d2;
extern "C" uint32_t _sidata_kb_dma_axi, _sdata_kb_dma_axi, _edata_kb_dma_axi;

void CopySection(const uint32_t* from, uint32_t* to, const uint32_t* end) {
  while (to < end) {
    *to = *from;
    ++to;
    ++from;
  }
}

void MemorySetup(bool mpu) {
  // The buffers are constant-initialised objects (their header holds the data address): their
  // sections are loaded from flash like .data, once, before anything uses them.
  static bool copied = false;
  if (!copied) {
    CopySection(&_sidata_kb_dma, &_sdata_kb_dma_d2, &_edata_kb_dma_d2);
    CopySection(&_sidata_kb_dma_axi, &_sdata_kb_dma_axi, &_edata_kb_dma_axi);
    copied = true;
  }
  SCB_DisableDCache();
  HAL_MPU_Disable();
  MPU_Region_InitTypeDef r{};
  r.Enable = mpu ? MPU_REGION_ENABLE : MPU_REGION_DISABLE;
  r.Number = MPU_REGION_NUMBER0;
  r.BaseAddress = 0x30000000U;
  r.Size = MPU_REGION_SIZE_128KB;
  r.SubRegionDisable = 0U;
  r.TypeExtField = MPU_TEX_LEVEL1;
  r.AccessPermission = MPU_REGION_FULL_ACCESS;
  r.DisableExec = MPU_INSTRUCTION_ACCESS_DISABLE;
  r.IsShareable = MPU_ACCESS_SHAREABLE;
  r.IsCacheable = MPU_ACCESS_NOT_CACHEABLE;
  r.IsBufferable = MPU_ACCESS_NOT_BUFFERABLE;
  HAL_MPU_ConfigRegion(&r);
  HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);
  SCB_EnableICache();
  SCB_EnableDCache();
}

void KernelClocks() {
  RCC_PeriphCLKInitTypeDef p{};
  p.PeriphClockSelection = RCC_PERIPHCLK_SPI123;
  p.Spi123ClockSelection = RCC_SPI123CLKSOURCE_CLKP; // per_ck = HSI at reset
  static_cast<void>(HAL_RCCEx_PeriphCLKConfig(&p));
}

void TransferErrorItem() {
  // T14 / B21 / B31: DMA1 cannot reach DTCM. A transmit stream lent DTCM memory faults on its first
  // read once the SPI asks for data: TE, the stream disables itself, the interrupt clears the flag
  // by its own bit and counts it, and does not re-enter.
  Loop<stm::SpiDmaLink<D2Window, kStage>> loop(g_d2_tx, g_d2_rx, 1000000U);
  static_cast<void>(loop.Start()); // the SPI is set up; its own streams stay idle
  constexpr d::StreamAssignment kRaw{stm::kEngineDma1, 4U,
                                     kSpiTxRequest,    d::ChannelPriority::High,
                                     kSpiIrq,          d::Notify::Task,
                                     d::IrqUse::All,   d::ChannelMode::Normal};
  stm::DmaStream s{kRaw, stm::DmaPeripheral{stm::sdk::SpiDmaTxRegister(board::kSpi),
                                            stm::DmaWidth::Byte, d::Direction::ToDevice}};
  kb::Status st = s.EnsureReady();
  if (st == kb::Status::Ok) {
    st = s.Begin(g_dtcm, 16U, kb::utils::Deadline::After(Now(), kb::Milliseconds{100U}));
    static_cast<void>(stm::sdk::SpiDmaPrepare(board::kSpi, 16U));
    stm::sdk::SpiDmaGo(board::kSpi);
  }
  HAL_Delay(5U);
  const stm::DmaPoll p = s.Poll(Now());
  const stm::DmaStats a = s.Stats();
  HAL_Delay(10U);
  const stm::DmaStats b = s.Stats();
  stm::sdk::SpiDmaFinish(board::kSpi);
  const bool ok = (p.status == kb::Status::Fault) && (a.transfer_errors >= 1U) &&
                  (b.transfer_errors == a.transfer_errors) &&
                  !stm::sdk::DmaEnabled(stm::kEngineDma1, 4U);
  Line("dma-te-dtcm", ok ? "PASS" : "FAIL", "status=%u te=%u reentries=%u",
       static_cast<uint32_t>(p.status), a.transfer_errors, b.unexpected - a.unexpected);
  static_cast<void>(s.Release());
}

void SpiOverrunItem() {
  // T14: the receive stream stalled (its request routed nowhere): the SPI overruns, the frame stops
  // at its deadline with both streams proven stopped, BusError with OVR.
  d::StreamAssignment rx = SpiRx::kAssignment;
  rx.request = 0U; // DMAMUX: no request
  stm::SpiDmaLink<D2Window, kStage> link{g_d2_tx, g_d2_rx,
                                         stm::SpiDmaConfig{rx, SpiTx::kAssignment, 16U}};
  stm::SpiBus bus{stm::SpiBusConfig{board::kSpi, board::kSpiSck, board::kSpiMosi, board::kSpiMiso,
                                    0U, kSpiIrq}};
  stm::SpiDevice dev{bus, stm::SpiDeviceConfig{board::kSpiCs, 0U, 1000000U, kb::Milliseconds{5U}}};
  static_cast<void>(bus.AttachDma(link));
  kb::Status st = dev.EnsureReady();
  if (st == kb::Status::Ok) {
    st = dev.Transfer(std::span<const uint8_t>(g_out.data(), 64U),
                      std::span<uint8_t>(g_in.data(), 64U));
  }
  const uint32_t detail = dev.LastError().Detail();
  Line("spi-ovr-forced", ((st == kb::Status::BusError) && !bus.Health().faulted) ? "PASS" : "FAIL",
       "status=%u detail=%u faulted=%u", static_cast<uint32_t>(st), detail,
       bus.Health().faulted ? 1U : 0U);
  static_cast<void>(dev.Release());
  static_cast<void>(bus.Release());
}

void EnClearItem() {
  // T7: a 1 kB frame at 125 kHz stopped by a 2 ms deadline: both streams stop with EN read 0; the
  // longest stop in core cycles is the measured bound.
  Loop<stm::SpiDmaLink<D2Window, kStage>> loop(g_d2_tx, g_d2_rx, 125000U);
  kb::Status st = loop.Start();
  stm::SpiDevice quick{loop.bus,
                       stm::SpiDeviceConfig{board::kSpiCs, 0U, 125000U, kb::Milliseconds{2U}}};
  static_cast<void>(loop.dev.Release());
  if (st == kb::Status::Ok) {
    st = quick.EnsureReady();
  }
  if (st == kb::Status::Ok) {
    st = quick.Transfer(std::span<const uint8_t>(g_out.data(), 1024U),
                        std::span<uint8_t>(g_in.data(), 1024U));
  }
  const stm::DmaStats rx = loop.link.RxStream().Stats();
  const stm::DmaStats tx = loop.link.TxStream().Stats();
  const uint32_t worst =
      (rx.max_stop_cycles > tx.max_stop_cycles) ? rx.max_stop_cycles : tx.max_stop_cycles;
  Line("t7-en-clear", ((st == kb::Status::Timeout) && !loop.bus.Health().faulted) ? "PASS" : "FAIL",
       "status=%u max_stop_cycles=%u bound=%u", static_cast<uint32_t>(st), worst,
       stm::sdk::DmaStopBoundCycles());
  static_cast<void>(quick.Release());
}

void PriorityItem() {
  // B32: a vendor initialisation set another priority; the port writes and reads back its own.
  HAL_NVIC_SetPriority(DMA1_Stream0_IRQn, 15U, 0U);
  stm::DmaStream s{SpiRx::kAssignment,
                   stm::DmaPeripheral{stm::sdk::SpiDmaRxRegister(board::kSpi), stm::DmaWidth::Byte,
                                      d::Direction::FromDevice}};
  const kb::Status st = s.EnsureReady();
  const uint32_t now = NVIC_GetPriority(DMA1_Stream0_IRQn);
  Line("nvic-priority-readback", ((st == kb::Status::Ok) && (now == kSpiIrq)) ? "PASS" : "FAIL",
       "status=%u priority=%u expected=%u", static_cast<uint32_t>(st), now, kSpiIrq);
  static_cast<void>(s.Release());
}

void MpuItem() {
  // A NonCacheable region without its MPU region while the D-cache is on: the start refuses.
  MemorySetup(false);
  stm::SpiDmaLink<D2Window, kStage> link{
      g_d2_tx, g_d2_rx, stm::SpiDmaConfig{SpiRx::kAssignment, SpiTx::kAssignment, 16U}};
  stm::SpiBus bus{stm::SpiBusConfig{board::kSpi, board::kSpiSck, board::kSpiMosi, board::kSpiMiso,
                                    0U, kSpiIrq}};
  static_cast<void>(bus.AttachDma(link));
  const kb::Status st = bus.EnsureReady();
  Line("mpu-readback", (st == kb::Status::NotReady) ? "PASS" : "FAIL", "status=%u detail=%u",
       static_cast<uint32_t>(st), bus.LastError().Detail());
  static_cast<void>(bus.Release());
  MemorySetup(true);
}

// --- ADC: TIM6 TRGO at 1 kHz, six ranks, circular DMA into the D2 ring
// ----------------------------- Nucleo-H743ZI2 style analog pins: PA3 (INP15), PC0 (INP10), PB1
// (INP5), PF11 (INP2), PA0 (INP16), PA4 (INP18); analog at reset
// (docs/assurance/bench/procedure-stm32h7.md).
constexpr std::array<uint8_t, 6> kRanks{15U, 10U, 5U, 2U, 16U, 18U};
TIM_HandleTypeDef g_tim6{};

void TriggerTimer(bool run) {
  if (g_tim6.Instance == nullptr) {
    __HAL_RCC_TIM6_CLK_ENABLE();
    g_tim6.Instance = TIM6;
    g_tim6.Init.Prescaler = (HAL_RCC_GetPCLK1Freq() / 1000000U) - 1U; // 1 MHz
    g_tim6.Init.Period = 999U;                                        // 1 kHz
    static_cast<void>(HAL_TIM_Base_Init(&g_tim6));
    TIM_MasterConfigTypeDef m{};
    m.MasterOutputTrigger = TIM_TRGO_UPDATE;
    static_cast<void>(HAL_TIMEx_MasterConfigSynchronization(&g_tim6, &m));
  }
  static_cast<void>(run ? HAL_TIM_Base_Start(&g_tim6) : HAL_TIM_Base_Stop(&g_tim6));
}

void AdcItems() {
  stm::AdcStreamConfig c{};
  c.instance = 1U;
  c.channels = kRanks;
  c.sampling = stm::AdcSampling::Medium;
  c.trigger = ADC_EXTERNALTRIG_T6_TRGO;
  c.dma = AdcStream1::kAssignment;
  c.shape = d::StreamShape{2U, 2U, kRanks.size()};
  c.supervision = d::StreamConfig{.frame_period = kb::Microseconds{1000U},
                                  .stall_periods = 10U,
                                  .bad_frames_to_restart = 0U,
                                  .restart_backoff = kb::Milliseconds{1000U},
                                  .restarts_before_fallback = 0U};
  c.now = &Now;
  static stm::AdcDmaStream<stm::AdcScan<6U>, D2Window> adc{g_ring, c};
  const kb::Status st = adc.Start();
  const kb::Status again = adc.Start();
  Line("adc-start",
       ((st == kb::Status::Ok) && (again == kb::Status::Ok) && (adc.Mode() == d::StreamMode::Dma))
           ? "PASS"
           : "FAIL",
       "status=%u again=%u readback=%u", static_cast<uint32_t>(st), static_cast<uint32_t>(again),
       adc.Core().ConversionsReadBack());
  TriggerTimer(true);
  // Rate and jitter over 2 s, the owner serving every millisecond.
  uint64_t last = 0U;
  uint32_t frames = 0U;
  uint32_t min_gap = 0xFFFFFFFFU;
  uint32_t max_gap = 0U;
  const kb::Instant t0 = Now();
  while (kb::utils::ElapsedUs(Now(), t0) < 2000000U) {
    d::Stream<stm::AdcScan<6U>, 2U, D2Window>::FrameView v{};
    while (adc.Stream().TryTake(v) == kb::Status::Ok) {
      const uint64_t at = v.At().Us();
      if (last != 0U) {
        const auto gap = static_cast<uint32_t>(at - last);
        min_gap = (gap < min_gap) ? gap : min_gap;
        max_gap = (gap > max_gap) ? gap : max_gap;
      }
      last = at;
      ++frames;
      adc.Stream().Give(std::move(v));
    }
  }
  const bool rate_ok = (frames >= 1990U) && (frames <= 2010U);
  Line("adc-1khz-x6-rate", rate_ok ? "PASS" : "FAIL", "frames=%u jitter_us=%u isr_cycles=%u",
       frames, max_gap - min_gap, adc.Stream().Stats().max_isr_cycles);
  // The consumer blocked for 50 ms: overruns counted, the next frame flagged as a gap.
  HAL_Delay(50U);
  static_cast<void>(adc.Service());
  const auto latest = adc.Latest();
  const d::StreamStats s1 = adc.Stream().Stats();
  Line("adc-consumer-blocked", ((s1.overruns > 0U) && latest.IsOk()) ? "PASS" : "FAIL",
       "overruns=%u gaps=%u hw_overruns=%u", s1.overruns, s1.gaps, s1.hw_overruns);
  // The trigger stopped: a stall after 10 periods, a restart; frames come back with the trigger.
  TriggerTimer(false);
  HAL_Delay(20U);
  const d::SupervisorAction action = adc.Service();
  TriggerTimer(true);
  HAL_Delay(20U);
  static_cast<void>(adc.Service());
  const d::StreamStats s2 = adc.Stream().Stats();
  Line("adc-stall-supervision",
       ((action == d::SupervisorAction::Restart) && (s2.stalls >= 1U) &&
        (s2.published > s1.published))
           ? "PASS"
           : "FAIL",
       "stalls=%u restarts=%u epoch=%u", s2.stalls, s2.restarts, s2.epoch);
  // ADC overrun with OVRMOD 0: the stream's requests stop; the restart reads OVR and counts it.
  stm::sdk::DmaDisable(stm::kEngineDma1, kAdcStreamNo);
  HAL_Delay(20U);
  static_cast<void>(adc.Service());
  HAL_Delay(5U);
  static_cast<void>(adc.Service());
  const d::StreamStats s3 = adc.Stream().Stats();
  Line("adc-ovr-counted", (s3.hw_overruns > s2.hw_overruns) ? "PASS" : "FAIL",
       "hw_overruns=%u restarts=%u", s3.hw_overruns, s3.restarts);
  TriggerTimer(false);
  static_cast<void>(adc.Stop());
}
#endif

} // namespace

int main() {
  static_cast<void>(HAL_Init());
  static_cast<void>(Clock().Start());
  static_cast<void>(Console().EnsureReady());
  Line("bench-start", "PASS", "core_hz=%u", SystemCoreClock);
#if defined(STM32H7)
  KernelClocks();
  MemorySetup(true);
  SpiItems<stm::SpiDmaLink<D2Window, kStage>>("spi-d2", g_d2_tx, g_d2_rx);
  SpiItems<stm::SpiDmaLink<stm::h7::AxiSram, kStage>>("spi-axi", g_axi_tx, g_axi_rx);
  TransferErrorItem();
  SpiOverrunItem();
  EnClearItem();
  PriorityItem();
  MpuItem();
  AdcItems();
#else
  SpiItems<stm::SpiDmaLink<stm::Sram, kStage>>("spi-sram", g_d2_tx, g_d2_rx);
  Line("h7-items", "SKIP", "family_has_no_dcache=%u", 1U);
#endif
  Line("bench-end", "PASS", "items=%u", 0U);
  while (true) {
  }
}

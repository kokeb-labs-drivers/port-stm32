# AGENTS.md

port-stm32: kb-core's contracts (`kb::iface`) over the STM32Cube HAL for the STM32 G4, H7, F4 and L4 families,
namespace `kb::port::stm32`, target `kb::port_stm32`. It follows the
[Kokeb Labs engineering standard](https://github.com/kokeb-labs/.github/blob/main/standards/README.md) and the
handbook at level `high` (`.kokeb/handbook.yml`).

## Layout

- `inc/kb/port/stm32/`: public headers, one per peripheral, no HAL type in any signature; `port.hpp` includes them
  all and proves every `*Ref`; `irq.hpp` holds the interrupt entry points.
- `src/<stem>.cpp`: the platform-free logic, one file per public header (`adc`, `can`, `completion`, `error`, `gpio`,
  `i2c`, `log`, `port`, `pwm`, `spi`, `time`, `uart`); `irq.hpp` and `version.hpp` are header-only
  (`tools/layout.yml`). Shared internals: `sdk.hpp` (the seam to the boundary), `state.hpp` (the shared state's
  type) and `clock_read.hpp` (the clock's read rule), used by several of these files.
- `src/boundary/<component>.cpp`: the HAL boundary, one file per HAL component (`i2c`, `spi`, `uart`, `gpio`, `adc`,
  `can`, `time`, `log`, `error`; `timer` for the TIM handles that PWM and the clock share, `state` for the shared
  state's one instance, `vectors` for the interrupt vectors), the only files that include HAL or CMSIS headers
  (`deviations/boundary-files.yml`). `tools/check_layout.py` (preflight, ctest) keeps header, source and boundary
  file in step: a new peripheral adds `inc/.../<x>.hpp`, `src/<x>.cpp` and `src/boundary/<x>.cpp`.
- `cmake/`: the pinned HAL fetch (`kb_port_stm32_hal.cmake`), the pinned FreeRTOS-Kernel fetch for the example
  (`kb_port_stm32_freertos.cmake`), the kb-core copy check, an Arm toolchain file.
- `tests/`: host tests; `tests/fakes/stm32/` fakes every HAL call the boundary files make.
- `examples/stm32/`: one firmware project for the four families (`boards/<board>/`); `drivers/` holds the pinned
  driver submodules it binds.
- `third_party/kb-core/`: the pinned, unchanged kb-core copy (`tools/kb_core_snapshot.py`, `third_party/kb-core.lock`).
- `_config/`: tool configuration; `tools/`: pre-flight, layering rules, the layout check, the snapshot tool, the firmware vector check
  (`check_vectors.cmake`).

## Rules

- Logic in the platform-free files, HAL calls in `src/boundary/`; a boundary function converts, calls and
  returns the vendor code, nothing more. Family differences use the device header's macros in boundary files only.
- Every wait bounded by a timeout of at most `kMaxTimeout`, measured on the port's clock (never `HAL_GetTick()`), and
  ended by an interrupt into a `Completion` (never a busy poll under an RTOS); every failure a `kb::Status` with the
  vendor code in `Detail()`.
- C++20, no heap, no exceptions, no RTTI, no virtual functions; each type `static_assert`ed against its concepts.
- The HAL is fetched at a pinned commit and never copied or changed; a new HAL version is a change of the pins in
  `cmake/kb_port_stm32_hal.cmake` and `docs/soup.md` together, with the firmware builds of every family.
- Never edit `third_party/kb-core` by hand; never edit inside a submodule.
- Every change: tests, docs and `CHANGELOG.md` together; `tools/preflight.sh` before a push; commits signed off;
  no attribution lines.
- Ask the maintainer before anything irreversible or public: releases, visibility, deleting branches or tags.

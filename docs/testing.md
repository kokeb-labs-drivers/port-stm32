# Testing and CI

## Host tests over HAL fakes

The port's logic (`src/*.cpp` outside the boundary list) and its boundary files are built unchanged on the host
against `tests/fakes/stm32`: one-line headers with the HAL's names (`stm32g4xx_hal.h`, `stm32f4xx_hal.h`) over one
declaration file with the register blocks, handle types and macros the boundary files use, and `fake_stm32_hal.cpp`,
which defines every HAL function they call. That is a link seam per HAL call: the tests inject any
`HAL_StatusTypeDef` and `ErrorCode` into any function (`fake::Fail`), drive a small model of each peripheral (pin
levels, I2C devices that ACK or NACK, the BUSY flag, SPI and UART bytes, ADC values, running timer counters, a
3-deep CAN FIFO with overrun, error counters and bus-off), stop `HAL_GetTick()` to prove no wait reads it, advance
the DWT counter, and re-enter the port from inside a HAL call or a time read (`fake::Hook`) to prove each lock. The
interrupt APIs (`*_IT`) call the port's HAL callbacks at once, as the interrupt would, and the UART model runs the
port's handler while a transmit interrupt is enabled; `fake::Stall` holds an area's interrupts to prove every
timeout and its stop-and-restore. Each variant has only the macros its family's HAL has (the F4 fake has no 7-bit
UART word).

| Test | Proves |
| --- | --- |
| `kb_port_stm32_tests_g4` | Every member of every type, every refusal and every HAL error path, over a G4-like device: I2C v2 (TIMINGR), FDCAN, ADC calibration; the `*Ref` of each contract at run time; the clock's read rule over every interleaving of its reads with the wrap interrupt (`test_clock_read.cpp`); the waits with the HAL tick stopped and with no clock at all (`test_wait.cpp`) |
| `kb_port_stm32_tests_f4` | The same tests over an F4-like device: I2C v1 (ClockSpeed), bxCAN, no ADC calibration, the SR-register UART flags |
| `kb_port_stm32_compile_fail_*` | Misuse is a compile error: a `*Ref` of the wrong contract, a copied controller, an output pin as an input |
| `kb_port_stm32_header_checks` | Each public header compiles on its own as ISO C++20 with the strict flags |
| `static_assert`s in `port.hpp`, beside each type and in the tests | Each type satisfies its concepts and converts to its `*Ref`; `Cycles()` and `CompareFor()` at compile time |

```bash
cmake -S . -B build -G Ninja -DKB_PORT_STM32_BUILD_TESTS=ON && cmake --build build && ctest --test-dir build
```

## Local pre-flight

`tools/preflight.sh` runs what CI runs, before a push: the kb-core copy check, clang-format 20, the tests with Clang
(strict flags) and GCC at -O2, ASan and UBSan, coverage at the high level's limits (lines 85 %, branches 75 %),
lizard (CCN 15, 60 lines, 6 parameters), clang-tidy 20, `misra_lint --strict`, the handbook check, the layering
check and the platform-free build. clang-tidy's configuration is checked with `--verify-config`, and a clang-tidy
that reports a configuration error fails the step (the host job's clang-tidy 18 once rejected a key and reported
nothing). The MISRA scan runs in the ci-tools MISRA image; the firmware builds need the Arm GNU toolchain.

The firmware builds run `tools/check_vectors.cmake` after each link: every vector of the port's vector object and
every HAL callback of the port's library must be a strong symbol of the firmware, not `Default_Handler`. Checked
on the firmware before this check existed, it reports every vector as weak.

## CI

Every check is a caller of a `kokeb-labs/ci-tools` reusable workflow at `@v1`, joined by one gate
(`CI gate / gate`).

| Caller | Reusable workflow | What |
| --- | --- | --- |
| `ci-host.yml` | `ru-host-ci` | GCC and Clang builds, the tests, clang-tidy 18 and cppcheck over `inc/` and `src/`, failing on findings |
| `ci-quality.yml` | `ru-host-quality` | Coverage, sanitizers and complexity at level high |
| `ci-misra.yml` | `ru-misra` | MISRA C++:2023 and the KB-CPP20 profile, host and Cortex-M traced builds, the boundary files' records |
| `ci-cpp-lint.yml` | `ru-cpp-lint` | clang-format 20 and clang-tidy 20 |
| `ci-cross-build.yml` | `ru-cross-build` | The platform-free logic for Cortex-M0+, M4F and M7F |
| `ci-cmake-matrix.yml` | `ru-cmake-matrix` | `all_drivers.elf` compiled and linked against the pinned HAL for G4, H7, F4 and L4 (bare metal) and for G4 under FreeRTOS, each with the vector check |
| `ci-layering.yml` | `ru-layering` | HAL headers only in boundary files, the poison build of the platform-free code |
| `ci-pins.yml` | `ru-pin-hygiene` | The driver submodules are tagged releases whose CI gate passed |
| `ci-docs.yml` | `ru-mkdocs-publish` | The strict site build and its checks |
| `ci-handbook.yml` | `ru-handbook-check` | The handbook's rules for a library at level high |
| `ci-dco.yml` | `dco` | Every commit of a pull request is signed off |

## Not verified here

The port has not been run on hardware by this repository's CI: the host tests prove the logic and the HAL call
sequence against fakes, and the firmware builds prove it compiles and links against the real HAL of each family.
Behaviour on silicon (TIMINGR at the bus, the interrupt-driven transfers and their timeouts, I2C recovery of a real
stuck slave and of a BUSY flag that stays set, FDCAN and bxCAN bus-off recovery and receive bursts, ADC accuracy,
UART at high baud rates under interrupt load, the FreeRTOS example) needs a bench run per family. The earlier
implementation's hardware observations that shaped these rules are quoted where they apply (I2C, UART, SPI, clock,
integration); they were made on STM32H7 hardware, not with this port.

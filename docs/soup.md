# SOUP and kb-core

## Software of unknown provenance

Fetched by `cmake/kb_port_stm32_hal.cmake` with CMake's FetchContent at a **commit** (never a branch), checked after
the fetch with `git rev-parse HEAD`; nothing is copied into this repository and nothing is changed. A local clone
of the same commit may stand in (`FETCHCONTENT_SOURCE_DIR_KB_STM32_HAL`, `..._KB_STM32_DEVICE`,
`..._KB_CMSIS_CORE`); its commit is checked too.

| Item | Tag | Commit | Use |
| --- | --- | --- | --- |
| `STMicroelectronics/stm32g4xx-hal-driver` | v1.2.7 | `f5929f431f9effe45fbe18f5337e4753ced9ac92` | G4 HAL |
| `STMicroelectronics/cmsis-device-g4` | v1.2.6 | `25664ddc3a7624ae9627ae8c4c672073dc5b2539` | G4 device header, startup, system file |
| `STMicroelectronics/stm32h7xx-hal-driver` | v1.11.7 | `65e5c7e40456f4d76d79f972738a16cb3a17762c` | H7 HAL |
| `STMicroelectronics/cmsis-device-h7` | v1.10.8 | `277b283ad9ab79ea85ee42920e371b019d7defb0` | H7 device files |
| `STMicroelectronics/stm32f4xx-hal-driver` | v1.8.6 | `6ef30cf0ef6cc29ad518f37b9e6de89b6b815f90` | F4 HAL |
| `STMicroelectronics/cmsis-device-f4` | v2.6.11 | `0fa0e489e053fa1ca7790bb40b4d76458f64c55d` | F4 device files |
| `STMicroelectronics/stm32l4xx-hal-driver` | v1.13.6 | `047a8610c7dc0dcfdb4cd63b86cd3a2cbd3e5899` | L4 HAL |
| `STMicroelectronics/cmsis-device-l4` | v1.7.5 | `dc04e41afbd7c204cd09f2fa5dd29658efbaff6c` | L4 device files |
| `STMicroelectronics/cmsis-core` | v5.9.0_20250520 | `b7487de1303e2eb77c402432e368691e9dcfb5f0` | CMSIS-Core (core_cm4.h, core_cm7.h: NVIC, DWT, ITM) |
| The pinned drivers | v0.1.0-rc.1 each | `drivers/drivers.lock.yml` | Examples only |

The HAL sources are compiled as their own static library (`kb_port_stm32_hal`) with their own warnings off; their
headers reach the port as system headers. Known anomalies the port handles:

| Anomaly | How the port handles it |
| --- | --- |
| One `HAL_I2C_ERROR_AF` bit for address and data NACK | One `HAL_I2C_IsDeviceReady` probe tells them apart |
| `HAL_I2C_Mem_Read` takes a 1- or 2-byte register address | `WriteRead()` with more is `Unsupported` |
| `HAL_I2C_Master_Transmit` takes a non-const data pointer | One `const_cast` at that call (DEV-002); the HAL only reads the data |
| The HAL I2C has no bus recovery | The port clocks SCL itself on the pins as GPIO |
| HAL lengths are `uint16_t` | I2C refuses spans above 65535 bytes; SPI and UART transfer in chunks |
| `HAL_UART_IRQHandler` and its callbacks expect the HAL's interrupt receive state machine | The port's own handler reads the data and flag registers |
| A HAL SPI timeout leaves the peripheral mid-frame | `HAL_SPI_Abort`, then a fresh `HAL_SPI_Init` before the next frame |
| FDCAN stays in INIT after bus-off | `Recover()` stops and starts it |

## kb-core

kb-core (`kokeb-labs/kb-core`) is private and not yet tagged, so this repository's CI cannot fetch it. The port
therefore carries an **unchanged copy** of the kb-core files it builds against in `third_party/kb-core`, at a commit
of kb-core's `main` recorded in `third_party/kb-core.lock` with the SHA-256 of every file:

- `tools/kb_core_snapshot.py --update <kb-core checkout>` is the only writer; it refuses a commit that is not on
  kb-core's `origin/main` and a checkout with local changes in the copied paths;
- `tools/kb_core_snapshot.py --check` and every CMake configure re-hash every file against the lock; a difference
  stops the build;
- the port uses kb-core only through its public `kb/<part>/` headers (kb-core ADR-0007), checked by the layering
  rules (`tools/layering/rules.yml`).

The copy is interim. [How the port depends on kb-core](kb-core-dependency.md) is the plan that replaces it with a
release tag fetched by `find_package` or `FetchContent`.

| kb-core | Commit | Parts used |
| --- | --- | --- |
| `main` (M1 to M5: utils, rtos, interfaces, handlers) | `efd67da9b3c8`, see `third_party/kb-core.lock` | `utils`, `rtos` (none or FreeRTOS backend on target, none on the host), `interfaces` |

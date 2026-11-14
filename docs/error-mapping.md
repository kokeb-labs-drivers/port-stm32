# Error mapping

Every call returns `kb::Status` or `kb::Result<T>`. The HAL result behind a failure is kept as the vendor code: in
`Detail()` of a `kb::Result`, and in `LastError().Detail()` of the object for calls that return a bare
`kb::Status`.

## The vendor code

`VendorCode(status, error_code)` in `kb/port/stm32/error.hpp`:

| Bits | Content |
| --- | --- |
| 0 to 7 | The `HAL_StatusTypeDef` (0 to 3), or a port code (0xF1 to 0xF5) for a refusal made before any HAL call |
| 8 to 31 | The handle's `ErrorCode` after the call (`HAL_I2C_ERROR_*`, `HAL_SPI_ERROR_*`, `HAL_UART_ERROR_*`, `HAL_FDCAN_ERROR_*`, `HAL_CAN_ERROR_*`, `HAL_ADC_ERROR_*`), or the port's `uart_flags` for receive errors |

For example `0x0401` is `HAL_ERROR` with `HAL_I2C_ERROR_AF` (bit 2) set.

## The general mapping

`MapHalStatus()`; the four HAL values are checked against `HAL_StatusTypeDef` by a `static_assert` in
`src/stm32_io_boundary.cpp`, so a HAL change stops the build.

| Code | Value | kb::Status |
| --- | --- | --- |
| `HAL_OK` | 0x00 | `Ok` |
| `HAL_ERROR` | 0x01 | `Fault` (refined per peripheral below) |
| `HAL_BUSY` | 0x02 | `Busy` |
| `HAL_TIMEOUT` | 0x03 | `Timeout` |
| `port_code::kInvalidArgument` | 0xF1 | `InvalidArgument` |
| `port_code::kNotReady` | 0xF2 | `NotReady` |
| `port_code::kLockTimeout` | 0xF3 | `Busy` (the object's lock was not had in time) |
| `port_code::kUnsupported` | 0xF4 | `Unsupported` |
| `port_code::kOwned` | 0xF5 | `Busy` (the instance or pin belongs to another object) |
| anything else | | `Fault` |

## Where a peripheral refines it

| Peripheral | HAL result | kb::Status | Why |
| --- | --- | --- | --- |
| I2C | `HAL_ERROR` + `HAL_I2C_ERROR_AF`, the address probe not acknowledged | `NoDevice` (`AddressNack`) | The HAL has one bit for every NACK; one `HAL_I2C_IsDeviceReady` probe tells them apart |
| I2C | `HAL_ERROR` + `HAL_I2C_ERROR_AF`, the probe acknowledged | `Nack` (`DataNack`) | |
| I2C | Any failure with SDA or SCL low afterwards | `BusError` (`BusStuck`) | A stuck line, whatever the HAL said |
| I2C | `HAL_TIMEOUT`, lines high | `Timeout` | Clock stretching beyond the timeout |
| I2C | Other `HAL_ERROR` (`BERR`, `ARLO`, `OVR`) | `BusError` (`Bus`) | |
| SPI | `HAL_ERROR` (`MODF`, `OVR`, `CRC`, `FRE`) | `BusError` | The frame was disturbed |
| SPI | `HAL_TIMEOUT` | `Timeout` | The peripheral is aborted and initialised again |
| UART | Receive overrun, ring full | `Overflow` | Detail: `uart_flags` (`kOverrun`, `kRingFull`) |
| UART | Framing, parity, noise | `BusError` | Detail: `uart_flags` (`kFrameError`, `kParityError`, `kNoise`) |
| CAN | `Send` while bus-off | `BusError` | Nothing leaves a bus-off controller |
| CAN | `HAL_ERROR` from the add-message call | `BusError` | The frame was refused |
| ADC | `HAL_TIMEOUT` from `HAL_ADC_PollForConversion` | `Timeout` | |

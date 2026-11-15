# Error mapping

Every call returns `kb::Status` or `kb::Result<T>`. The HAL result behind a failure is kept as the vendor code: in
`Detail()` of a `kb::Result`, and in `LastError().Detail()` of the object for calls that return a bare
`kb::Status`.

## The vendor code

`VendorCode(status, error_code)` in `kb/port/stm32/error.hpp`:

| Bits | Content |
| --- | --- |
| 0 to 7 | The `HAL_StatusTypeDef` (0 to 3), or a port code (0xF1 to 0xF9) for a refusal made before any HAL call |
| 8 to 31 | The handle's `ErrorCode` after the call (`HAL_I2C_ERROR_*`, `HAL_SPI_ERROR_*`, `HAL_UART_ERROR_*`, `HAL_FDCAN_ERROR_*`, `HAL_CAN_ERROR_*`, `HAL_ADC_ERROR_*`), or the port's `uart_flags` for receive errors |

For example `0x0401` is `HAL_ERROR` with `HAL_I2C_ERROR_AF` (bit 2) set.

## The general mapping

`MapHalStatus()`; the four HAL values are checked against `HAL_StatusTypeDef` by a `static_assert` in
`src/boundary/error.cpp`, so a HAL change stops the build.

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
| `port_code::kOwned` | 0xF5 | `Busy` (the instance, pin or timer belongs to another object) |
| `port_code::kConflict` | 0xF6 | `InvalidArgument` (another frequency on a timer whose channels share one) |
| `port_code::kRtosObject` | 0xF7 | `Fault` (a kb::rtos mutex or semaphore could not be created) |
| `port_code::kHalTickStopped` | 0xF8 | `NotReady` (the HAL time base does not advance) |
| `port_code::kNoClock` | 0xF9 | `NotReady` (a kernel clock reads 0 Hz) |
| anything else | | `Fault` |

## Where a peripheral refines it

| Peripheral | HAL result | kb::Status | Why |
| --- | --- | --- | --- |
| I2C | `HAL_ERROR` + `HAL_I2C_ERROR_AF` on a read or an empty write | `NoDevice` (`AddressNack`) | Only the address was sent |
| I2C | `HAL_ERROR` + `HAL_I2C_ERROR_AF` on a write | `Nack` (`DataNack`) | The HAL has one bit for every NACK: the address or a data byte. With `nack_probe`, an address probe splits them (`NoDevice` when the probe is not acknowledged) |
| I2C | `HAL_BUSY` from the start (BUSY set), lines high | `BusError` (`Bus`) | The peripheral is reset while BUSY stays set |
| I2C | Any failure with SDA or SCL low afterwards | `BusError` (`BusStuck`) | A stuck line, whatever the HAL said |
| I2C | No completion within the timeout (`HAL_TIMEOUT` + `HAL_I2C_ERROR_TIMEOUT`), lines high | `Timeout` | Clock stretching beyond the timeout |
| I2C | Other `HAL_ERROR` (`BERR`, `ARLO`, `OVR`) | `BusError` (`Bus`) | |
| SPI | `HAL_ERROR` (`MODF`, `OVR`, `CRC`, `FRE`) | `BusError` | The frame was disturbed |
| SPI | No completion within the timeout (`HAL_TIMEOUT`, the abort's `ErrorCode`) | `Timeout` | The peripheral is aborted and initialised again |
| UART | Receive overrun, ring full | `Overflow` | Detail: `uart_flags` (`kOverrun`, `kRingFull`) |
| UART | Framing, parity, noise | `BusError` | Detail: `uart_flags` (`kFrameError`, `kParityError`, `kNoise`) |
| CAN | `Send` while bus-off | `BusError` | Nothing leaves a bus-off controller |
| CAN | `HAL_ERROR` from the add-message call | `BusError` | The frame was refused |
| ADC | No end-of-conversion interrupt within the timeout (`HAL_TIMEOUT`, the stop's `ErrorCode`) | `Timeout` | The conversion is stopped |
| ADC | `HAL_ADC_ErrorCallback` | `Fault` | Detail: `HAL_ADC_ERROR_*` |
| CAN | RX FIFO overrun or the port's ring full | `Overflow` | Detail: `can_flags` (`kFifoOverrun`, `kRingFull`) |
| Any | A timeout above `kMaxTimeout` (2 s) | `InvalidArgument` | No wait can become the HAL's `HAL_MAX_DELAY` |

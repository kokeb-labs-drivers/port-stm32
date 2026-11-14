# I2C

`kb::port::stm32::I2cController` satisfies `kb::iface::I2cBus` over the HAL's I2C module
(`stm32<family>xx_hal_i2c`), blocking calls.

## How it works

| Step | HAL call |
| --- | --- |
| `EnsureReady()` | The instance clock, SCL and SDA as open-drain alternate function, then `HAL_I2C_Init`: `Timing` from `ComputeI2cTiming()` on G4, H7 and L4 (or `I2cConfig::timing` as given), `ClockSpeed` and duty cycle 2 on F4 |
| `Write(addr, tx)` | `HAL_I2C_Master_Transmit`; an empty `tx` is an address probe, `HAL_I2C_IsDeviceReady` with one trial |
| `WriteRead(addr, tx, rx)` | `HAL_I2C_Mem_Read` with `tx` (1 or 2 bytes) as the memory address: repeated START |
| `Read(addr, rx)` | `HAL_I2C_Master_Receive` |
| `RecoverBus()` | `HAL_I2C_DeInit`, SCL and SDA as open-drain GPIO, up to nine SCL clocks at 100 kHz until SDA is high, a STOP, both lines read back, then `HAL_I2C_Init` again |
| `Release()` | `HAL_I2C_DeInit` |

The controller owns the instance: a second `I2cController` on it gets `Busy` from `EnsureReady()`.

## Pins and clock

| Item | Note |
| --- | --- |
| SCL, SDA | `PinId` with the I2C alternate function of the device (AF4 on most pins; check the datasheet). Open-drain |
| Pull-ups | External 2.2 kOhm to 4.7 kOhm to the bus voltage. `internal_pullups` enables the weak internal ones (about 40 kOhm): short buses at 100 kHz only |
| Bus clock | `clock_hz` 10 kHz to 1 MHz on G4, H7 and L4 (Fast-mode Plus needs FM+ capable pins), 10 kHz to 400 kHz on F4 |
| Kernel clock (G4, H7, L4) | `kernel_clock_hz`, or PCLK1 (the reset selection) when 0. TIMINGR is computed for it and must be at least 4 times the bus clock; `InvalidArgument` when no prescaler fits |
| TIMINGR | `ComputeI2cTiming()` meets the I2C specification's minimum SCL low and high times, data setup time and rise time of the mode (standard, fast, fast-mode plus); give `timing` to use a value from STM32CubeMX instead |

## Timing limits

| Wait | Bound |
| --- | --- |
| The controller lock | `I2cConfig::timeout` (default 50 ms); `Busy` when not had, nothing is sent |
| One transfer | `I2cConfig::timeout` as the HAL's `Timeout` argument (each flag wait inside the HAL) |
| Clock stretching | Inside the HAL's flag waits; a longer stretch ends as `Timeout` |
| A failed transfer | At most one extra probe of the address (to tell address from data NACK), and one recovery |
| Recovery | Nine clocks of 10 us, a STOP: about 100 us |
| Length | At most 65535 bytes each way (the HAL's `uint16_t`); more is `InvalidArgument` |

## Results and failure kinds

| Result | `LastFailure()` | When |
| --- | --- | --- |
| `Ok` | `None` | The transfer completed |
| `NoDevice` | `AddressNack` | `HAL_I2C_ERROR_AF` and the probe of the address was not acknowledged; or an empty write's probe was not acknowledged |
| `Nack` | `DataNack` | `HAL_I2C_ERROR_AF` and the probe was acknowledged: the device refused a data byte |
| `Timeout` | `Timeout` | `HAL_TIMEOUT` with both lines high (clock stretching too long, a device that holds SCL) |
| `BusError` | `Bus` | `HAL_I2C_ERROR_BERR`, `HAL_I2C_ERROR_ARLO` or another HAL error, with both lines high |
| `BusError` | `BusStuck` | Any failure with SDA or SCL read low afterwards; with `auto_recover` (default) RecoverBus() runs at once |
| `Unsupported` | unchanged | `WriteRead()` with more than 2 bytes to write |
| `InvalidArgument` | unchanged | Address outside 0x08 to 0x77, an empty read, a span above 65535 bytes, a bad configuration |
| `Busy` | unchanged | The lock was not had within the timeout, or the instance is owned by another object |
| `NotReady` | unchanged | Before `EnsureReady()` |

`LastError().Detail()` is the vendor code: the `HAL_StatusTypeDef` in bits 0 to 7 and the handle's `ErrorCode`
(`HAL_I2C_ERROR_*`) from bit 8. `Stats()` counts transfers, failures, recoveries and lock timeouts.

## Bus recovery

A slave that lost a clock edge (a reset in the middle of a read, a glitch) can hold SDA low forever. After any
failed transfer the controller reads SDA and SCL; when one is low the failure is `BusStuck` and, with
`auto_recover`, the recovery follows at once: the peripheral is released, the pins are taken as open-drain GPIO,
SCL is clocked until the slave lets SDA go (at most nine clocks), a STOP is made and the peripheral is initialised
again. The transfer itself is not retried: the caller decides. A line still low after the recovery is reported as
`BusError` with `LastFailure()` `BusStuck` (a short, a missing pull-up, a device in reset).

## Example

```cpp
stm::I2cConfig cfg{};
cfg.instance = 1U;
cfg.scl = stm::PinId{stm::GpioPort::B, 8U, 4U};
cfg.sda = stm::PinId{stm::GpioPort::B, 9U, 4U};
cfg.clock_hz = 400000U;
static stm::I2cController bus{cfg};
std::array<uint8_t, 2> rx{};
const std::array<uint8_t, 1> reg{0x00U};
if (bus.WriteRead(0x4AU, reg, rx) == kb::Status::NoDevice) {
  // nothing at 0x4A: check the address strap and the wiring
}
```

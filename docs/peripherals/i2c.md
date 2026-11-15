# I2C

`kb::port::stm32::I2cController` satisfies `kb::iface::I2cBus` over the HAL's I2C module
(`stm32<family>xx_hal_i2c`), interrupt-driven transfers: the calling task waits on the port's clock for the HAL's
completion callback, asleep under an RTOS ([How the port waits](../waits.md)).

## How it works

| Step | HAL call |
| --- | --- |
| `EnsureReady()` | The instance clock, SCL and SDA as open-drain alternate function, then `HAL_I2C_Init`: `Timing` from `ComputeI2cTiming()` on G4, H7 and L4 (or `I2cConfig::timing` as given), `ClockSpeed` and duty cycle 2 on F4 |
| Before every transfer | I2C v2: a latched TXIS cleared with a dummy byte, TXDR flushed, RXDR read, the CR2 transfer fields (NBYTES, RELOAD, AUTOEND, START, STOP, RD_WRN) cleared |
| `Write(addr, tx)` | `HAL_I2C_Master_Seq_Transmit_IT` (first and last frame); an empty `tx` is an address probe, `HAL_I2C_Master_Transmit_IT` of no data |
| `WriteRead(addr, tx, rx)` | `HAL_I2C_Master_Seq_Transmit_IT` of `tx` (any length) without STOP, then `HAL_I2C_Master_Seq_Receive_IT` of `rx` after a repeated START |
| `Read(addr, rx)` | `HAL_I2C_Master_Seq_Receive_IT` (first and last frame) |
| After a failure | The handle back to READY and unlocked, the peripheral's interrupts off, the error flags (NACK, STOP, BERR, ARLO, OVR) cleared, the data registers flushed; if BUSY is still set, the peripheral is reset (`HAL_I2C_DeInit` and `HAL_I2C_Init`: PE cleared; SWRST on F4) |
| `RecoverBus()` | `HAL_I2C_DeInit`, SCL and SDA as open-drain GPIO with the configured pull, up to nine SCL clocks at 100 kHz until SDA is high, a STOP, both lines read back, then `HAL_I2C_Init` again |
| `Release()` | `HAL_I2C_DeInit` |

The controller owns the instance: a second `I2cController` on it gets `Busy` from `EnsureReady()`.

## Pins and clock

| Item | Note |
| --- | --- |
| SCL, SDA | `PinId` with the I2C alternate function of the device (AF4 on most pins; check the datasheet). Open-drain |
| Pull-ups | External 2.2 kOhm to 4.7 kOhm to the bus voltage. `internal_pullups` enables the weak internal ones (about 40 kOhm): short buses at 100 kHz only |
| Bus clock | `clock_hz` 10 kHz to 1 MHz on G4, H7 and L4 (Fast-mode Plus needs FM+ capable pins), 10 kHz to 400 kHz on F4 |
| Kernel clock (G4, H7, L4) | `kernel_clock_hz`, or PCLK1 (the reset selection) when 0. TIMINGR is computed for it and must be at least 4 times the bus clock; `InvalidArgument` when no prescaler fits |
| TIMINGR | `ComputeI2cTiming()` meets the I2C specification's minimum SCL low and high times, data setup time and rise time of the mode (standard, fast, fast-mode plus); give `timing` to use a value from STM32CubeMX instead. Keep that override: observed with an earlier implementation on STM32H7 hardware, a sensor bus computed for 400 kHz and 100 kHz returned corrupt or unapplied data on real wiring, and only a ~25 kHz timing worked until the pull-ups were changed |
| Kernel clock 0 Hz | Refused by `EnsureReady()` with `NotReady` (`kNoClock`) |
| Interrupts | `I2Cn_EV` and `I2Cn_ER` at `irq_priority` (default 5) |

## Timing limits

| Wait | Bound |
| --- | --- |
| The controller lock | `I2cConfig::timeout` (default 50 ms, at most `kMaxTimeout` = 2 s); `Busy` when not had, nothing is sent |
| One transfer (a `WriteRead()`: both phases) | `I2cConfig::timeout`, from the start of the transfer, on the port's clock |
| Clock stretching | Inside the transfer's budget; a longer stretch ends as `Timeout` |
| F4 start | The HAL waits for BUSY to clear with a counted loop (about 25 ms of iterations at the core clock), then `HAL_BUSY` |
| Probe after a NACK (`nack_probe` only) | One more transfer budget |
| Restore, reset | Register writes; the reset is a de-initialisation and an initialisation of the peripheral |
| Recovery (`BusStuck`, `auto_recover`) | Nine clocks of 10 us, a STOP: about 100 us, then an initialisation |
| Length | At most 65535 bytes each way (the HAL's `uint16_t`); more is `InvalidArgument` |

The worst case of one call is: lock wait (T) + transfer (T) + probe (T, only with `nack_probe`) + reset and
recovery (well under 1 ms), plus up to 25 ms of the F4 busy-flag loop. With the default T = 50 ms: about 100 ms
(125 ms on F4), 150 ms with `nack_probe`.

## Results and failure kinds

| Result | `LastFailure()` | When |
| --- | --- | --- |
| `Ok` | `None` | The transfer completed |
| `NoDevice` | `AddressNack` | `HAL_I2C_ERROR_AF` on a `Read()` or an empty write (only the address was sent); with `nack_probe`, a write whose address probe was not acknowledged |
| `Nack` | `DataNack` | `HAL_I2C_ERROR_AF` on a write: the address or a data byte (not split without `nack_probe`); with `nack_probe`, the probe was acknowledged, so a data byte was refused |
| `Timeout` | `Timeout` | `HAL_TIMEOUT` with both lines high (clock stretching too long, a device that holds SCL) |
| `BusError` | `Bus` | `HAL_I2C_ERROR_BERR`, `HAL_I2C_ERROR_ARLO`, `HAL_BUSY` (BUSY set before the start) or another HAL error, with both lines high; a BUSY that stays set resets the peripheral |
| `BusError` | `BusStuck` | Any failure with SDA or SCL read low afterwards; with `auto_recover` (default) RecoverBus() runs at once |
| `InvalidArgument` | unchanged | Address outside 0x08 to 0x77, an empty read, a span above 65535 bytes, a timeout above 2 s, a bad configuration |
| `Busy` | unchanged | The lock was not had within the timeout, or the instance is owned by another object |
| `NotReady` | unchanged | Before `EnsureReady()` |

`LastError().Detail()` is the vendor code: the `HAL_StatusTypeDef` in bits 0 to 7 and the handle's `ErrorCode`
(`HAL_I2C_ERROR_*`) from bit 8. `Stats()` counts transfers, failures, recoveries and lock timeouts.

## Address probes

The HAL reports a NACK of the address and of a data byte with one bit. The controller sends an address-only frame
only when asked: an empty `Write()` (an explicit probe), or after a write NACK with `nack_probe` set. It is off by
default, and `EnsureReady()` never probes: observed with an SE05x secure element, the device NACKs every poll while
it is busy, and an address-only frame to its address can make it hold the bus (vendor errata). Drivers that need
the split set `nack_probe` for their own controller.

## The BUSY flag

Observed with an earlier implementation on STM32H7 hardware: after an error the I2C BUSY flag can stay set with both
lines idle, so the next transfer fails with `HAL_BUSY` until the peripheral is reset; and toggling PE after every
NACK left the master unresponsive on a busy bus, while restoring the HAL state and clearing the error flags, with a
PE toggle only while BUSY stays set, recovered it. The controller does exactly that after every failure, on every
family (the same holds for the F4's I2C v1, where the reset is SWRST). Also observed there: a latched TXIS after
`HAL_I2C_IsDeviceReady`, and stale CR2 NBYTES/AUTOEND after a transmit, made the next combined read lose its register
address; hence the flush and the CR2 clear before every transfer.

## Bus recovery

A slave that lost a clock edge (a reset in the middle of a read, a glitch) can hold SDA low forever. After any
failed transfer the controller reads SDA and SCL; when one is low the failure is `BusStuck` and, with
`auto_recover`, the recovery follows at once: the peripheral is released, the pins are taken as open-drain GPIO
(with the internal pull-ups kept when `internal_pullups` is set),
SCL is clocked until the slave lets SDA go (at most nine clocks), a STOP is made and the peripheral is initialised
again. The transfer itself is not retried: the caller decides. A device holding SCL low cannot be clocked free: the
recovery then sends no clocks and counts it in `Stats().scl_held`. If the peripheral cannot be initialised again
after a recovery, the controller is no longer ready: its calls return `NotReady` until `EnsureReady()` succeeds. A
line still low after the recovery is reported as `BusError` with `LastFailure()` `BusStuck` (a short, a missing
pull-up, a device in reset).

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

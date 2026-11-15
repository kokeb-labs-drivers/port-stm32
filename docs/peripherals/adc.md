# ADC

`kb::port::stm32::AdcChannel` is one regular channel of an ADC and satisfies `kb::iface::AdcChannel`. HAL
`stm32<family>xx_hal_adc`, single conversion, software start, ended by the end-of-conversion interrupt
([How the port waits](../waits.md)).

## How it works

| Call | HAL call |
| --- | --- |
| `EnsureReady()` | The ADC clock checked (0 Hz: `NotReady`; above the H7 limit: `Unsupported`); the pin as analog (when given); the first channel of the instance initialises it (clock, `HAL_ADC_Init`, on G4 and L4 `HAL_ADCEx_Calibration_Start` single ended, on H7 offset and linearity, the instance's interrupt at priority 5) while the others wait at most `lock_timeout`; later channels join it |
| `ReadRaw()` | The instance lock; `HAL_ADC_ConfigChannel` (the channel, rank 1, the sampling time); `HAL_ADC_Start_IT`; the task waits for `HAL_ADC_ConvCpltCallback` (which reads `HAL_ADC_GetValue`) at most `conversion_timeout`; on a timeout `HAL_ADC_Stop_IT` |
| `ReadMillivolts()` | `ReadRaw()` scaled by `vref_mv / FullScale()`, rounded to nearest |
| `Release()` | The instance's channel count drops; the last channel de-initialises it (`HAL_ADC_DeInit`), in any order of release |

The instance's channel count and phase (closed, opening, open) change only with interrupts masked, so two tasks
that start or release channels of one instance never initialise it twice or de-initialise it under the other.

## Inputs and clock

| Item | Note |
| --- | --- |
| Instance, channel | ADC1 to ADC5 where the device has them; channel 0 to 19 (`ADC_CHANNEL_<n>`), 0 to 18 on F4 |
| Pin | The pin of the channel (datasheet pinout), set analog; unused for internal channels (temperature, VREFINT) |
| Resolution | 12 bit (`FullScale()` 4095) on G4, F4, L4; 16 bit (65535) on H7 |
| Reference | `vref_mv`: the voltage on VREF+ (3300 mV default); the port does not measure VREFINT |
| ADC clock | The HAL's synchronous clock HCLK / 4 on G4, H7, L4 and PCLK2 / 4 on F4; on H7 refused above 36 MHz (HCLK above 144 MHz: select another ADC clock in the application and give the port a lower HCLK, or wait for a configurable clock) |

Sampling time per `AdcSampling` class, in ADC clock cycles:

| Class | G4, L4 | H7 | F4 |
| --- | --- | --- | --- |
| `Short` | 6.5 | 8.5 | 15 |
| `Medium` (default) | 47.5 | 64.5 | 84 |
| `Long` (high impedance, internal channels) | 640.5 | 810.5 | 480 |

## Timing limits

| Wait | Bound |
| --- | --- |
| The instance lock | `lock_timeout` (default 10 ms, at most 2 s) |
| One conversion | `conversion_timeout` (default 5 ms, at most 2 s) on the port's clock |
| Initialisation, calibration | The HAL's own bounded waits, once (the HAL time base must run: checked) |

## Results

| Result | When |
| --- | --- |
| `Ok` | One conversion |
| `Timeout` | No end of conversion within `conversion_timeout` |
| `Busy` | The instance lock was not had in time |
| `InvalidArgument` | An instance or channel the device does not have, `vref_mv` 0, a timeout above 2 s |
| `Unsupported` | H7: the ADC clock above 36 MHz |
| `Fault` | A HAL refusal (Detail: the vendor code with `HAL_ADC_ERROR_*`) |
| `NotReady` | Before `EnsureReady()`; or the ADC clock reads 0 Hz, or the HAL time base does not run |

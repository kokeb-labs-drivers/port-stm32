# ADC

`kb::port::stm32::AdcChannel` is one regular channel of an ADC and satisfies `kb::iface::AdcChannel`. HAL
`stm32<family>xx_hal_adc`, single conversion, software start, polled.

## How it works

| Call | HAL call |
| --- | --- |
| `EnsureReady()` | The pin as analog (when given); the instance's clock and `HAL_ADC_Init` the first time a channel of the instance starts (counted); on G4, H7 and L4 `HAL_ADCEx_Calibration_Start` (single ended) once |
| `ReadRaw()` | The instance lock; `HAL_ADC_ConfigChannel` (the channel, rank 1, the sampling time); `HAL_ADC_Start`; `HAL_ADC_PollForConversion` at most `conversion_timeout`; `HAL_ADC_GetValue`; `HAL_ADC_Stop` |
| `ReadMillivolts()` | `ReadRaw()` scaled by `vref_mv / FullScale()`, rounded to nearest |
| `Release()` | The count of the instance drops; `HAL_ADC_DeInit` with the last channel |

## Inputs and clock

| Item | Note |
| --- | --- |
| Instance, channel | ADC1 to ADC5 where the device has them; channel 0 to 19 (`ADC_CHANNEL_<n>`) |
| Pin | The pin of the channel (datasheet pinout), set analog; unused for internal channels (temperature, VREFINT) |
| Resolution | 12 bit (`FullScale()` 4095) on G4, F4, L4; 16 bit (65535) on H7 |
| Reference | `vref_mv`: the voltage on VREF+ (3300 mV default); the port does not measure VREFINT |
| ADC clock | The HAL's synchronous clock /4 on G4, H7, L4 and PCLK2 /4 on F4: within the datasheet limits from the reset clock tree; check them when the application raises the bus clocks |

Sampling time per `AdcSampling` class, in ADC clock cycles:

| Class | G4, L4 | H7 | F4 |
| --- | --- | --- | --- |
| `Short` | 6.5 | 8.5 | 15 |
| `Medium` (default) | 47.5 | 64.5 | 84 |
| `Long` (high impedance, internal channels) | 640.5 | 810.5 | 480 |

## Timing limits

| Wait | Bound |
| --- | --- |
| The instance lock | `lock_timeout` (default 10 ms) |
| One conversion | `conversion_timeout` (default 5 ms) as the HAL's poll timeout |
| Calibration | The HAL's own bounded wait, once |

## Results

| Result | When |
| --- | --- |
| `Ok` | One conversion |
| `Timeout` | No end of conversion within `conversion_timeout` |
| `Busy` | The instance lock was not had in time |
| `InvalidArgument` | An instance or channel the device does not have, `vref_mv` 0 |
| `Fault` | A HAL refusal (Detail: the vendor code with `HAL_ADC_ERROR_*`) |
| `NotReady` | Before `EnsureReady()` |

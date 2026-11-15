# PWM (TIM)

`kb::port::stm32::PwmChannel` is one output channel of a timer in PWM mode 1 and satisfies
`kb::iface::PwmOutput`. HAL `stm32<family>xx_hal_tim`.

## How it works

| Call | What |
| --- | --- |
| `EnsureReady()` | The timer's clock and `HAL_TIM_PWM_Init` the first time a channel of the timer starts (counted), the prescaler and auto-reload from `ComputePwmPeriod()` for `frequency_hz`; the pin as alternate function; `HAL_TIM_PWM_ConfigChannel` and `HAL_TIM_PWM_Start`; the output held at its idle level |
| `SetFrequencyHz(hz)` | New PSC and ARR, an update event (`HAL_TIM_GenerateEvent`); the duty is kept as a fraction |
| `SetDuty(duty)` | CCR = duty x (ARR + 1) / 10000, rounded to nearest |
| `Enable(on)` | `false` holds the output at `idle_high` by a compare of 0 or full; the counter keeps running |
| `Release()` | `HAL_TIM_PWM_Stop`; `HAL_TIM_PWM_DeInit` with the last channel of the timer |

`ComputePwmPeriod()` picks the smallest prescaler whose period fits the 16-bit auto-reload, so the duty has the
finest steps the timer allows (ARR + 1 steps). Advanced timers (TIM1, TIM8, TIM20) get their main output enabled by
the HAL. One `PwmChannel` per timer channel: the port counts the channels of a timer but does not refuse a second
object on the same channel.

Timer ownership is enforced: the timer of a running `Clock` (TIM2 or TIM5) is refused (`Busy`, port code
`kOwned`), since a PWM channel there would rewrite its prescaler and stop the clock at release. The channels of one
timer share its prescaler and auto-reload, so they must run at one frequency: a channel at another frequency than
the timer's other channels is refused (`InvalidArgument`, port code `kConflict`), and `SetFrequencyHz()` on a
channel whose timer has other channels open is refused the same way.

## Pins and clock

| Item | Note |
| --- | --- |
| Timer, channel | TIM1 to TIM20 where the device has them; channel 1 to 4 |
| Pin | The channel's pin with its alternate function (AF1 or AF2 for TIM1 to TIM5 on most pins) |
| Timer clock | `timer_clock_hz`, or 0: the APB clock of the timer, doubled when the APB prescaler is not 1 |
| Frequency | 1 Hz (at timer clocks up to 4.29 GHz) to timer clock / 2 |
| Resolution | ARR + 1 steps: 65536 at low frequencies, timer clock / frequency at high ones (80 steps at 2 MHz from 160 MHz) |

## Timing limits

Every call is a few register writes; nothing waits. A frequency change takes effect at the update event it
generates (the counter restarts).

## Results

| Result | When |
| --- | --- |
| `Ok` | Done |
| `InvalidArgument` | Frequency 0 or above timer clock / 2, a timer, channel or pin the device does not have; another frequency on a shared timer (`kConflict`) |
| `Busy` | The timer is the clock's (`kOwned`) |
| `NotReady` | Before `EnsureReady()`; or the timer clock reads 0 Hz |

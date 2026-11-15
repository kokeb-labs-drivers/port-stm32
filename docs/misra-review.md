# MISRA review of "to review" findings

The MISRA scan (`ci-misra.yml`) marks a finding "to review" when its guideline's CodeQL query was found, in the
tool validation of ci-tools, to report compliant code (the anomaly ids, `AN-*`, are in the scan's
`misra-findings.csv`). They do not fail the level; each needs the reviewer's check (review checklist R-FP): a real
one is fixed, a false one needs no record beyond the anomaly.

**This page reviews the CI scan of `v0.1.0-rc.1`, not the current code.** No scan of the current code has been run
yet; the CI MISRA job of the pull request produces it. Its "to review" list must be checked against the table below
before the next release candidate: a finding whose pattern is here gets the verdict here, a new pattern is reviewed
and added, and the scanned commit is written into this page.

## Scope

- Scan reviewed: the CI scan of `v0.1.0-rc.1` (host and Cortex-M configurations, 25 source files each): 303 findings
  to review in 14 guidelines, 19 advisory violations (Rule 6.5.1 x 18, Rule 10.1.1 x 1), 10 deviated (Rule 6.7.1,
  DEV-001).
- Since that scan the port changed substantially (interrupt-driven waits, the shared state out of the boundary
  files, `src/can_time_log.cpp` split into `can.cpp` and `time.cpp`): many of the reviewed lines no longer exist. The
  verdicts below are per pattern; the scan of the next release re-lists the findings, and a finding whose pattern
  is not in the table below is reviewed and added here before that release.

## Verdicts

| Guideline | Findings | Pattern found at the sites | Verdict |
| --- | --- | --- | --- |
| Rule 0.0.2 (invariant condition), advisory | 1 | Inside the expansion of `__HAL_TIM_SET_AUTORELOAD` (a HAL macro's constant sub-expression) | False positive (anomaly): the condition belongs to the SOUP macro, not to the port |
| Rule 0.1.1 (unnecessary write), advisory | 28 | The single-exit style: a local initialised to a safe default, then assigned on every branch (`uint32_t code = port_code::kOwned; if (...) { code = ...; }`) | False positive (anomaly): the initial value is the defined result on the path that does not assign; kept on purpose (no uninitialised local, KB-CPP20 single exit) |
| Rule 0.2.4 (unused limited-visibility function), advisory | 9 | The `fdcan::`/`bxcan::` helpers of the CAN boundary: one family's set is compiled per variant and reached through the `family` alias | False positive (anomaly): each helper is called in its own family's build |
| Rule 4.6.1 (unsequenced use and modification), required | 4 | `ok ? Compute(..., ok) : value`: the condition is sequenced before the chosen operand, so the code was well-defined | False positive, **fixed anyway**: the three sites in `adc.cpp`, `pwm.cpp` and `spi.cpp` are now `if` statements; the CAN site is gone |
| Rule 5.13.4 (unsigned literal suffix), required | 36 | Empty brace initialisers of `PinId` members (`PinId tx{};`) and similar: the query reports the `0` (an octal literal of type `int`) of an enumerator or default member initialiser behind them | False positive (anomaly): no unsigned literal without `U` is written; `0` is signed |
| Rule 6.8.4 (ref-qualified member functions), advisory | 5 | Accessors that return a member by const reference (`Config()`, `Timing()`) | Reviewed, kept (advisory): the objects are non-copyable and non-movable and live for the program (drivers hold references), so a dangling reference from a temporary cannot arise in use |
| Rule 7.0.1 (conversion from bool), required | 17 | A `bool` bound to a `bool&` out-parameter (`ComputeCanTiming(..., ok)`), and `static_cast<void>` of a `bool` | False positive (anomaly): a reference binding and a discard are not conversions of the value |
| Rule 7.0.2 (implicit conversion to bool), required | 24 | A `bool&` read as `bool`; the HAL's `FlagStatus` compared with `RESET` | False positive (anomaly): no value conversion happens |
| Rule 7.0.5 (signedness change by promotion), required | 28 | Comparisons of `uint8_t` values with `uint8_t` constants (`address >= kI2cFirstAddress`, `config_.mode <= kMaxMode`), `uint8_t` shift counts (`1U << pin.pin`), and HAL macro expansions in the boundary files | False positive (anomaly): both operands are promoted alike and non-negative below 2^16, so no value or comparison result changes; the shift counts were re-checked, every one is below 32 |
| Rule 7.0.6 (numeric assignment type mismatch), required | 64 | Mostly HAL constants assigned to HAL fields in the boundary and vector files (`I2C_MEMADD_SIZE_16BIT`, `IRQn_Type` values, `__HAL_RCC_*` expansions); in the logic, a `uint8_t` member assigned a constant that fits (`mode_ = ok ? mode : 0xFFU`) and a returned `kb::Status` | False positive (anomaly): every value fits its destination; the HAL's types are fixed by the SOUP |
| Rule 8.2.2 (C-style casts), required | 11 | Brace-initialised temporaries (`uint64_t{raw}`, `uint32_t{1U}`) used to widen before arithmetic | False positive (anomaly): list initialisation is not a functional-notation cast and cannot narrow; the port has no C-style cast (`-Wold-style-cast -Werror` on every platform-free file) |
| Rule 8.14.1 (short-circuited side effect), advisory | 12 | The right operand reads a register or pops a ring (`PinRead(sda) && PinRead(scl)`, `n < size && ring.TryPop(b)`, the elapsed-time checks of the old polling loops) | Real (advisory), **fixed**: both pins are read before the test, the ring drain and the recovery clock loop no longer short-circuit a side effect, `ItmEnabled` reads both registers first; the polling loops are gone |
| Rule 8.20.1 (unsigned wrap with constant operands), advisory | 56 | Statistics counters (`++stats.transfers`), the DWT elapsed-cycle difference `now - last`, rounding sums in 64 bits (`raw * vref + full / 2`, `total + span - 1`), and `x - 1U` after `x >= 1` is established (`prescaler - 1U`, `quanta - sample`) | Reviewed: counters and cycle differences wrap by design (modular arithmetic is the intent); every rounding sum and product is in 64 bits over operands below 2^32 and cannot wrap; every subtraction is guarded by its range check |
| Rule 9.5.1 (simple legacy `for`), advisory | 8 | Searches that stop early (`for (...; (n < max) && !ok; ++n)`) and loops over a counter with a second condition | Reviewed, kept (advisory): each loop has a fixed upper bound and a counter it alone changes; the recovery loop was rewritten as a simple loop |

## Advisory violations left

| Guideline | Where | Why it stays |
| --- | --- | --- |
| Rule 6.5.1 | The interrupt vectors and the HAL callbacks | Their names are fixed by the startup file and the HAL headers ([MISRA boundary and deviations](deviations.md)) |
| Rule 10.1.1 | The HAL callbacks' handle parameters | The signatures are the HAL's |

Reviewer: the maintainer of record (`.github/CODEOWNERS`), 2026-11-14, against the CI scan of `v0.1.0-rc.1`; the
fixes named above were checked at their sites in the current code. Scan of the current code: pending (the pull
request's CI MISRA job), to be reviewed against this table before the next release candidate.

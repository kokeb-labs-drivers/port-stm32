## Summary

<!-- What changes, in one or two sentences. -->

## Area

- [ ] I2C  - [ ] SPI  - [ ] UART  - [ ] GPIO  - [ ] ADC  - [ ] PWM  - [ ] CAN  - [ ] clock, log  - [ ] build, CI or docs

## How verified

- [ ] `tools/preflight.sh` passed before pushing; Arm builds of the touched families
- [ ] CI green, including `CI gate / gate`
- [ ] Boundary files and deviation records unchanged, or each new HAL finding has its record

## Bench status

- [ ] **Verified on hardware**: <!-- board, family, what was run, what was observed -->
- [ ] **Not verified on hardware**. Bench steps: <!-- steps -->

## Checklist

- [ ] PR title is an imperative commit subject (it becomes the squash commit)
- [ ] `CHANGELOG.md` updated
- [ ] Commits signed off (`git commit -s`)

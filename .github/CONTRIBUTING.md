# Contributing

Thank you for helping. This repository follows the
[Kokeb Labs engineering standard](https://github.com/kokeb-labs/.github/blob/main/standards/README.md).

1. Open an issue first for anything larger than a fix.
2. Branch from `main`, keep one topic per pull request, and write tests with the change: a new HAL call gets a fake
   in `tests/fakes/stm32` and a test of its success and of every error path.
3. Run `tools/preflight.sh` before pushing, and the Arm build of the examples for a family you touched.
4. Sign off every commit (`git commit -s`, Developer Certificate of Origin); CI checks it.
5. Update `CHANGELOG.md` and the documentation page of the peripheral.

A new header `x.hpp` comes with `src/x.cpp` and `src/boundary/x.cpp` (`tools/check_layout.py`). STM32Cube HAL and
CMSIS headers belong only in the boundary files listed in `deviations/boundary-files.yml`; a new MISRA finding
there gets a record in `deviations/deviations.yml`. Questions: <kokeblabs@gmail.com>.

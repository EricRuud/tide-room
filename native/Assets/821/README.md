# Retained 821 calibration assets

These numeric coefficients support the 821 tape option in Tide Room, alongside Worn tape. Both calibrations require 48 kHz. Their settings and model selection are saved with scenes, using the existing parameter IDs.

- `model`: LF2 research calibration for the 456 / 15 ips mode.
- `90030`: selected 900 / 30 ips calibration.
- `hq`: oversampling/limiter filters and compensation.

`manifest.json` records each file's SHA-256. CMake copies these folders to the app's `Contents/Resources/821` directory. The corresponding implementation and generation scripts are in `native/Room/EightTwentyOneTape.cpp` and `native/Lab`, including `native/Lab/LF2`.

These are generated coefficients, not third-party plugin binaries or recorded source audio. The research scripts may require locally captured measurement data that is not included in this repository.

# Airwindows comparison reference

Chris Johnson / Airwindows, MIT license. ToTape9.h, ToTape9.cpp and ToTape9Proc.cpp are unmodified upstream files from commit `974a26ea5a96a3cd57c3f8f8d95745bb765a5a72` (2026-04-01), https://github.com/airwindows/airwindows/tree/974a26ea5a96a3cd57c3f8f8d95745bb765a5a72/plugins/WinVST/ToTape9 . LICENSE is retained verbatim.

The original `audioeffectx.h` shim here supplies only sample rate and unused metadata methods to compile the original DSP without a VST2 SDK or plugin host. This is an offline reference executable, not an Airwindows plugin build or an endorsement. The harness uses the upstream double-precision processing path with a deterministic C random seed, and records parameter settings. No signal-processing equations in the reference are changed.

The separate hybrid is an original experiment inspired by the dynamic brightness idea in TapeHack2. Its one-pole filter, smooth rational detector and linked stereo control differ from Chris's implementation. Its magnetic section is a GPL-3.0 adaptation of CHOW Tape, as in Tide Room 0.3.

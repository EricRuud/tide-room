# CHOW Tape Model attribution

Jatin Chowdhury, CHOW Tape Model / AnalogTapeModel, GPL-3.0.
https://github.com/jatinchowdhury18/AnalogTapeModel
Pinned commit: 604372e4ffd9690c3e283362e4598cb43edbb475 (2023-11-05).

The three original hysteresis source files in this directory are retained for attribution and comparison; they are not compiled directly. `native/Room/TapeMagnet.h` adapts their Jiles–Atherton equations and parameter mapping into a scalar double-precision processor. Tide's adaptation uses a continuous near-zero Langevin series, quadratic intersample-extremum estimation and RK4 integration in field space with bounded substeps, finite-state guards, and a separate oversampling/bypass wrapper. The original GUI, neural weights, presets and other plugin processors are not used.

Modifications in TapeMagnet.h and the accompanying Tape stage are made in 2026 for Tide Room and are provided under GPL-3.0. See LICENSE for the complete terms. This is a modified implementation, not the CHOW Tape plugin or a claim of endorsement by its author.

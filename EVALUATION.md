# A build–render–evaluate loop for the synth

Status: Tide now has a playable native standalone, VST3 and AU, alongside the original Python lab. `python3 native/evaluate.py` builds and preserves one native iteration, runs engineering checks, compares the oscillator with an independent Fourier reference, loads the VST3 in a host and renders presets. See the [native validation report](artifacts/native-final-001/report.md) and [development guide](native/DEVELOPMENT.md). Engineering tolerances remain provisional; listening calibrates the musical preferences. Full-instrument dynamic alias validation and physical Buchla circuit modelling remain future work.

## Purpose

Let an agent change DSP code or parameters, render a repeatable set of audio examples, evaluate the results against a fixed specification, and retain useful candidates. The user defines the desired behavior and calibrates the musical preferences. The evaluator provides evidence about those criteria; passing its tests is not proof of universal sound quality or analog realism.

## Three kinds of criteria

| User criterion | Automated evidence | What still needs calibration |
| --- | --- | --- |
| No aliasing | Controlled stationary tests for unwanted folded components; comparisons with an independently checked, much higher sample-rate reference for more complex signals. Report worst case and the exact tested operating range. | An agreed residual ceiling, measurement bandwidth, supported notes, drive/modulation limits, and quality modes. Literal zero over every possible input is not a conclusion a finite test suite can establish. |
| Smooth dynamics | Output envelope and transfer behavior during level changes, note events, and control automation; unexpected transient energy; attack/release timing; stability and recovery. | Examples of desirable attacks, compression, and release. A sharp intentional attack must not be treated as an implementation click. |
| Harmonics that move and feel alive | Time-varying harmonic ratios, modulation rates/depths, repeatability and variation across notes, and dependence on velocity, envelope, and recent signal history. | Preferred ranges and examples. Motion is a measurable property; whether that motion is musically compelling is a preference to validate. |

## What happens in one iteration

1. Read the fixed criteria and the previous candidate's report.
2. Make a bounded code or parameter change with a stated hypothesis.
3. Build the DSP and render the same diagnostic cases and musical gestures.
4. Reject candidates that violate any mandatory criterion. Preserve the previous passing version.
5. Compare the surviving candidates on musical descriptors and any preference model that has passed validation. Keep alternatives when they represent different useful tradeoffs.
6. Save source identity, configuration, random seeds, sample rate, render settings, raw measurements, plots, and audio together.
7. Produce a small, level-matched blind listening shortlist when the criteria need calibration or a new kind of sound is being explored.

Completion means that the agreed tests pass and any explicitly required listening review is complete. If a criterion remains unresolved, the report should say so. Budget exhaustion is not success.

## Aliasing evaluation

Start with isolated blocks such as the oscillator and waveshaper. Use several pitches, including high notes and frequencies whose aliased products do not simply coincide with the desired harmonics. Test light and strong drive. Use coherent sampling or a leakage-controlled spectral analysis.

For stationary cases with known harmonic structure, inspect unwanted components and changes to intended components. A count of energy outside harmonic bins alone can miss aliases that land on a valid harmonic.

For modulation, transients, and feedback, compare with a higher-rate reference that follows the same continuous control trajectories and random processes. Check that increasing the reference rate again changes the reference by substantially less than the proposed tolerance. If it does not converge, mark the test inconclusive. An analytical or independently implemented reference should be used where practical.

Account for documented latency and sample-rate conversion before comparing waveforms. Do not fit away arbitrary timing, gain, or frequency-response errors. The remaining difference includes numerical/modeling and filtering errors as well as aliasing, so label it **reference residual**, not pure alias energy. Report both relative residual and absolute level, including the bandwidth used.

Legitimate FM and AM sidebands must not be automatically classified as aliases. A model that produces a duller sound can reduce alias energy while failing the intended timbre, so preserve desired harmonic content in a separate check.

## Dynamics evaluation

Use a small set of specified gestures: slow and fast level ramps, isolated notes, retriggers during release, control changes during a held note, and recovery after strong drive. Later test repeated gestures at different block sizes and sample rates using the same event timestamps.

Evaluate the envelope and localized residual around events. Adjacent waveform-sample differences by themselves are unsuitable as a click test: a legitimate high-frequency waveform has large differences. Separate intended attack behavior from unexpected discontinuities and automation artifacts.

Retain raw output levels for engineering tests. Any gain adjustment for listening comparisons must be a documented constant gain, not compression or per-frame normalization that conceals dynamics.

## Harmonic-motion evaluation

Begin with held notes at fixed pitch so changes in timbre can be separated from vibrato, tremolo, and the note envelope. Track harmonic amplitudes relative to the fundamental or total harmonic energy, while excluding silence and components too weak to measure reliably. Report which harmonics are actually resolved at each pitch.

Then introduce controlled gestures and ask whether the spectral response changes appropriately with velocity, drive, attack/release, and recent history. Record pitch drift and amplitude modulation separately. Normalize descriptors for analysis only; retain the original waveform and raw dynamics measurements.

Use target ranges rather than maximizing motion, irregularity, entropy, or spectral flux. A changing spectrum is not evidence of a physically realistic analog mechanism. Physical claims require circuit behavior or hardware comparisons of their own.

## Validate the evaluator before trusting its rankings

The first proof of concept should deliberately include:

- A clean stationary tone: a control that can pass technical tests while having little harmonic motion.
- A deliberately aliased version of a nonlinear tone.
- A version with deliberately stepped controls.
- A smoothly evolving version.
- A version with excessive pitch wobble or noise.
- A quieter copy of the same sound and a silent output, to test whether level changes can exploit the metrics.

These establish expected technical distinctions. The smooth version is not automatically the musical winner. That judgment should be checked with the user.

Keep an evaluator version and a separate validation set with new pitches, gestures, and seeds. Do not change the criteria or the evaluator merely to make a candidate pass. If a metric is wrong, correct it explicitly and rerun the baseline and candidates under the new evaluator version. Once validation cases are repeatedly used to tune changes, add fresh cases rather than continuing to call them unseen.

## Learning the user's preferences

Present a manageable number of blind, level-matched comparisons with a tie/uncertain option. Ask focused questions such as which example has more desirable movement or a more convincing release, not only which is better overall. Include occasional repeated pairs to understand consistency.

A learned preference model is optional. Keep it advisory until it predicts held-out user choices usefully. Track disagreement and uncertainty, and send uncertain or novel cases back for listening. Initial calibration should also establish whether the measured descriptors track the user's intended qualities at all.

## Proposed first implementation

A small offline renderer and evaluator around one nonlinear voice would make the loop testable before building a full VST interface. Its first deliverables would be:

- An editable criteria file that distinguishes required limits, descriptive measurements, and unvalidated preferences.
- A repeatable command that renders and evaluates candidates.
- A report with per-case results, worst cases, plots, and audio examples.
- The deliberate controls above, demonstrating what the evaluator can and cannot distinguish.

This first version would establish the workflow. It would not be a Buchla emulation, a production realtime benchmark, or a validated model of human taste. Once a production DSP core exists, the offline renderer and plugin should share that core so the tests exercise the code that actually ships.

## Research informing the design

- Holters, *Antiderivative Antialiasing for Stateful Systems* (DAFx 2019): https://dafx.de/paper-archive/details/lp5GcJOo79OBkfcMfN3N_g
- *Efficient Anti-aliasing of a Complex Polygonal Oscillator* (DAFx 2017): https://www.dafx.de/paper-archive/2017/papers/DAFx17_paper_100.pdf
- Moffat and Reiss, *Objective Evaluations of Synthesised Environmental Sounds* (DAFx 2018): https://www.dafx.de/paper-archive/details/7RJC1dIa-fP4C36RwNRxGw
- *Sound Matching Using Synthesizer Ensembles* (DAFx 2024), including the discussion of envelope and modulation errors missed by a spectral similarity metric: https://www.dafx.de/paper-archive/2024/papers/DAFx24_paper_76.pdf

The test choices above are a proposed engineering design, not a claim that these papers validate this particular evaluator or its future thresholds.

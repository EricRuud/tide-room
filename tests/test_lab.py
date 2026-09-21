import json
import tempfile
import unittest
from pathlib import Path

import numpy as np
import soundfile as sf

import lab
import voice


class EvaluatorTests(unittest.TestCase):
    def test_previous_run_is_preserved(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            marker = path / "report.json"
            marker.write_text("previous measurements")
            with self.assertRaises(FileExistsError):
                lab.run(lab.ROOT / "criteria.json", path)
            self.assertEqual(marker.read_text(), "previous measurements")

    def test_analytic_reference_matches_low_frequency_direct_evaluation(self):
        # Unaliased low-frequency case validates the independent Bessel
        # expansion, including the even-harmonic signs and DC treatment.
        n, sr, frequency = 8192, 48000, 48000 * 11 / 8192
        direct = voice.stationary(n, sr, 1, frequency, 5.9)
        expected = lab.analytic_reference(n, sr, frequency, 5.9)
        self.assertLess(lab.residual(direct, expected)["relative_db"], -200)

    def test_unseen_high_pitch_exposes_aliasing_and_oversampling_reduces_it(self):
        n, sr = 8192, 44100
        frequency, drive = 991 * sr / n, 5.9
        ref = lab.analytic_reference(n, sr, frequency, drive)
        naive = lab.residual(voice.stationary(n, sr, 1, frequency, drive), ref, sr, .4 * sr)
        improved = lab.residual(voice.stationary(n, sr, 8, frequency, drive), ref, sr, .4 * sr)
        self.assertGreater(naive["relative_db"], -50)
        self.assertLess(improved["relative_db"], -90)

    def test_residual_does_not_fit_away_a_gain_error(self):
        x = np.sin(2 * np.pi * np.arange(4096) / 64)
        self.assertGreater(lab.residual(.1 * x, x)["relative_db"], -2)
        self.assertAlmostEqual(lab.band_rms(x, 48000, 24000), lab.rms(x), places=12)
        with self.assertRaises(ValueError):
            lab.residual(x, x * 0)
        with self.assertRaises(ValueError):
            lab.residual(x * np.nan, x)

    def test_movement_is_gain_invariant_and_silence_is_not_valid(self):
        y = voice.held(96000, 48000, 4, .7, 719)
        a = lab.harmonic_motion(y, 48000)
        b = lab.harmonic_motion(y * .2, 48000)
        stationary = voice.held(96000, 48000, 4, 0, 719)
        self.assertAlmostEqual(a["dispersion"], b["dispersion"], places=10)
        self.assertGreater(a["dispersion"], .01)
        self.assertLess(lab.harmonic_motion(stationary, 48000)["dispersion"], 1e-8)
        self.assertFalse(lab.harmonic_motion(np.zeros(96000), 48000)["valid"])

    def test_step_automation_fails_the_smooth_reference(self):
        ref = voice.held(48000, 48000, 16, .7, 719)
        smooth = voice.held(48000, 48000, 4, .7, 719)
        stepped = voice.held(48000, 48000, 4, .7, 719, True)
        self.assertLess(lab.residual(smooth, ref)["relative_db"], -80)
        self.assertGreater(lab.residual(stepped, ref)["relative_db"], -50)

    def test_reference_inventory_does_not_cancel_antiphase_stereo(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            x = .2 * np.sin(2 * np.pi * 440 * np.arange(4800) / 48000)
            sf.write(path / "stereo.wav", np.column_stack((x, -x)), 48000, subtype="FLOAT")
            lab.inventory(path, path / "inventory.json")
            item = json.loads((path / "inventory.json").read_text())["files"][0]
            self.assertGreater(item["rms_dbfs"], -20)
            self.assertEqual(item["channels"], 2)
            self.assertGreater(max(item["envelope_10ms_rms"]), .1)


if __name__ == "__main__":
    unittest.main()

import unittest

import numpy as np

import space


class SpaceTests(unittest.TestCase):
    def test_fft_processing_matches_independent_direct_convolution(self):
        rng = np.random.default_rng(1209)
        x = rng.normal(size=137)
        h = rng.normal(size=(53, 2))
        expected = .38 * np.column_stack([np.convolve(x, h[:, ch]) for ch in range(2)])
        expected[:len(x)] += .71 * x[:, None]
        actual = space.process(x, h, .38, .71)
        np.testing.assert_allclose(actual, expected, rtol=1e-12, atol=1e-12)

    def test_silence_stays_silent_and_tail_is_not_discarded(self):
        h = np.zeros((101, 2))
        h[-1] = [1, -.5]
        self.assertFalse(np.any(space.process(np.zeros(31), h, 1)))
        x = np.zeros(31)
        x[-1] = .25
        actual = space.process(x, h, 1, 0)
        self.assertEqual(actual.shape, (131, 2))
        np.testing.assert_allclose(actual[-1], [.25, -.125], atol=1e-14)

    def test_no_wrapping_before_causal_reflections(self):
        for sr in (44100, 48000):
            for preset in space.PRESETS:
                h = space.impulse_response(sr, preset)
                delay = round(preset.predelay * sr)
                self.assertFalse(np.any(h[:delay]))
                self.assertTrue(np.isfinite(h).all())
                # An impulse must reproduce the filter, not circularly wrap it.
                actual = space.process(np.array([1., 0., 0.]), h, .4, 0)
                np.testing.assert_allclose(actual[:len(h)], .4 * h, atol=1e-14)
                np.testing.assert_allclose(actual[len(h):], 0, atol=1e-14)

    def test_overdrive_remains_linear_without_clipping_or_compression(self):
        h = space.impulse_response(44100, space.PRESETS[0])
        x = np.random.default_rng(35).normal(size=4000)
        y = np.random.default_rng(71).normal(size=4000)
        combined = space.process(3 * x - .2 * y, h, .6)
        separate = 3 * space.process(x, h, .6) - .2 * space.process(y, h, .6)
        np.testing.assert_allclose(combined, separate, atol=1e-12)
        self.assertGreater(np.max(np.abs(combined)), 1)

    def test_invalid_audio_and_sample_rate_are_rejected(self):
        with self.assertRaises(ValueError):
            space.impulse_response(22050, space.PRESETS[0])
        with self.assertRaises(ValueError):
            space.process(np.array([np.nan]), np.ones((10, 2)), 1)
        with self.assertRaises(ValueError):
            space.process(np.zeros((10, 2)), np.ones((10, 2)), 1)


if __name__ == "__main__":
    unittest.main()

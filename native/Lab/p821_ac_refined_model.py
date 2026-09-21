"""DC-rejecting fast descriptors on the v27 checkpoint family, no new training.

The 5 Hz/two-pole choice comes from prior DC-pilot diagnostics. Selection still
evaluates every completed v27 checkpoint with the unchanged weighted criterion.
"""
import p821_refined_distillation_model as previous
from p821_detector_placement import change

Model=previous.Model
core=previous.core


def prepare_source(source):return change(previous.prepare_source(source),5.,2)


def activate():
    previous.activate();core.VERSION='v27ac';core.prepare_source=prepare_source

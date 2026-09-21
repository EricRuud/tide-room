"""Generate original synth constants and copy the two approved synthetic spaces."""
from pathlib import Path
import hashlib
import json
import sys
import numpy as np
from scipy import signal, special
import soundfile as sf

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
import space
dest = ROOT / "native/generated"
dest.mkdir(parents=True, exist_ok=True)
h = signal.firwin(384 * 16 + 1, .90 / 16, window=("kaiser", 14))
x = np.linspace(0, 10, 2049)
with (dest / "Constants.h").open("w") as f:
    f.write("#pragma once\n#include <array>\nnamespace tide::constants {\n")
    for name, values in (("decimator", h), ("j0", special.j0(x))):
        f.write(f"inline constexpr std::array<float, {len(values)}> {name} = {{\n")
        for start in range(0, len(values), 8):
            f.write(", ".join(f"{v:.10e}f" for v in values[start:start+8]) + ",\n")
        f.write("};\n")
    f.write("}\n")
for preset in space.PRESETS[:2]:
    sf.write(dest / f"{preset.name}.wav", space.impulse_response(48000, preset, 208), 48000, subtype="FLOAT")
(dest / "provenance.json").write_text(json.dumps({
    "origin": "Original synthetic spaces from space.py, preferred as B/close and C/bloom. No commercial reference audio included.",
    "files": {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in dest.iterdir() if p.name != "provenance.json"}
}, indent=2) + "\n")
print(dest)

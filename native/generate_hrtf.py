"""Convert MIT's diffuse-field-equalized KEMAR WAVs to a compact source bank.

Bill Gardner and Keith Martin, MIT Media Laboratory, 1994.
https://sound.media.mit.edu/resources/KEMAR.html
The original archive is retained as a build input; no filter is independently
level-normalized, so interaural and directional level differences are preserved.
"""
from io import BytesIO
from pathlib import Path
import hashlib
import re
import struct
import zipfile
import numpy as np
from scipy.io import wavfile

root = Path(__file__).resolve().parent.parent
archive = root / "artifacts/room-002/mit-kemar-diffuse.zip"
rows = []
with zipfile.ZipFile(archive) as z:
    for name in z.namelist():
        match = re.search(r"H(-?\d+)e(\d+)a\.wav$", name)
        if not match:
            continue
        rate, audio = wavfile.read(BytesIO(z.read(name)))
        assert rate == 44100 and audio.shape == (128, 2) and audio.dtype == np.int16
        rows.append((int(match[1]), int(match[2]), audio.astype(np.float32).T / 32768))
rows.sort(key=lambda row: row[:2])
destination = root / "native/generated/kemar.bin"
with destination.open("wb") as f:
    f.write(struct.pack("<IIII", 0x48525446, len(rows), 128, 44100))
    for elevation, azimuth, audio in rows:
        f.write(struct.pack("<ff", elevation, azimuth))
        f.write(audio.astype("<f4").tobytes())
notice = root / "native/generated/MIT-KEMAR-NOTICE.txt"
notice.write_text(
    "MIT KEMAR head-related transfer function measurements\n"
    "Copyright 1994 MIT Media Laboratory.\n"
    "Authors: Bill Gardner and Keith Martin.\n"
    "Source: https://sound.media.mit.edu/resources/KEMAR.html\n"
    "Data: https://sound.media.mit.edu/resources/KEMAR/diffuse.zip\n\n"
    "MIT permits unrestricted use, including commercial use, provided the authors are cited.\n"
    "Tide Room uses the authors' diffuse-field-equalized measurements, converts their WAV files\n"
    "to floating-point coefficients, resamples at device initialization, and interpolates\n"
    "between measured directions. This is a generic mannequin, not an individual listener model.\n\n"
    f"Original archive SHA-256: {hashlib.sha256(archive.read_bytes()).hexdigest()}\n"
    f"Generated bank SHA-256: {hashlib.sha256(destination.read_bytes()).hexdigest()}\n",
    encoding="utf-8",
)
print(f"Wrote {len(rows)} directions to {destination}")

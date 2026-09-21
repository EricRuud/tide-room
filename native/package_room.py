"""Package the tested standalone room app without third-party plugin binaries."""
import hashlib
import json
import plistlib
import shutil
import subprocess
from pathlib import Path

root = Path(__file__).resolve().parent.parent
release = root / "dist/Tide-Room-0.3.0-mac-arm64"
archive = release.parent / (release.name + ".zip")
if release.exists() or archive.exists():
    raise SystemExit("Release already exists; use a new version for another release.")
app = root / "build-room3/TideRoom_artefacts/Release/Standalone/Tide Room.app"
with (app / "Contents/Info.plist").open("rb") as stream:
    assert plistlib.load(stream)["CFBundleShortVersionString"] == "0.3.0"
release.mkdir()
shutil.copytree(app, release / app.name, symlinks=True)
subprocess.run(["codesign", "--force", "--deep", "--sign", "-", str(release / app.name)], check=True)
subprocess.run(["codesign", "--verify", "--deep", "--strict", str(release / app.name)], check=True)
shutil.copy2(root / "native/ROOM-QUICKSTART.md", release / "QUICKSTART.md")
shutil.copy2(root / "artifacts/room-003/report.md", release / "TEST-REPORT.md")
shutil.copytree(root / "dist/Tide-FX-0.3.0-mac-arm64/Third-party notices", release / "Third-party notices")
shutil.copy2(root / "native/generated/MIT-KEMAR-NOTICE.txt", release / "Third-party notices/MIT-KEMAR-NOTICE.txt")
shutil.copy2(root / "native/third_party/chowtape/LICENSE", release / "Third-party notices/CHOW-Tape-GPL-3.0.txt")
shutil.copy2(root / "native/third_party/chowtape/NOTICE.md", release / "Third-party notices/CHOW-Tape-NOTICE.md")
shutil.copy2(root / "artifacts/room-003/research.md", release / "DSP-RESEARCH.md")
source = release / "Source"
source.mkdir()
shutil.copytree(root / "native", source / "native", ignore=shutil.ignore_patterns("__pycache__", ".DS_Store"))
for name in ("CMakeLists.txt", "README.md", "requirements.txt", "lab.py", "voice.py", "space.py", "space_round.py", "criteria.json"):
    if (root / name).exists():
        shutil.copy2(root / name, source / name)
shutil.copytree(root / "tests", source / "tests", ignore=shutil.ignore_patterns("__pycache__"))
measurements = source / "artifacts/room-002"
measurements.mkdir(parents=True)
shutil.copy2(root / "artifacts/room-002/mit-kemar-diffuse.zip", measurements / "mit-kemar-diffuse.zip")
listen = release / "Listen"
listen.mkdir()
for name in ("new-voices.wav", "tape-comparison.wav", "listening-guide.json"):
    shutil.copy2(root / "artifacts/room-003/listen" / name, listen / name)
shutil.copy2(root / "artifacts/room-003/scene-optimized/ring-room-tape.wav", listen / "ring-room-tape.wav")
shutil.copy2(root / "artifacts/room-003/tape-optimized/room.png", release / "Room.png")
verification = release / "Verification"
verification.mkdir()
for name in ("source-hashes.json", "ring-spectral-report.json", "tape-spectral-report.json", "tape-optimized.log", "voices-2.log"):
    shutil.copy2(root / "artifacts/room-003" / name, verification / name)
(release / "README.txt").write_text(
    "Tide Room 0.3\n\nOpen Tide Room.app, put on headphones, and press Play.\n"
    "Use 48 kHz / 1024 samples with Tape on. The app loads your installed Reverside VST3.\n"
    "Tape is a global magnetic saturation stage after the complete room mix.\n"
    "Tape controls... opens Drive, Saturation, Bias, Warmth and Mix.\n"
    "Eight new sounds follow Hollow marimba in each instrument dropdown.\n"
    "Quit an earlier Tide Room normally before opening this version; existing scene settings carry over, with Tape off.\n\n"
    "Listen/new-voices.wav plays the eight new voices, level-matched.\n"
    "Listen/tape-comparison.wav plays Original, Tape at 6 dB drive, then Tape at 12 dB drive.\n"
    "Each segment is 14 seconds with a one-second gap; the identical source audio is used and levels are matched.\n"
    "Listen/ring-room-tape.wav is a 34-second generative room scene with the new voices and Tape.\n"
    "Read QUICKSTART.md for controls and the advanced Reverside state-recall limitation.\n"
    "TEST-REPORT.md records verification; DSP-RESEARCH.md explains the research and implementation decisions.\n\n"
    "Source/ includes the app project and build instructions. JUCE is pinned in native/build.sh.\n"
    "Third-party notices/ includes CHOW Tape's GPL-3.0 license and modification notice, plus MIT KEMAR attribution.\n"
    "Reverside itself, its presets and its authorization data are not included.\n",
    encoding="utf-8",
)
files = {}
for path in sorted(release.rglob("*")):
    if path.is_file():
        files[str(path.relative_to(release))] = hashlib.sha256(path.read_bytes()).hexdigest()
(release / "manifest.json").write_text(json.dumps({"product": "Tide Room", "version": "0.3.0", "architecture": "arm64", "files": files}, indent=2) + "\n")
subprocess.run(["ditto", "-c", "-k", "--sequesterRsrc", "--keepParent", str(release), str(archive)], check=True)
print(json.dumps({"app": str(release / app.name), "archive": str(archive), "sha256": hashlib.sha256(archive.read_bytes()).hexdigest(), "files": len(files)}, indent=2))

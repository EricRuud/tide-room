"""Package the tested 0.5 position LFO update without user preferences/vendor state."""
from pathlib import Path
import hashlib, json, plistlib, shutil, subprocess

root = Path(__file__).resolve().parent.parent
previous = root / 'dist/Tide-Room-0.4.1-mac-arm64'
release = root / 'dist/Tide-Room-0.5.0-mac-arm64'
archive = release.parent / (release.name + '.zip')
if release.exists() or archive.exists():
    raise SystemExit('Use a new release directory.')
app = root / 'build-room3/TideRoom_artefacts/Release/Standalone/Tide Room.app'
with (app / 'Contents/Info.plist').open('rb') as stream:
    assert plistlib.load(stream)['CFBundleShortVersionString'] == '0.5.0'
checks = root / 'artifacts/motion-001'
assert 'PASS moving render' in (checks / 'scene-release.log').read_text()
release.mkdir()
(release / "Listen").mkdir()
shutil.copytree(previous / "Third-party notices", release / "Third-party notices")
shutil.copy2(previous / "ROOM-CONTROLS-0.3.md", release / "ROOM-CONTROLS-0.3.md")
shutil.copytree(app, release / app.name, symlinks=True)
subprocess.run(['codesign', '--force', '--deep', '--sign', '-', str(release / app.name)], check=True)
subprocess.run(['codesign', '--verify', '--deep', '--strict', str(release / app.name)], check=True)
shutil.copy2(previous / 'TEST-REPORT.md', release / 'EFFICIENCY-0.4.1-REPORT.md')
shutil.copy2(checks / 'report.md', release / 'TEST-REPORT.md')
shutil.copy2(root / 'native/MOTION-QUICKSTART.md', release / 'QUICKSTART.md')
shutil.copy2(root / 'native/SPATIAL-QUICKSTART.md', release / 'SPATIAL-CONTROLS.md')
source = release / 'Source'
source.mkdir()
shutil.copy2(root / 'README.md', source / 'README.md')
shutil.copytree(root / 'native', source / 'native', dirs_exist_ok=True,
                ignore=shutil.ignore_patterns('__pycache__', '.DS_Store'))
shutil.copy2(root / 'CMakeLists.txt', source / 'CMakeLists.txt')
verification = source / 'artifacts/motion-001'
verification.mkdir(parents=True)
for name in ['report.md', 'unit.log', 'scene-release.log', 'scene-initial.log',
             'spatial-regression.log', 'build-final.log', 'build-test-fix.log']:
    shutil.copy2(checks / name, verification / name)
(verification / 'scene-release').mkdir()
for name in ['room.png', 'motion.png']:
    shutil.copy2(checks / 'scene-release' / name, verification / 'scene-release' / name)
shutil.copytree(checks / 'spatial-regression', verification / 'spatial-regression')
for name in ['fixed.wav', 'motion.wav', 'fast-deep-motion.wav', 'original-placement-motion.wav']:
    shutil.copy2(checks / 'scene-release' / name, release / 'Listen' / name)
for src, dst in [('room.png', 'Room.png'), ('motion.png', 'Motion.png')]:
    shutil.copy2(checks / 'scene-release' / src, release / dst)
(release / 'README.txt').write_text('''Tide Room 0.5.0 — position LFOs

Open Tide Room.app, then Motion / LFOs... above Placement.
Choose a destination and switch an LFO on. Existing scenes start with all six off.
Each LFO offers sine/triangle, free/tempo sync, depth and phase.
The numbered room dots move; hollow markers show the centres you can drag.

Your instrument and tape controls carry over. Reverside remains separately installed.
See QUICKSTART.md for motion, SPATIAL-CONTROLS.md for tape,
and TEST-REPORT.md for measured performance and test scope.
Listen/motion.wav is a six-LFO example; fixed.wav is the same score without motion.
fast-deep-motion.wav deliberately exaggerates Doppler effects.
Source/ includes the project and verification. User settings are not included.
''')
manifest = release / 'manifest.json'
manifest.unlink(missing_ok=True)
files = {str(p.relative_to(release)): hashlib.sha256(p.read_bytes()).hexdigest()
         for p in sorted(release.rglob('*')) if p.is_file()}
manifest.write_text(json.dumps(dict(product='Tide Room', version='0.5.0',
                                    architecture='arm64', files=files), indent=2) + '\n')
subprocess.run(['ditto', '-c', '-k', '--sequesterRsrc', '--keepParent', str(release), str(archive)], check=True)
print(json.dumps(dict(app=str(release / app.name), archive=str(archive), files=len(files),
                      sha256=hashlib.sha256(archive.read_bytes()).hexdigest()), indent=2))

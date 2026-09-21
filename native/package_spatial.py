"""Package Tide Room 0.4 with source, references and reproducible verification."""
from pathlib import Path
import hashlib, json, plistlib, shutil, subprocess
root=Path(__file__).resolve().parent.parent
release=root/'dist/Tide-Room-0.4.0-mac-arm64'
archive=release.parent/(release.name+'.zip')
if release.exists() or archive.exists(): raise SystemExit('Release already exists; keep approved builds intact.')
app=root/'build-room3/TideRoom_artefacts/Release/Standalone/Tide Room.app'
with (app/'Contents/Info.plist').open('rb') as f: assert plistlib.load(f)['CFBundleShortVersionString']=='0.4.0'
release.mkdir()
shutil.copytree(app,release/app.name,symlinks=True)
subprocess.run(['codesign','--force','--deep','--sign','-',str(release/app.name)],check=True)
subprocess.run(['codesign','--verify','--deep','--strict',str(release/app.name)],check=True)
research=root/'artifacts/tape-research-2026'
verification=root/'artifacts/spatial-realtime-001'
for original,name in [(root/'native/SPATIAL-QUICKSTART.md','QUICKSTART.md'),(root/'native/ROOM-QUICKSTART.md','ROOM-CONTROLS-0.3.md'),(verification/'report.md','TEST-REPORT.md'),(research/'DSP-DESIGN.md','SPATIAL-DESIGN.md'),(research/'RESEARCH.md','TAPE-RESEARCH.md'),(verification/'checks-release/room.png','Room.png'),(verification/'checks-release/tape.png','Tape.png')]: shutil.copy2(original,release/name)
shutil.copytree(root/'dist/Tide-Room-0.3.0-mac-arm64/Third-party notices',release/'Third-party notices')
source=release/'Source';source.mkdir()
shutil.copytree(root/'native',source/'native',ignore=shutil.ignore_patterns('__pycache__','.DS_Store'))
for name in ['CMakeLists.txt','README.md','requirements.txt','lab.py','voice.py','space.py','space_round.py','criteria.json']:
    if (root/name).exists(): shutil.copy2(root/name,source/name)
shutil.copytree(root/'tests',source/'tests',ignore=shutil.ignore_patterns('__pycache__'))
measurements=source/'artifacts/room-002';measurements.mkdir(parents=True)
shutil.copy2(root/'artifacts/room-002/mit-kemar-diffuse.zip',measurements/'mit-kemar-diffuse.zip')
# All data needed by the independent waveform comparison (no vendor plugin binary).
for name in ['spatial-realtime-001','tape-research-2026']:
    dest=source/'artifacts'/name;dest.mkdir()
    for path in (root/'artifacts'/name).glob('*.py'): shutil.copy2(path,dest/path.name)
for path in ['comparison.json','checks-release.log','probes.log','plucks-release.log','room-release.log','scene.log','scene-final.log','build-release.log']:
    shutil.copy2(verification/path,source/'artifacts/spatial-realtime-001'/path)
for folder in ['probes','plucks-release','room-release']: shutil.copytree(verification/folder,source/'artifacts/spatial-realtime-001'/folder)
inputs=source/'artifacts/room-004/inputs';inputs.mkdir(parents=True)
for name in ['plucks.wav','room.wav']: shutil.copy2(root/'artifacts/room-004/inputs'/name,inputs/name)
ref=source/'artifacts/tape-research-2026/listen';ref.mkdir()
for name in ['plucks-spatial-raw.wav','room-spatial-raw.wav','listening-guide.json']: shutil.copy2(research/'listen'/name,ref/name)
listen=release/'Listen';shutil.copytree(verification/'listen',listen)
(release/'README.txt').write_text('Tide Room 0.4\n\nOpen Tide Room.app. Tape controls… → Model: Spatial. Switch Tape on.\nStart with 4x Reference and 48 kHz / 1024 samples.\nDrive 27.6 dB, HF softness 25%, trim +6.8 dB reproduce the starting research character.\nChoose 2x Balanced or 1x Eco to save CPU. Tape latency stays fixed at 16 ms / 48 kHz.\n\nExisting scenes retain their original magnetic model until you select Spatial.\nThe complete source, verification files, and research are included.\nReverside loads from your existing installation; it is not bundled.\nRead QUICKSTART.md and TEST-REPORT.md for controls and measured limits.\n')
files={str(p.relative_to(release)):hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(release.rglob('*')) if p.is_file()}
(release/'manifest.json').write_text(json.dumps(dict(product='Tide Room',version='0.4.0',architecture='arm64',files=files),indent=2)+'\n')
subprocess.run(['ditto','-c','-k','--sequesterRsrc','--keepParent',str(release),str(archive)],check=True)
print(json.dumps(dict(app=str(release/app.name),archive=str(archive),sha256=hashlib.sha256(archive.read_bytes()).hexdigest(),files=len(files)),indent=2))

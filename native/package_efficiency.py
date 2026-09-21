"""Package the tested 0.4.1 efficiency update, preserving the frozen 0.4 release."""
from pathlib import Path
import hashlib, json, plistlib, shutil, subprocess
import numpy as np
import soundfile as sf
root=Path(__file__).resolve().parent.parent
old=root/'dist/Tide-Room-0.4.0-mac-arm64'
release=root/'dist/Tide-Room-0.4.1-mac-arm64'
archive=release.parent/(release.name+'.zip')
if release.exists() or archive.exists():raise SystemExit('Use a new release directory.')
app=root/'build-room3/TideRoom_artefacts/Release/Standalone/Tide Room.app'
with (app/'Contents/Info.plist').open('rb') as f:assert plistlib.load(f)['CFBundleShortVersionString']=='0.4.1'
shutil.copytree(old,release,symlinks=True)
shutil.rmtree(release/app.name)
shutil.copytree(app,release/app.name,symlinks=True)
subprocess.run(['codesign','--force','--deep','--sign','-',str(release/app.name)],check=True)
subprocess.run(['codesign','--verify','--deep','--strict',str(release/app.name)],check=True)
shutil.copy2(old/'TEST-REPORT.md',release/'SPATIAL-0.4-REPORT.md')
shutil.copy2(root/'artifacts/efficiency-001/report.md',release/'TEST-REPORT.md')
shutil.copy2(root/'native/SPATIAL-QUICKSTART.md',release/'QUICKSTART.md')
source=release/'Source'
shutil.copytree(root/'native',source/'native',dirs_exist_ok=True,ignore=shutil.ignore_patterns('__pycache__','.DS_Store'))
shutil.copy2(root/'CMakeLists.txt',source/'CMakeLists.txt')
verification=root/'artifacts/efficiency-001'
dest=source/'artifacts/efficiency-001';dest.mkdir()
for name in ['report.md','analyze_release.py','verify_solver.py','SpatialSolver-before-spectral.cpp','SpatialSolver-before-spectral.h','solver-comparison.json','waveform-comparison.json','baseline-scene.log','engines.log','release-scene.log','release-heavy.log','release-room.log','release-plucks.log','release-checks.log','release-probes.log','build-release.log','live-before.sample.txt']:
    shutil.copy2(verification/name,dest/name)
for folder in ['release-room','release-plucks','release-probes','release-checks']:
    shutil.copytree(verification/folder,dest/folder)
shutil.copy2(verification/'release-checks/room.png',release/'Room.png')
shutil.copy2(verification/'release-heavy/tape-on.png',release/'Tape.png')
shutil.copy2(verification/'release-heavy/tape-off.png',release/'Tape-off.png')
listen=release/'Listen'
guide=json.loads((root/'artifacts/tape-research-2026/listen/listening-guide.json').read_text())
for name in ['plucks','room']:
    ref,sr=sf.read(root/'artifacts/tape-research-2026/listen'/f'{name}-spatial-raw.wav',always_2d=True)
    live,_=sf.read(verification/f'release-{name}'/'quality-4.wav',always_2d=True)
    gain=next(v['fixed_gain_db'] for v in guide['versions'] if v['path']==f'{name}-spatial.wav')
    both=np.concatenate([ref[:len(live)],np.zeros((int(sr*.75),2)),live])*10**(gain/20)
    assert np.max(np.abs(both))<1
    sf.write(listen/f'{name}-offline-then-live.wav',both,sr,subtype='PCM_24')
shutil.copy2(verification/'release-scene/room-q4.wav',listen/'generative-room-spatial-4x.wav')
shutil.copy2(verification/'release-heavy/heavy-q4.wav',listen/'dense-room-spatial-4x.wav')
(listen/'README.txt').write_text('The comparison files play eight seconds of the approved offline Spatial model, 0.75 seconds of silence, then eight seconds from the 0.4.1 live implementation. Input, alignment and fixed gain are identical.\n\ngenerative-room-spatial-4x.wav is the unnormalized 30-second scene. dense-room-spatial-4x.wav is the unnormalized 20-second maximum-drive stress scene.\n')
(release/'README.txt').write_text('Tide Room 0.4.1 — efficiency update\n\nOpen Tide Room.app. Your scene settings carry over.\nThe Tape on/off button is now explicit in both windows.\nUse 48 kHz / 1024 samples as a starting point. 2x gives extra headroom; 4x retains the strongest product-alias suppression.\nTape controls shows current and peak DSP load and over-budget blocks.\n\nSix inaudible convolution reverbs are removed from Room playback. The solver does less repeated work, and stopped playback sleeps after its tails decay.\nNo synth oversampling, tape quadrature, or Reverside quality was lowered.\nRead TEST-REPORT.md for measurements and limits, QUICKSTART.md for controls.\nSource/ includes the project and verification. Reverside remains a separate installed plugin.\n')
manifest=release/'manifest.json';manifest.unlink()
files={str(p.relative_to(release)):hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(release.rglob('*')) if p.is_file()}
manifest.write_text(json.dumps(dict(product='Tide Room',version='0.4.1',architecture='arm64',files=files),indent=2)+'\n')
subprocess.run(['ditto','-c','-k','--sequesterRsrc','--keepParent',str(release),str(archive)],check=True)
print(json.dumps(dict(app=str(release/app.name),archive=str(archive),files=len(files),sha256=hashlib.sha256(archive.read_bytes()).hexdigest()),indent=2))

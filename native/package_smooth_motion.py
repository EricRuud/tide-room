"""Package the verified moving-reflection renderer and expanded tape control."""
from pathlib import Path
import hashlib
import json
import plistlib
import shutil
import subprocess
import sys

root=Path(__file__).resolve().parent.parent
release=root/'dist/Tide-Room-0.6.0-mac-arm64'
archive=release.parent/(release.name+'.zip')
refresh='--refresh-validation' in sys.argv
assert (release.exists() and archive.exists()) if refresh else (not release.exists() and not archive.exists()), 'Use a new release directory, or refresh an existing validation package'
verification=root/'artifacts/motion-smooth-001'
app=root/'build-room3/TideRoom_artefacts/Release/Standalone/Tide Room.app'
with (app/'Contents/Info.plist').open('rb') as f:
    assert plistlib.load(f)['CFBundleShortVersionString']=='0.6.0'
for file,required in [('checks.log','PASS moving render'),('routing-drained.log','PASS renderer routing'),
                      ('softness.log','PASS extended HF softness'),('spatial-checks.log','PASS fresh defaults')]:
    content=(verification/file).read_text()
    assert required in content and 'FAIL' not in content
assert all(code==0 for code in json.loads((verification/'test-exit-codes.json').read_text()).values())
analysis=json.loads((verification/'live-analysis.json').read_text())
assert analysis['input_max_difference']==0
assert analysis['old_reverside']['large_discontinuities']==35
assert all(analysis[k]['large_discontinuities']==0 for k in ['new_reverside','new_reflections','new_mix'])
if not refresh:
    release.mkdir()
    shutil.copytree(app,release/app.name,symlinks=True)
    subprocess.run(['codesign','--force','--deep','--sign','-',str(release/app.name)],check=True)
else:
    previous=json.loads((release/'manifest.json').read_text())
    for name,digest in previous['files'].items():
        if name.startswith('Tide Room.app/'):
            assert hashlib.sha256((release/name).read_bytes()).hexdigest()==digest, 'Running app changed since packaging'
    for p in (release/'Source/native').rglob('*'):
        relative=p.relative_to(release/'Source/native')
        if p.is_file() and str(relative) not in ['Room/MotionChecks.cpp','package_smooth_motion.py']:
            assert p.read_bytes()==(root/'native'/relative).read_bytes(), 'Validation refresh cannot change app source'
subprocess.run(['codesign','--verify','--deep','--strict',str(release/app.name)],check=True)
old=root/'dist/Tide-Room-0.5.1-mac-arm64'
shutil.copytree(old/'Third-party notices',release/'Third-party notices',dirs_exist_ok=refresh)
shutil.copy2(old/'ROOM-CONTROLS-0.3.md',release/'ROOM-CONTROLS-0.3.md')
shutil.copy2(root/'native/SPATIAL-QUICKSTART.md',release/'SPATIAL-CONTROLS.md')
shutil.copy2(root/'native/MOTION-QUICKSTART.md',release/'QUICKSTART.md')
shutil.copy2(verification/'report.md',release/'TEST-REPORT.md')
listen=release/'Listening';listen.mkdir(exist_ok=refresh)
shutil.copy2(verification/'moving-room.wav',listen/'Moving room.wav')
source=release/'Source';source.mkdir(exist_ok=refresh)
shutil.copytree(root/'native',source/'native',ignore=shutil.ignore_patterns('__pycache__','.DS_Store'),dirs_exist_ok=refresh)
for name in ['CMakeLists.txt','README.md']:shutil.copy2(root/name,source/name)
checks=source/'artifacts/motion-smooth-001';checks.mkdir(parents=True,exist_ok=refresh)
for name in ['report.md','checks.log','routing.log','routing-drained.log','routing-confirm.log','test-exit-codes.json','softness.log','spatial-checks.log','live-analysis.json','analyze_live.py']:
    shutil.copy2(verification/name,checks/name)
(release/'README.txt').write_text('Tide Room 0.6.0 — moving reflections and extended tape softness\n\nOpen Tide Room.app. Motion / LFOs offers Moving reflections (new continuous wall paths with a Reverside late tail) or Reverside fixed (its original, stationary reflection character). Continuous LFO offsets no longer drive Reverside geometry.\n\nSpatial tape HF softness now ranges from 0–400%. The previous 100% retains its strength. Spatial is a custom tape model, not a calibrated emulation of a named machine.\n\nQUICKSTART.md describes motion, SPATIAL-CONTROLS.md covers tape, and TEST-REPORT.md records the live regression and limitations. The listening example is the captured moving scene at its saved tape amount. Source and third-party notices are included; Reverside and private preferences are not bundled.\n')
files={str(p.relative_to(release)):hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(release.rglob('*')) if p.is_file() and p!=release/'manifest.json'}
(release/'manifest.json').write_text(json.dumps(dict(product='Tide Room',version='0.6.0',architecture='arm64',files=files),indent=2)+'\n')
temporary=archive.with_suffix('.pending.zip')
subprocess.run(['ditto','-c','-k','--sequesterRsrc','--keepParent',str(release),str(temporary)],check=True)
temporary.replace(archive)
print(json.dumps(dict(app=str(release/app.name),files=len(files),archive=str(archive),sha256=hashlib.sha256(archive.read_bytes()).hexdigest()),indent=2))

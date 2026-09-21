"""Package the 0.5.1 control-delivery fix without duplicating older audio renders."""
from pathlib import Path
import hashlib,json,plistlib,shutil,subprocess
root=Path(__file__).resolve().parent.parent
release=root/'dist/Tide-Room-0.5.1-mac-arm64'
archive=release.parent/(release.name+'.zip')
if archive.exists():raise SystemExit('Use a new release directory.')
if release.exists() and any(p.name!='Tide Room.app' for p in release.iterdir()):raise SystemExit('Only an incomplete app-only stage may be resumed.')
old=root/'dist/Tide-Room-0.5.0-mac-arm64';verification=root/'artifacts/motion-crackle-001'
app=root/'build-room3/TideRoom_artefacts/Release/Standalone/Tide Room.app'
with (app/'Contents/Info.plist').open('rb') as f:assert plistlib.load(f)['CFBundleShortVersionString']=='0.5.1'
assert 'PASS moving render' in (verification/'fix-checks.log').read_text()
if not release.exists():
    release.mkdir();shutil.copytree(app,release/app.name,symlinks=True)
subprocess.run(['codesign','--force','--deep','--sign','-',str(release/app.name)],check=True)
subprocess.run(['codesign','--verify','--deep','--strict',str(release/app.name)],check=True)
shutil.copytree(old/'Third-party notices',release/'Third-party notices')
for name in ['SPATIAL-CONTROLS.md','ROOM-CONTROLS-0.3.md']:shutil.copy2(old/name,release/name)
shutil.copy2(root/'native/MOTION-QUICKSTART.md',release/'QUICKSTART.md')
shutil.copy2(verification/'report.md',release/'TEST-REPORT.md')
source=release/'Source';source.mkdir();shutil.copytree(root/'native',source/'native',ignore=shutil.ignore_patterns('__pycache__','.DS_Store'))
for name in ['CMakeLists.txt','README.md']:shutil.copy2(root/name,source/name)
checks=source/'artifacts/motion-crackle-001';checks.mkdir(parents=True)
for name in ['report.md','fix-checks.log','probe-analysis.json','probe2-analysis.json','controls.log']:
    shutil.copy2(verification/name,checks/name)
(release/'README.txt').write_text('Tide Room 0.5.1 — Reverside motion delivery\n\nOpen Tide Room.app. LFOs now reach Reverside through the same 20 Hz control-thread path as manual placement. Oscillator timing and your scene controls are retained.\n\nThis is a candidate fix for crackling isolated to moving Reverside reflections. The exact vendor-internal cause is not yet proven; see TEST-REPORT.md. Prior versions remain available.\n\nQUICKSTART.md covers motion. The source and third-party notices are included; Reverside and user preferences are not bundled.\n')
manifest=release/'manifest.json';files={str(p.relative_to(release)):hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(release.rglob('*')) if p.is_file()}
manifest.write_text(json.dumps(dict(product='Tide Room',version='0.5.1',architecture='arm64',files=files),indent=2)+'\n')
subprocess.run(['ditto','-c','-k','--sequesterRsrc','--keepParent',str(release),str(archive)],check=True)
print(json.dumps(dict(app=str(release/app.name),files=len(files),archive=str(archive),sha256=hashlib.sha256(archive.read_bytes()).hexdigest()),indent=2))

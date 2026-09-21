"""Build, preserve sources, test, measure, and render one native iteration."""
from datetime import datetime, timezone
from pathlib import Path
import shutil
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[1]


def main():
    stamp=datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S.%fZ')
    out=ROOT/'artifacts'/f'native-{stamp}'
    out.mkdir(parents=True,exist_ok=False)
    source=out/'source';source.mkdir()
    for name in ('native','CMakeLists.txt','criteria.json','space.py','voice.py','lab.py','requirements.txt'):
        p=ROOT/name
        if p.is_dir():shutil.copytree(p,source/name,ignore=shutil.ignore_patterns('__pycache__'))
        else:shutil.copy2(p,source/name)
    checker=ROOT/'build/TideCheck_artefacts/Release/TideCheck'
    host=ROOT/'build/TideHostCheck_artefacts/Release/TideHostCheck'
    steps=[
        ('build',['bash','native/build.sh']),
        ('checks',[str(checker)]),
        ('probes',[str(checker),'--probes',str(out/'probes')]),
        ('spectral',[sys.executable,'native/analyze.py',str(out/'probes')]),
        ('vst3-host',[str(host),str(ROOT/'build/Tide_artefacts/Release/VST3/Tide.vst3')]),
        ('renders',[str(checker),'--render',str(out/'presets')]),
    ]
    print(out,flush=True)
    for name,command in steps:
        print(f'Running {name}…',flush=True)
        with (out/f'{name}.log').open('w') as log:
            result=subprocess.run(command,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT)
        if result.returncode:
            print(f'FAILED {name}: see {out/name}.log. Sources and previous results preserved.',flush=True)
            return result.returncode
    print('Technical checks passed. Preset audio is ready for listening; taste is not scored.',flush=True)
    return 0


if __name__=='__main__':sys.exit(main())

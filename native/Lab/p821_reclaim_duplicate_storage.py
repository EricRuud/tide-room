"""Reclaim duplicate generated-file storage with independent APFS clones.

All paths and bytes remain available. Only old generated artifact/build folders
are scanned; final recordings, reference datasets and the running app are out.
"""
import collections,datetime,hashlib,json,os,stat,subprocess,tempfile,uuid
from pathlib import Path

BASE=Path(__file__).resolve().parents[2]
ROOT=BASE/'artifacts/p821-identification-001'


def digest(path):
    h=hashlib.sha256()
    with path.open('rb') as f:
        while chunk:=f.read(4*1024*1024):h.update(chunk)
    return h.hexdigest()


def free():
    s=os.statvfs(BASE);return s.f_bavail*s.f_frsize


def main():
    # Verify write independence using expendable files on the same filesystem.
    with tempfile.TemporaryDirectory(prefix='clone-check-',dir=ROOT) as folder:
        source=Path(folder)/'a';copy=Path(folder)/'b';source.write_bytes(b'A'*4096)
        subprocess.run(['/bin/cp','-cp',str(source),str(copy)],check=True)
        assert source.stat().st_ino!=copy.stat().st_ino
        with copy.open('r+b') as f:f.write(b'B')
        assert source.read_bytes()==b'A'*4096
    scopes=['artifacts/room-001','artifacts/room-002','artifacts/room-003','artifacts/room-004',
            'artifacts/room-005-driven','artifacts/room-006-cream','artifacts/room-007-dynamic',
            'artifacts/efficiency-001','artifacts/west-coast-001','build','build-room2','build-room3']
    sizes=collections.defaultdict(list)
    for scope in scopes:
        for path in (BASE/scope).rglob('*'):
            if path.is_file() and not path.is_symlink() and path.stat().st_size>=1024**2:sizes[path.stat().st_size].append(path)
    groups=collections.defaultdict(list)
    for paths in sizes.values():
        if len(paths)>1:
            for path in paths:groups[(path.stat().st_size,digest(path))].append(path)
    report=dict(created_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),scopes=scopes,
                method='cp -cp clonefile, verify bytes, preserve destination mode/timestamps, atomic replacement; no hard links',
                independent_write_test_passed=True,free_bytes_before=free(),rows=[])
    manifest=ROOT/'duplicate-storage-clones-2026-09-10.json'
    for (size,sha),paths in groups.items():
        source=paths[0]
        for dest in paths[1:]:
            before=dest.stat()
            if before.st_ino==source.stat().st_ino:continue
            temporary=dest.with_name(dest.name+'.clone-'+uuid.uuid4().hex)
            try:
                subprocess.run(['/bin/cp','-cp',str(source),str(temporary)],check=True)
                assert digest(temporary)==sha and digest(dest)==sha
                assert temporary.stat().st_ino!=source.stat().st_ino
                os.chmod(temporary,stat.S_IMODE(before.st_mode));os.utime(temporary,ns=(before.st_atime_ns,before.st_mtime_ns))
                os.replace(temporary,dest)
            finally:
                if temporary.exists():temporary.unlink()
            report['rows'].append(dict(source=str(source.relative_to(BASE)),destination=str(dest.relative_to(BASE)),bytes=size,sha256=sha))
            report['logical_duplicate_bytes']=sum(r['bytes'] for r in report['rows']);report['free_bytes_after']=free()
            manifest.write_text(json.dumps(report,indent=2))
    print('Cloned',len(report['rows']),'duplicates; logical bytes',report.get('logical_duplicate_bytes',0),
          'free bytes before/after',report['free_bytes_before'],free(),flush=True)


if __name__=='__main__':main()

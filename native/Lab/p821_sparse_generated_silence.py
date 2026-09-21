"""Keep generated WAV paths and bytes while storing long zero spans sparsely.

Explicit development prefixes only; final files and their inputs are excluded.
Every replacement is checksum-verified and atomic. Physical savings are measured.
"""
import hashlib,json,os,stat,tempfile,time
from pathlib import Path
from p821_identification_analysis import ROOT

PREFIXES=['long-memory-','training-','quality-','history-','release-generalization-','crest-','dc-detector-','limiter-']

def digest(path):
    h=hashlib.sha256()
    with path.open('rb') as f:
        while block:=f.read(1024*1024):h.update(block)
    return h.hexdigest()

def available():s=os.statvfs(ROOT);return s.f_bavail*s.f_frsize

def main():
    before=available();rows=[]
    for path in sorted(ROOT.glob('*.wav')):
        name=path.name.removeprefix('input-')
        if not any(name.startswith(prefix) for prefix in PREFIXES):continue
        assert 'test-' not in path.name and 'final' not in path.name
        info=path.stat()
        if info.st_size<4*1024**2:continue
        zero=0
        with path.open('rb') as source:
            while block:=source.read(65536):
                if not block.strip(b'\0'):zero+=len(block)
        if zero<4*1024**2 or info.st_blocks*512<=info.st_size-zero+1024**2:continue
        expected=digest(path);fd,name=tempfile.mkstemp(prefix=path.name+'.sparse-',dir=path.parent);temporary=Path(name)
        try:
            with os.fdopen(fd,'wb') as target,path.open('rb') as source:
                while block:=source.read(65536):
                    if block.strip(b'\0'):target.write(block)
                    else:target.seek(len(block),1)
                target.truncate(info.st_size);target.flush();os.fsync(target.fileno())
            assert temporary.stat().st_size==info.st_size and digest(temporary)==expected
            assert path.stat().st_ino==info.st_ino and path.stat().st_mtime_ns==info.st_mtime_ns
            os.chmod(temporary,stat.S_IMODE(info.st_mode));os.utime(temporary,ns=(info.st_atime_ns,info.st_mtime_ns))
            allocation=temporary.stat().st_blocks*512;temporary.replace(path)
            rows.append(dict(path=str(path),bytes=info.st_size,sha256=expected,allocated_before=info.st_blocks*512,allocated_after=allocation))
        finally:
            if temporary.exists():temporary.unlink()
    report=dict(unix_time=time.time(),final_files_accessed=False,free_bytes_before=before,free_bytes_after=available(),rows=rows)
    (ROOT/'sparse-generated-silence-2026-09-10.json').write_text(json.dumps(report,indent=2))
    print('Verified sparse replacements',len(rows),'free bytes before/after',before,report['free_bytes_after'],flush=True)

if __name__=='__main__':main()

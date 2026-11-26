import sys
import tarfile
from pathlib import Path

root,output=map(Path,sys.argv[1:])
def permissions(info):
    info.uid=info.gid=0;info.uname=info.gname='root'
    info.mode=0o755 if info.isdir() or Path(info.name).name in ('build.sh','YoKaiWatch2Builder') else 0o644
    return info
with tarfile.open(output,'w:gz',compresslevel=9) as archive:
    archive.add(root,arcname=root.name,filter=permissions)

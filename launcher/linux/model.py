"""Portable Linux launcher state and verified full-builder release updates."""
import hashlib
import json
import os
import re
import shutil
import tarfile
import tempfile
import urllib.request
from pathlib import Path, PurePosixPath
from urllib.parse import urlsplit

ROOT=Path(__file__).resolve().parents[2]
REPO='Okxmir/yo-kai-watch-2-recompiled'
MAX_ARCHIVE=100*1024*1024
MAX_EXPANDED=256*1024*1024
FILES={'VERSION','README.md','BUILD_INSTRUCTIONS.md','CHANGELOG.md','COMPATIBILITY.md','KNOWN_ISSUES.md','ORIGINAL_TIMING_ANALYSIS.md','PERFORMANCE.md','PROGRESS.md','RECOMPILATION_STATUS.md','LICENSE','build.ps1','build.cmd','build.sh','YoKaiWatch2Builder','.gitignore'}
DIRS={'source','scripts','launcher','tests','licenses'}
CODE_HASH='b536dfce0a7138f2d5691532604d782b1ea3e20731120cc662baee118c370547'

def version(value):
    if not isinstance(value,str) or not re.fullmatch(r'[0-9]{1,5}\.[0-9]{1,5}\.[0-9]{1,5}',value):raise ValueError('Invalid version')
    return tuple(map(int,value.split('.')))

def digest(path):
    h=hashlib.sha256()
    with Path(path).open('rb') as f:
        for b in iter(lambda:f.read(65536),b''):h.update(b)
    return h.hexdigest()

def read_json(path,default=None):
    try:return json.loads(path.read_text(encoding='utf-8'))
    except (OSError,ValueError):return default

def write_text(path,text):
    path.parent.mkdir(parents=True,exist_ok=True)
    with tempfile.NamedTemporaryFile(mode='w',encoding='utf-8',dir=path.parent,delete=False) as f:
        f.write(text);pending=Path(f.name)
    pending.replace(path)

def read_ini(path):
    try:return {k:v for line in path.read_text(encoding='utf-8-sig').splitlines() for k,s,v in [line.partition('=')] if s}
    except OSError:return {}

def write_ini(path,values):
    write_text(path,''.join(f'{k}={v}\n' for k,v in values.items() if '\n' not in str(v) and '\r' not in str(v)))

def parse_releases(releases):
    best=None
    for r in releases:
        try:
            tag=r['tag_name'];v=tag[1:]
            if not tag.startswith('v') or r['draft'] or r['prerelease']:continue
            key=version(v);name=f'Yo-kai-Watch-2-Linux-x64-v{v}.tar.gz'
            for a in r.get('assets',[]):
                if a.get('name')!=name or a.get('state')!='uploaded':continue
                if not 0<a.get('size',0)<=MAX_ARCHIVE or not re.fullmatch(r'sha256:[a-fA-F0-9]{64}',a.get('digest') or ''):continue
                url=f'https://github.com/{REPO}/releases/download/{tag}/{name}'
                if a.get('browser_download_url')!=url:continue
                if best is None or key>version(best['version']):best={'version':v,'url':url,'sha256':a['digest'][7:].lower(),'size':a['size']}
        except (KeyError,TypeError,ValueError):continue
    return best

def trusted(url):
    u=urlsplit(url)
    return u.scheme=='https' and u.hostname in ('api.github.com','github.com','release-assets.githubusercontent.com','objects.githubusercontent.com') and u.port in (None,443) and not u.username and not u.password

class Redirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self,req,fp,code,msg,headers,newurl):
        if not trusted(newurl):raise ValueError('Unexpected update redirect.')
        return super().redirect_request(req,fp,code,msg,headers,newurl)

def fetch(url,limit):
    if not trusted(url):raise ValueError('Unexpected download host.')
    request=urllib.request.Request(url,headers={'User-Agent':'YoKaiWatch2-Launcher','Accept':'application/vnd.github+json' if 'api.github.com' in url else 'application/octet-stream'})
    return urllib.request.build_opener(Redirect()).open(request,timeout=20)

def check():
    with fetch(f'https://api.github.com/repos/{REPO}/releases?per_page=30',2*1024*1024) as r:
        data=r.read(2*1024*1024+1)
    if len(data)>2*1024*1024:raise ValueError('Invalid GitHub response.')
    return parse_releases(json.loads(data))

def extract_verified(archive,stage,asset):
    if archive.stat().st_size!=asset['size'] or digest(archive)!=asset['sha256']:raise ValueError('Update SHA-256 or size mismatch.')
    prefix=f"Yo-kai-Watch-2-Linux-x64-v{asset['version']}";seen=set();total=0
    with tarfile.open(archive,'r:gz') as tar:
        members=tar.getmembers()
        if len(members)>5000:raise ValueError('Too many archive members.')
        for m in members:
            p=PurePosixPath(m.name)
            if p.is_absolute() or not p.parts or p.parts[0]!=prefix or '..' in p.parts or any(not re.fullmatch(r'[A-Za-z0-9._ -]+',part) for part in p.parts):raise ValueError('Unsafe update path.')
            if str(p).lower() in seen or not (m.isdir() or m.isfile()) or m.size>64*1024*1024:raise ValueError('Invalid archive entry.')
            seen.add(str(p).lower());total+=m.size
            if total>MAX_EXPANDED:raise ValueError('Expanded update is too large.')
            if len(p.parts)>1:
                if p.parts[1] not in FILES|DIRS or len(p.parts)>2 and p.parts[1] not in DIRS or 'runs' in p.parts or '.git' in p.parts:raise ValueError('Unmanaged update path.')
                if p.suffix.lower() in {'.3ds','.cci','.cxi','.cia','.sav','.yw'}:raise ValueError('Update contains local data.')
            target=stage.joinpath(*p.parts)
            if m.isdir():target.mkdir(parents=True,exist_ok=True)
            else:
                target.parent.mkdir(parents=True,exist_ok=True)
                with tar.extractfile(m) as source,target.open('xb') as out:shutil.copyfileobj(source,out)
                target.chmod(0o755 if target.name in ('YoKaiWatch2Builder','build.sh') else 0o644)
    payload=stage/prefix
    if (payload/'VERSION').read_text().strip()!=asset['version'] or not (payload/'build.sh').is_file() or not (payload/'launcher/linux/launcher.py').is_file() or not (payload/'source/clockwork/CMakeLists.txt').is_file():raise ValueError('Incomplete or incorrect builder update.')
    return payload

def apply_package(root,payload,backup):
    backup.mkdir(parents=True,exist_ok=True);moved=[];installed=[]
    def install(src,dest):
        rel=dest.relative_to(root);old=backup/rel
        if dest.exists() or dest.is_symlink():old.parent.mkdir(parents=True,exist_ok=True);dest.rename(old);moved.append(rel)
        dest.parent.mkdir(parents=True,exist_ok=True);src.rename(dest);installed.append(rel)
    try:
        for name in ('source','scripts','launcher','licenses'):
            if (payload/name).is_dir():install(payload/name,root/name)
        for name in FILES:
            if (payload/name).is_file():install(payload/name,root/name)
        if (payload/'tests').is_dir():
            for f in (payload/'tests').iterdir():
                if f.is_file():install(f,root/'tests'/f.name)
    except Exception:
        for rel in reversed(installed):
            failed=backup/'failed-new'/rel;failed.parent.mkdir(parents=True,exist_ok=True);(root/rel).rename(failed)
        for rel in reversed(moved):(backup/rel).rename(root/rel)
        raise

class Session:
    def __init__(self,root=ROOT):
        self.root=Path(root);self.version=(self.root/'VERSION').read_text().strip();version(self.version)
        self.data=self.root/'launcher-data';self.prefs_path=self.data/'launcher-linux.json';self.prefs=read_json(self.prefs_path,{}) or {}
        self.dist=self.root/'dist';self.executable=self.dist/'YoKaiWatch2';self.ini=self.dist/'data/settings.ini';self.required_file=self.data/'required-update-linux.txt'
    def save(self):write_text(self.prefs_path,json.dumps(self.prefs,indent=2)+'\n')
    def required(self):
        try:v=self.required_file.read_text().strip();version(v);return v
        except (OSError,ValueError):return None
    def require(self,v):
        old=self.required()
        if old is None or version(v)>version(old):write_text(self.required_file,v)
    @property
    def update_required(self):return self.required() is not None and version(self.required())>version(self.version)
    def manifest(self):
        try:
            m=read_json(self.dist/'build-manifest.json');rom=Path(m['cartridge'])
            if m['code_sha256'].lower()!=CODE_HASH or m['title_id']!='00040000001B2900' or not rom.is_file() or not self.executable.is_file() or digest(self.executable)!=m['game_exe_sha256'].lower():return None
            return m
        except (OSError,KeyError,TypeError,ValueError):return None
    @property
    def rebuild_required(self):
        m=self.manifest();return m is not None and m.get('version')!=self.version
    def update(self,asset,progress=lambda s:None):
        if version(asset['version'])<=version(self.version):raise ValueError('Cannot downgrade.')
        self.require(asset['version']);self.data.mkdir(parents=True,exist_ok=True)
        stage=Path(tempfile.mkdtemp(prefix='update-',dir=self.data));archive=stage/'package.tar.gz';total=0
        with fetch(asset['url'],asset['size']) as response,archive.open('xb') as out:
            while True:
                b=response.read(65536)
                if not b:break
                total+=len(b)
                if total>asset['size']:raise ValueError('Update exceeded expected size.')
                out.write(b)
        progress('Verifying and installing the builder…');payload=extract_verified(archive,stage,asset);apply_package(self.root,payload,stage/'backup')

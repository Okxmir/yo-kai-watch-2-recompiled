#!/usr/bin/env python3
"""Local Linux x86-64 builder. Never distribute a ROM or generated game code."""
import argparse
import hashlib
import json
import os
import platform
import re
import shutil
import subprocess
import sys
import tempfile
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / 'build'
DEPS = BUILD / 'linux-deps'
VERSION = (ROOT / 'VERSION').read_text().strip()
CODE_HASH = 'b536dfce0a7138f2d5691532604d782b1ea3e20731120cc662baee118c370547'
LANGUAGES = {'en': 1, 'fr': 2, 'de': 3, 'it': 4, 'es': 5, 'nl': 8, 'ru': 10}
RUST = '1.90.0'
HOST = 'x86_64-unknown-linux-gnu'
RUST_HASHES = {'rustc': '48c2a42de9e92fcae8c24568f5fe40d5734696a6f80e83cc6d46eef1a78f13c9',
               'rust-std': '663f4ab7945b392d5e5294dec1b050a66820a20e86f084ec37eeb0f2f7ff5569',
               'cargo': '9853db03d68578a30972e2755c89c66aec035fec641cf8f3a7117c81eec2578d'}
SDL_HASH = '082cbf5f429e0d80820f68dc2b507a94d4cc1b4e70817b119bbb8ec6a69584b8'

def digest(path):
    h = hashlib.sha256()
    with Path(path).open('rb') as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b''):
            h.update(chunk)
    return h.hexdigest()

def stage(message):
    print('Build stage: ' + message, flush=True)

def run(*args, cwd=ROOT, env=None):
    subprocess.run([str(x) for x in args], cwd=cwd, env=env, check=True)

def download(name, url, expected):
    DEPS.mkdir(parents=True, exist_ok=True)
    target = DEPS / name
    if target.is_file() and digest(target) == expected:
        return target
    with tempfile.TemporaryDirectory(dir=DEPS) as tmp:
        path = Path(tmp) / name
        print('Downloading ' + name, flush=True)
        with urllib.request.urlopen(url, timeout=90) as response, path.open('wb') as out:
            shutil.copyfileobj(response, out)
        if digest(path) != expected:
            raise ValueError('SHA-256 mismatch: ' + name)
        path.replace(target)
    return target

def rust_env():
    env = os.environ.copy()
    def usable(cargo, rustc):
        try:
            if not cargo or not rustc:
                return False
            for name, path in [('cargo', cargo), ('rustc', rustc)]:
                out = subprocess.check_output([str(path), '--version'], text=True, timeout=10)
                match = re.match(name + r' (\d+)\.(\d+)\.', out)
                if not match or tuple(map(int, match.groups())) < (1, 90):
                    return False
            return True
        except (OSError, subprocess.SubprocessError):
            return False
    cargo, rustc = shutil.which('cargo'), shutil.which('rustc')
    if usable(cargo, rustc):
        return cargo, env
    prefix = DEPS / ('rust-' + RUST)
    if not usable(prefix/'bin/cargo', prefix/'bin/rustc'):
        stage('Preparing local Rust and Cargo')
        DEPS.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=DEPS) as work:
            tmp = Path(work) / 'toolchain'; tmp.mkdir()
            for component, sha in RUST_HASHES.items():
                name = f'{component}-{RUST}-{HOST}'
                archive = download(name+'.tar.xz', f'https://static.rust-lang.org/dist/2025-09-18/{name}.tar.xz', sha)
                extraction = Path(work) / name; extraction.mkdir()
                run('tar', '-xf', archive, '-C', extraction)
                part = 'rust-std-'+HOST if component=='rust-std' else component
                shutil.copytree(extraction/name/part, tmp, dirs_exist_ok=True, symlinks=True)
            if not usable(tmp/'bin/cargo', tmp/'bin/rustc'):
                raise ValueError('Local Rust installation failed.')
            if prefix.exists():
                prefix.rename(DEPS/('rust-old-'+str(os.getpid())))
            tmp.rename(prefix)
    env['PATH'] = str(prefix/'bin') + os.pathsep + env.get('PATH','')
    env['RUSTC'] = str(prefix/'bin/rustc')
    env['CARGO_HOME'] = str(BUILD/'cargo-cache')
    return str(prefix/'bin/cargo'),env

def sdl(jobs):
    # SDL 3.4.0, the same version as the Windows runtime. Install in-project.
    prefix=DEPS/'sdl-prefix'
    if (prefix/'lib/cmake/SDL3/SDL3Config.cmake').is_file():
        return prefix
    archive=download('SDL3-3.4.0.tar.gz','https://github.com/libsdl-org/SDL/releases/download/release-3.4.0/SDL3-3.4.0.tar.gz',SDL_HASH)
    source=DEPS/'SDL3-3.4.0'
    if not (source/'CMakeLists.txt').is_file():
        with tempfile.TemporaryDirectory(dir=DEPS) as work:
            staged=Path(work)/'SDL'; staged.mkdir()
            run('tar','-xf',archive,'-C',staged,'--strip-components=1')
            staged.rename(source)
    run('cmake','-S',source,'-B',DEPS/'sdl-build','-G','Ninja','-DCMAKE_BUILD_TYPE=Release',
        '-DSDL_SHARED=ON','-DSDL_STATIC=OFF','-DSDL_TESTS=OFF','-DSDL_TEST_LIBRARY=OFF','-DSDL_EXAMPLES=OFF',
        '-DCMAKE_INSTALL_LIBDIR=lib',f'-DCMAKE_INSTALL_PREFIX={prefix}')
    run('cmake','--build',DEPS/'sdl-build','--parallel',jobs)
    run('cmake','--install',DEPS/'sdl-build')
    return prefix

def map_memory(generated):
    file=generated/'recomp_0010.c'; text=file.read_text()
    for pc,old,trial,new,count in [('00104894','029c0000','03dc0000','04500000',1),
                                  ('00105984','02400000','03800000','03f40000',2),
                                  ('001059a4','00840000','01c40000','02380000',2)]:
        pattern=rf'(?m)(^    /\* {pc}: mov r[013], #[0-9]+ \*/\r?\n    \{{\r?\n        uint32_t res = 0x)({old}|{trial}|{new})(u;)'
        text, found=re.subn(pattern,lambda m:m[1]+new+m[3],text)
        if found!=count: raise ValueError('Unexpected translation at '+pc)
    file.write_text(text,encoding='utf-8')

def settings(path, updates):
    old={}
    if path.exists():
        old={k:v for line in path.read_text(encoding='utf-8-sig').splitlines() for k,sep,v in [line.partition('=')] if sep}
    old.update({k:str(v) for k,v in updates.items()});path.parent.mkdir(parents=True,exist_ok=True)
    tmp=path.with_suffix('.pending');tmp.write_text(''.join(f'{k}={v}\n' for k,v in old.items()),encoding='utf-8');tmp.replace(path)

def build(args):
    if platform.system()!='Linux' or platform.machine() not in ('x86_64','AMD64'):
        raise ValueError('This builder requires Linux x86-64.')
    required=['cmake','ninja','cc','c++','tar']
    missing=[name for name in required if not shutil.which(name)]
    if missing: raise ValueError('Missing build dependencies: '+', '.join(missing)+'. See BUILD_INSTRUCTIONS.md.')
    BUILD.mkdir(parents=True,exist_ok=True)
    import fcntl
    with (BUILD/'linux-build.lock').open('a+') as lock:
        fcntl.flock(lock,fcntl.LOCK_EX)
        stage('Preparing dependencies');cargo,env=rust_env()
        stage('Compiling extractor and recompiler')
        target=BUILD/'linux-cargo';run(cargo,'build','--locked','--release','--manifest-path',ROOT/'source/clockwork/Cargo.toml','--target-dir',target,env=env)
        if args.runtime_check:
            stage('Compiling Linux runtime (no ROM)');prefix=sdl(args.jobs)
            native=BUILD/'linux-runtime-check';game_target='clockwork'
            generated=BUILD/'empty-generated';generated.mkdir(exist_ok=True)
        else:
            cartridge=Path(args.cartridge).expanduser().resolve() if args.cartridge else None
            if not cartridge:
                roms=[p for p in ROOT.iterdir() if p.is_file() and p.suffix.lower() in ('.3ds','.cci','.cxi','.cia')]
                if len(roms)!=1: raise ValueError('Select your decrypted EUR cartridge with --cartridge.')
                cartridge=roms[0]
            if not cartridge.is_file(): raise ValueError('Cartridge not found.')
            stage('Validating and extracting cartridge');extracted=ROOT/'extracted/yw2_eur';extracted.mkdir(parents=True,exist_ok=True)
            run(target/'release/ctrx','--code-only',cartridge,extracted)
            info=(extracted/'info.txt').read_text();code_hash=digest(extracted/'code.bin')
            if not re.search(r'program id:\s+00040000001B2900',info,re.I) or not re.search(r'product code:\s+CTR-P-BYSP',info) or code_hash!=CODE_HASH:
                raise ValueError('This cartridge is not the supported European Psychic Specters revision.')
            if args.validate_only: return
            stage('Static ARM to C recompilation');generated=ROOT/'generated/yw2_eur'
            fingerprint=hashlib.sha256(''.join(digest(p) for p in sorted((ROOT/'source/clockwork/tools/recomp/src').rglob('*.rs'))).encode()).hexdigest()
            stamp=generated/'.linux-recompiled';signature=code_hash+' '+fingerprint
            if not stamp.is_file() or stamp.read_text()!=signature or not (generated/'lookup.c').is_file():
                generated.mkdir(parents=True,exist_ok=True);run(target/'release/recomp','emit',extracted,generated);stamp.write_text(signature)
            map_memory(generated);prefix=sdl(args.jobs);native=BUILD/'linux-x64';game_target='yw2_eur'
        stage('Compiling native Linux x86-64 game')
        run('cmake','-S',ROOT/'source/clockwork','-B',native,'-G','Ninja',f'-DCMAKE_PREFIX_PATH={prefix}',
            f'-DYW2_EUR_GENERATED_DIR={generated}','-DCMAKE_BUILD_TYPE=Release','-DCMAKE_C_FLAGS_RELEASE=-O1 -DNDEBUG',
            '-DCMAKE_CXX_FLAGS_RELEASE=-O2 -DNDEBUG','-DYW2_LOCAL_ONLY=ON','-DCMAKE_BUILD_RPATH=$ORIGIN')
        run('cmake','--build',native,'--target',game_target,'--parallel',args.jobs)
        if args.runtime_check: print('Linux runtime compilation passed.');return
        out=ROOT/'dist';out.mkdir(exist_ok=True)
        # Retain the data directory, including all save files and existing preferences.
        shutil.copy2(native/'YoKaiWatch2',out/'YoKaiWatch2');shutil.copy2((prefix/'lib/libSDL3.so.0').resolve(),out/'libSDL3.so.0')
        manifest={'version':VERSION,'configuration':'Release','sanitizer':'None','cartridge':str(cartridge),'rom_sha256':digest(cartridge),
                  'code_sha256':code_hash,'title_id':'00040000001B2900','game_exe':'YoKaiWatch2','game_exe_sha256':digest(out/'YoKaiWatch2')}
        pending=out/'build-manifest.pending';pending.write_text(json.dumps(manifest,indent=2)+'\n');pending.replace(out/'build-manifest.json')
        settings(out/'data/settings.ini',{'language':LANGUAGES[args.language],'show_launcher':0,'hd_textures':0,'check_updates':0,'game_path':cartridge})
        licenses=out/'licenses';licenses.mkdir(exist_ok=True);shutil.copy2(ROOT/'source/clockwork/LICENSE',licenses/'CLOCKWORK-MIT.txt');shutil.copy2(DEPS/'SDL3-3.4.0/LICENSE.txt',licenses/'SDL3.txt')
        print('Build complete: '+str(out/'YoKaiWatch2'),flush=True)

if __name__=='__main__':
    ap=argparse.ArgumentParser();ap.add_argument('--cartridge');ap.add_argument('--language',choices=LANGUAGES,default='en')
    ap.add_argument('--jobs',type=int,default=min(6,os.cpu_count() or 1));ap.add_argument('--validate-only',action='store_true');ap.add_argument('--runtime-check',action='store_true')
    args=ap.parse_args();
    if not 1<=args.jobs<=32:ap.error('--jobs must be 1..32')
    try:build(args)
    except (OSError,ValueError,subprocess.SubprocessError) as e:print('error: '+str(e),file=sys.stderr);sys.exit(1)
